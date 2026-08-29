#include <extractpdf/extractpdf.h>

#include "pdf_edit_test_api.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rollback_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define ROLLBACK_CHECK(x) rollback_check_impl((x), #x, __LINE__)

static extractpdf_pdf_edit *rollback_open_edit(const char *path)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;

    ROLLBACK_CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    return edit;
}

static size_t rollback_field_index(
    const extractpdf_form *form,
    const char *wanted)
{
    size_t count = 0;
    size_t i;

    ROLLBACK_CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (i = 0; i < count; ++i) {
        const char *name = NULL;
        size_t size = 0;
        ROLLBACK_CHECK(extractpdf_form_field_name(form, i, &name, &size) ==
            EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) &&
            memcmp(name, wanted, size) == 0)
            return i;
    }
    ROLLBACK_CHECK(0);
    return SIZE_MAX;
}

static extractpdf_form_field_ref rollback_field_ref(
    extractpdf_pdf_edit *edit,
    const char *name)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    size_t index;

    ROLLBACK_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    index = rollback_field_index(form, name);
    ROLLBACK_CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, index, &ref) ==
        EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    return ref;
}

static void rollback_expect_same_ref(
    const extractpdf_form_field_ref *left,
    const extractpdf_form_field_ref *right)
{
    ROLLBACK_CHECK(left != NULL);
    ROLLBACK_CHECK(right != NULL);
    ROLLBACK_CHECK(memcmp(left, right, sizeof(*left)) == 0);
}

static void rollback_make_text(
    extractpdf_form_value_input *value,
    extractpdf_form_value_update *update,
    const char *text)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    value->utf8 = text;
    value->utf8_size = strlen(text);
    update->struct_size = sizeof(*update);
    update->presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void rollback_make_option(
    extractpdf_form_value_input *value,
    extractpdf_form_value_update *update,
    size_t option_index)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = EXTRACTPDF_FORM_VALUE_OPTION;
    value->option_index = option_index;
    update->struct_size = sizeof(*update);
    update->presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void rollback_copy_editor_output(
    extractpdf_pdf_edit *edit,
    unsigned char **out_data,
    size_t *out_size)
{
    extractpdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    ROLLBACK_CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    ROLLBACK_CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
    extractpdf_drop_output(output);
}

static void rollback_expect_failed_atomic(
    extractpdf_pdf_edit *edit,
    const char *field_name,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update,
    extractpdf_test_pdf_edit_fault fault)
{
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref rediscovered;

    rollback_copy_editor_output(edit, &before, &before_size);
    extractpdf_test_pdf_edit_set_fault(edit, fault);
    ROLLBACK_CHECK(extractpdf_pdf_edit_form_set_values(edit, ref, update) ==
        EXTRACTPDF_ERROR_MUPDF);
    rollback_copy_editor_output(edit, &after, &after_size);
    ROLLBACK_CHECK(before_size == after_size);
    ROLLBACK_CHECK(memcmp(before, after, before_size) == 0);
    ROLLBACK_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    rediscovered = rollback_field_ref(edit, field_name);
    rollback_expect_same_ref(ref, &rediscovered);
    free(before);
    free(after);
}

static void rollback_expect_text(
    extractpdf_pdf_edit *edit,
    const char *name,
    const char *expected)
{
    extractpdf_form *form = NULL;
    size_t index;
    extractpdf_form_field_info info = {0};
    extractpdf_form_value_info value_info = {0};
    const char *text = NULL;
    size_t size = 0;

    ROLLBACK_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    index = rollback_field_index(form, name);
    info.struct_size = sizeof(info);
    ROLLBACK_CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(info.type == EXTRACTPDF_FORM_FIELD_TEXT);
    ROLLBACK_CHECK(info.value_presence == EXTRACTPDF_FORM_VALUE_PRESENT);
    ROLLBACK_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    ROLLBACK_CHECK(extractpdf_form_field_value_get_info(form, index, 0,
        &value_info) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
    ROLLBACK_CHECK(extractpdf_form_field_value_utf8(form, index, 0,
        &text, &size) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(text != NULL && size == strlen(expected));
    ROLLBACK_CHECK(memcmp(text, expected, size) == 0);
    extractpdf_drop_form(form);
}

static void rollback_expect_button_option(
    extractpdf_pdf_edit *edit,
    const char *name,
    size_t expected_option)
{
    extractpdf_form *form = NULL;
    size_t index;
    extractpdf_form_field_info info = {0};
    extractpdf_form_value_info value_info = {0};

    ROLLBACK_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    index = rollback_field_index(form, name);
    info.struct_size = sizeof(info);
    ROLLBACK_CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(info.type == EXTRACTPDF_FORM_FIELD_CHECKBOX);
    ROLLBACK_CHECK(info.value_presence == EXTRACTPDF_FORM_VALUE_PRESENT);
    ROLLBACK_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    ROLLBACK_CHECK(extractpdf_form_field_value_get_info(form, index, 0,
        &value_info) == EXTRACTPDF_OK);
    ROLLBACK_CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_OPTION);
    ROLLBACK_CHECK(value_info.option_index == expected_option);
    extractpdf_drop_form(form);
}

static void test_direct_field_ref_survives_semantic_rollback(void)
{
    extractpdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_DIRECT_FIELD_PDF);
    extractpdf_form_field_ref ref = rollback_field_ref(edit, "direct");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;

    rollback_make_text(&value, &update, "failed");
    rollback_expect_failed_atomic(edit, "direct", &ref, &update,
        EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE);
    rollback_expect_text(edit, "direct", "before");

    rollback_make_text(&value, &update, "after");
    ROLLBACK_CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        EXTRACTPDF_OK);
    rollback_expect_text(edit, "direct", "after");
    extractpdf_drop_pdf_edit(edit);
}

static void test_button_first_state_rollback_and_reuse(void)
{
    extractpdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form_field_ref ref = rollback_field_ref(edit, "check");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;

    rollback_make_option(&value, &update, 0);
    rollback_expect_failed_atomic(edit, "check", &ref, &update,
        EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE);
    ROLLBACK_CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        EXTRACTPDF_OK);
    rollback_expect_button_option(edit, "check", 0);
    extractpdf_drop_pdf_edit(edit);
}

static void test_first_ap_refresh_rollback_and_reuse(void)
{
    extractpdf_pdf_edit *edit = rollback_open_edit(FORM_MUTATION_EVENTS_PDF);
    extractpdf_form_field_ref ref = rollback_field_ref(edit, "target");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;

    rollback_make_text(&value, &update, "AFTER");
    rollback_expect_failed_atomic(edit, "target", &ref, &update,
        EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH);
    rollback_expect_text(edit, "target", "BEFORE");
    rollback_expect_text(edit, "unrelated", "UNCHANGED");
    rollback_expect_text(edit, "calc", "CALC-SENTINEL");

    ROLLBACK_CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        EXTRACTPDF_OK);
    rollback_expect_text(edit, "target", "AFTER");
    rollback_expect_text(edit, "unrelated", "UNCHANGED");
    rollback_expect_text(edit, "calc", "CALC-SENTINEL");
    extractpdf_drop_pdf_edit(edit);
}

int extractpdf_pdf_form_rollback_main(void)
{
    test_direct_field_ref_survives_semantic_rollback();
    test_button_first_state_rollback_and_reuse();
    test_first_ap_refresh_rollback_and_reuse();
    return EXIT_SUCCESS;
}
