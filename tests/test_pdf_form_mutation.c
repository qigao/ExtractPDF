#include <extractpdf/extractpdf.h>

#include "pdf_edit_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORM_MUTATION_ROUNDTRIP_PDF "acroform-mutation-roundtrip-output.pdf"

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static void compile_surface(void)
{
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "x";
    value.utf8_size = 1;

    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    if (0) {
        (void)extractpdf_pdf_edit_form_snapshot(edit, &form);
        (void)extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref);
        (void)extractpdf_pdf_edit_form_set_values(edit, &ref, &update);
    }
}

static void expect_form_count(const extractpdf_form *form, size_t expected)
{
    size_t count = SIZE_MAX;
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == expected);
}

static void copy_output(
    const extractpdf_output *output,
    unsigned char **out_data,
    size_t *out_size)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    CHECK(data != NULL);
    CHECK(size != 0);
    *out_data = (unsigned char *)malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
}

static void copy_editor_output(
    extractpdf_pdf_edit *edit,
    unsigned char **out_data,
    size_t *out_size)
{
    extractpdf_output *output = NULL;

    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    copy_output(output, out_data, out_size);
    extractpdf_drop_output(output);
}

static extractpdf_output *snapshot_source(
    const char *path,
    int observe_document_form)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_output *output = NULL;
    extractpdf_form *form = NULL;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    if (observe_document_form) {
        CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
        CHECK(form != NULL);
        extractpdf_drop_form(form);
    }
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    extractpdf_drop_pdf_edit(edit);
    extractpdf_close(document);
    return output;
}

static extractpdf_form_field_ref field_ref_by_name(
    extractpdf_pdf_edit *edit,
    const char *wanted)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    size_t count = 0;
    size_t index;

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (index = 0; index < count; ++index) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(extractpdf_form_field_name(form, index, &name, &size) == EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) &&
            memcmp(name, wanted, size) == 0) {
            CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, index, &ref) ==
                EXTRACTPDF_OK);
            extractpdf_drop_form(form);
            return ref;
        }
    }
    extractpdf_drop_form(form);
    CHECK(0);
    return ref;
}

static size_t form_field_index_by_name(const extractpdf_form *form, const char *wanted)
{
    size_t count = 0;
    size_t index;

    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (index = 0; index < count; ++index) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(extractpdf_form_field_name(form, index, &name, &size) == EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) &&
            memcmp(name, wanted, size) == 0)
            return index;
    }
    CHECK(0);
    return SIZE_MAX;
}

static void expect_text_field(
    const extractpdf_form *form,
    const char *name,
    extractpdf_form_value_presence presence,
    const char *expected)
{
    size_t index = form_field_index_by_name(form, name);
    extractpdf_form_field_info info = {0};

    info.struct_size = sizeof(info);
    CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    CHECK(info.type == EXTRACTPDF_FORM_FIELD_TEXT);
    CHECK(info.value_presence == presence);
    if (presence == EXTRACTPDF_FORM_VALUE_MISSING) {
        CHECK(info.value_count == 0);
    } else {
        extractpdf_form_value_info value_info = {0};
        const char *text = NULL;
        size_t size = 0;

        CHECK(presence == EXTRACTPDF_FORM_VALUE_PRESENT);
        CHECK(info.value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(extractpdf_form_field_value_get_info(form, index, 0, &value_info) ==
            EXTRACTPDF_OK);
        CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
        CHECK(extractpdf_form_field_value_utf8(form, index, 0, &text, &size) ==
            EXTRACTPDF_OK);
        CHECK(text != NULL);
        CHECK(size == strlen(expected));
        CHECK(memcmp(text, expected, size) == 0);
    }
}

static void test_editor_snapshot_and_refs(void)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form *first = NULL;
    extractpdf_form *second = NULL;
    extractpdf_form_field_ref ref0 = {{9, 9}};
    extractpdf_form_field_ref ref0_again = {{8, 8}};
    extractpdf_form_field_ref ref1 = {{7, 7}};
    extractpdf_form_field_ref bad = {{6, 6}};

    CHECK(extractpdf_open(FORM_MUTATION_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &first) == EXTRACTPDF_OK);
    CHECK(first != NULL);
    expect_form_count(first, 4);

    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref0) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref0_again) == EXTRACTPDF_OK);
    CHECK(memcmp(&ref0, &ref0_again, sizeof(ref0)) == 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 1, &ref1) == EXTRACTPDF_OK);
    CHECK(memcmp(&ref0, &ref1, sizeof(ref0)) != 0);

    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, SIZE_MAX, &bad) ==
        EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bad.opaque[0] == 0);
    CHECK(bad.opaque[1] == 0);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &second) == EXTRACTPDF_OK);
    CHECK(second != NULL);
    extractpdf_drop_pdf_edit(edit);

    expect_form_count(first, 4);
    expect_form_count(second, 4);
    extractpdf_drop_form(first);
    extractpdf_drop_form(second);
}

static void test_need_appearances_observation_is_byte_preserving(void)
{
    extractpdf_output *plain = snapshot_source(
        FORM_MUTATION_NEED_APPEARANCES_PDF, 0);
    extractpdf_output *observed = snapshot_source(
        FORM_MUTATION_NEED_APPEARANCES_PDF, 1);
    unsigned char *plain_data = NULL;
    unsigned char *observed_data = NULL;
    size_t plain_size = 0;
    size_t observed_size = 0;
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_output *before = NULL;
    extractpdf_output *after = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    unsigned char *before_data = NULL;
    unsigned char *after_data = NULL;
    size_t before_size = 0;
    size_t after_size = 0;

    copy_output(plain, &plain_data, &plain_size);
    copy_output(observed, &observed_data, &observed_size);
    CHECK(plain_size == observed_size);
    CHECK(memcmp(plain_data, observed_data, plain_size) == 0);
    free(plain_data);
    free(observed_data);
    extractpdf_drop_output(plain);
    extractpdf_drop_output(observed);

    CHECK(extractpdf_open(FORM_MUTATION_NEED_APPEARANCES_PDF, NULL, &document) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &before) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    expect_form_count(form, 1);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref) == EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &after) == EXTRACTPDF_OK);

    copy_output(before, &before_data, &before_size);
    copy_output(after, &after_data, &after_size);
    CHECK(before_size == after_size);
    CHECK(memcmp(before_data, after_data, before_size) == 0);

    free(before_data);
    free(after_data);
    extractpdf_drop_output(before);
    extractpdf_drop_output(after);
    extractpdf_drop_pdf_edit(edit);
}

static void test_widget_prepare_is_byte_preserving(void)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form_field_ref ref;
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;

    CHECK(extractpdf_open(FORM_MUTATION_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    ref = field_ref_by_name(edit, "textWidget");

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "new-text";
    value.utf8_size = 8;
    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    copy_editor_output(edit, &before, &before_size);
    extractpdf_test_pdf_edit_set_fault(
        edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        EXTRACTPDF_ERROR_MUPDF);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size);
    CHECK(memcmp(before, after, before_size) == 0);

    free(before);
    free(after);
    extractpdf_drop_pdf_edit(edit);
}

static void test_zero_widget_text_roundtrip(void)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form_field_ref ref;
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};
    extractpdf_form *form = NULL;
    extractpdf_output *output = NULL;
    extractpdf_document *reopened = NULL;

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(extractpdf_open(FORM_MUTATION_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    ref = field_ref_by_name(edit, "zero");

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_MISSING, "");
    extractpdf_drop_form(form);
    form = NULL;

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "alpha";
    value.utf8_size = 5;
    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_PRESENT, "alpha");
    extractpdf_drop_form(form);
    form = NULL;

    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(reopened, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_PRESENT, "alpha");
    extractpdf_drop_form(form);
    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    remove(FORM_MUTATION_ROUNDTRIP_PDF);

    update.presence = EXTRACTPDF_FORM_VALUE_MISSING;
    update.values = NULL;
    update.value_count = 0;
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_MISSING, "");
    extractpdf_drop_form(form);
    extractpdf_drop_pdf_edit(edit);
}

static void test_api_reset(void)
{
    extractpdf_form *form = (extractpdf_form *)(uintptr_t)1;
    extractpdf_form_field_ref ref = {{UINT64_MAX, UINT64_MAX}};

    CHECK(extractpdf_pdf_edit_form_snapshot(NULL, &form) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(form == NULL);
    CHECK(extractpdf_pdf_edit_form_snapshot(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(NULL, 0, &ref) ==
        EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(ref.opaque[0] == 0);
    CHECK(ref.opaque[1] == 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(NULL, 0, NULL) ==
        EXTRACTPDF_ERROR_ARGUMENT);
}

int main(void)
{
    compile_surface();
    test_api_reset();
    test_editor_snapshot_and_refs();
    test_need_appearances_observation_is_byte_preserving();
    test_widget_prepare_is_byte_preserving();
    test_zero_widget_text_roundtrip();
    return EXIT_SUCCESS;
}
