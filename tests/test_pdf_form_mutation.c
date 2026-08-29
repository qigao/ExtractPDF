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

static void copy_output(const extractpdf_output *output,
    unsigned char **out_data, size_t *out_size)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
}

static void copy_editor_output(extractpdf_pdf_edit *edit,
    unsigned char **out_data, size_t *out_size)
{
    extractpdf_output *output = NULL;
    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    copy_output(output, out_data, out_size);
    extractpdf_drop_output(output);
}

static size_t count_bytes(const unsigned char *data, size_t size, const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t i;
    size_t count = 0;

    if (needle_size == 0 || needle_size > size)
        return 0;
    for (i = 0; i + needle_size <= size; ++i)
        if (memcmp(data + i, needle, needle_size) == 0)
            ++count;
    return count;
}

static extractpdf_output *snapshot_source(const char *path, int observe_document_form)
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

static extractpdf_form_field_ref field_ref_by_name(extractpdf_pdf_edit *edit,
    const char *wanted)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    size_t count = 0;
    size_t index;

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (index = 0; index < count; ++index) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(extractpdf_form_field_name(form, index, &name, &size) == EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) && memcmp(name, wanted, size) == 0) {
            CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, index, &ref) == EXTRACTPDF_OK);
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
        if (name != NULL && size == strlen(wanted) && memcmp(name, wanted, size) == 0)
            return index;
    }
    CHECK(0);
    return SIZE_MAX;
}

static void expect_text_field(const extractpdf_form *form, const char *name,
    extractpdf_form_value_presence presence, const char *expected)
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
        CHECK(presence == EXTRACTPDF_FORM_VALUE_PRESENT && info.value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(extractpdf_form_field_value_get_info(form, index, 0, &value_info) == EXTRACTPDF_OK);
        CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
        CHECK(extractpdf_form_field_value_utf8(form, index, 0, &text, &size) == EXTRACTPDF_OK);
        CHECK(text != NULL && size == strlen(expected));
        CHECK(memcmp(text, expected, size) == 0);
    }
}

static void expect_button_field(const extractpdf_form *form, const char *name,
    extractpdf_form_field_type type, extractpdf_form_value_presence presence,
    size_t value_count, size_t option_index)
{
    size_t index = form_field_index_by_name(form, name);
    extractpdf_form_field_info info = {0};

    info.struct_size = sizeof(info);
    CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    CHECK(info.type == type);
    CHECK(info.value_presence == presence);
    CHECK(info.value_count == value_count);
    if (value_count != 0) {
        extractpdf_form_value_info value_info = {0};
        CHECK(value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(extractpdf_form_field_value_get_info(form, index, 0, &value_info) == EXTRACTPDF_OK);
        CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_OPTION);
        CHECK(value_info.option_index == option_index);
    }
}

static void make_text_update(extractpdf_form_value_input *value,
    extractpdf_form_value_update *update, const char *text, size_t size)
{
    memset(value, 0, sizeof(*value));
    memset(update, 0, sizeof(*update));
    value->struct_size = sizeof(*value);
    value->kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    value->utf8 = text;
    value->utf8_size = size;
    update->struct_size = sizeof(*update);
    update->presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update->values = value;
    update->value_count = 1;
}

static void make_option_update(extractpdf_form_value_input *value,
    extractpdf_form_value_update *update, size_t option_index)
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

static void make_empty_update(extractpdf_form_value_update *update,
    extractpdf_form_value_presence presence)
{
    memset(update, 0, sizeof(*update));
    update->struct_size = sizeof(*update);
    update->presence = presence;
    update->values = NULL;
    update->value_count = 0;
}

static extractpdf_pdf_edit *open_edit(const char *path)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    return edit;
}

static void test_editor_snapshot_and_refs(void)
{
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form *first = NULL;
    extractpdf_form *second = NULL;
    extractpdf_form_field_ref ref0 = {{9, 9}}, ref0_again = {{8, 8}}, ref1 = {{7, 7}}, bad = {{6, 6}};

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &first) == EXTRACTPDF_OK);
    expect_form_count(first, 4);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref0) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref0_again) == EXTRACTPDF_OK);
    CHECK(memcmp(&ref0, &ref0_again, sizeof(ref0)) == 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 1, &ref1) == EXTRACTPDF_OK);
    CHECK(memcmp(&ref0, &ref1, sizeof(ref0)) != 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, SIZE_MAX, &bad) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bad.opaque[0] == 0 && bad.opaque[1] == 0);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &second) == EXTRACTPDF_OK);
    extractpdf_drop_pdf_edit(edit);
    expect_form_count(first, 4);
    expect_form_count(second, 4);
    extractpdf_drop_form(first);
    extractpdf_drop_form(second);
}

static void test_need_appearances_observation_is_byte_preserving(void)
{
    extractpdf_output *plain = snapshot_source(FORM_MUTATION_NEED_APPEARANCES_PDF, 0);
    extractpdf_output *observed = snapshot_source(FORM_MUTATION_NEED_APPEARANCES_PDF, 1);
    unsigned char *plain_data = NULL, *observed_data = NULL;
    size_t plain_size = 0, observed_size = 0;
    extractpdf_pdf_edit *edit;
    extractpdf_output *before = NULL, *after = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    unsigned char *before_data = NULL, *after_data = NULL;
    size_t before_size = 0, after_size = 0;

    copy_output(plain, &plain_data, &plain_size);
    copy_output(observed, &observed_data, &observed_size);
    CHECK(plain_size == observed_size && memcmp(plain_data, observed_data, plain_size) == 0);
    free(plain_data); free(observed_data);
    extractpdf_drop_output(plain); extractpdf_drop_output(observed);

    edit = open_edit(FORM_MUTATION_NEED_APPEARANCES_PDF);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &before) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_form_count(form, 1);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref) == EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &after) == EXTRACTPDF_OK);
    copy_output(before, &before_data, &before_size);
    copy_output(after, &after_data, &after_size);
    CHECK(before_size == after_size && memcmp(before_data, after_data, before_size) == 0);
    free(before_data); free(after_data);
    extractpdf_drop_output(before); extractpdf_drop_output(after);
    extractpdf_drop_pdf_edit(edit);
}

static void test_widget_prepare_is_byte_preserving(void)
{
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form_field_ref ref = field_ref_by_name(edit, "textWidget");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    unsigned char *before = NULL, *after = NULL;
    size_t before_size = 0, after_size = 0;

    make_text_update(&value, &update, "new-text", 8);
    copy_editor_output(edit, &before, &before_size);
    extractpdf_test_pdf_edit_set_fault(edit, EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_MUPDF);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size && memcmp(before, after, before_size) == 0);
    free(before); free(after);
    extractpdf_drop_pdf_edit(edit);
}

static void test_zero_widget_text_roundtrip(void)
{
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form_field_ref ref = field_ref_by_name(edit, "zero");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_form *form = NULL;
    extractpdf_output *output = NULL;
    extractpdf_document *reopened = NULL;

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_MISSING, "");
    extractpdf_drop_form(form); form = NULL;

    make_text_update(&value, &update, "alpha", 5);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_PRESENT, "alpha");
    extractpdf_drop_form(form); form = NULL;

    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) == EXTRACTPDF_OK);
    CHECK(extractpdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(reopened, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_PRESENT, "alpha");
    extractpdf_drop_form(form); extractpdf_close(reopened); extractpdf_drop_output(output);
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

static void test_text_validation_and_noop(void)
{
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form_field_ref ref = field_ref_by_name(edit, "zero");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_form *form = NULL;
    unsigned char invalid[] = {0xC0, 0x80};
    char embedded[] = {'a', '\0', 'b'};
    unsigned char *before = NULL, *after = NULL;
    size_t before_size = 0, after_size = 0;
    static const char empty[] = "";

    make_text_update(&value, &update, (const char *)invalid, sizeof(invalid));
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, embedded, sizeof(embedded));
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, empty, 0);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "zero", EXTRACTPDF_FORM_VALUE_PRESENT, "");
    extractpdf_drop_form(form);

    copy_editor_output(edit, &before, &before_size);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size && memcmp(before, after, before_size) == 0);
    free(before); free(after);
    extractpdf_drop_pdf_edit(edit);
}

static void test_text_modes_and_preflight(void)
{
    static const struct {
        const char *name;
        extractpdf_status expected;
    } cases[] = {
        {"readonly", EXTRACTPDF_ERROR_STATE},
        {"required", EXTRACTPDF_OK},
        {"noexport", EXTRACTPDF_OK},
        {"rich", EXTRACTPDF_ERROR_UNSUPPORTED},
        {"file", EXTRACTPDF_ERROR_UNSUPPORTED},
        {"push", EXTRACTPDF_ERROR_UNSUPPORTED},
        {"sig", EXTRACTPDF_ERROR_UNSUPPORTED},
        {"unknown", EXTRACTPDF_ERROR_UNSUPPORTED}
    };
    size_t i;
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;

    make_text_update(&value, &update, "changed", 7);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_MODES_PDF);
        extractpdf_form_field_ref ref = field_ref_by_name(edit, cases[i].name);
        CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == cases[i].expected);
        extractpdf_drop_pdf_edit(edit);
    }
    {
        extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_NEED_APPEARANCES_PDF);
        extractpdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_UNSUPPORTED);
        extractpdf_drop_pdf_edit(edit);
    }
    {
        extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_XFA_PDF);
        extractpdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_UNSUPPORTED);
        extractpdf_drop_pdf_edit(edit);
    }
    {
        extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BAD_NEED_APPEARANCES_PDF);
        extractpdf_form_field_ref ref = field_ref_by_name(edit, "field");
        CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_ERROR_FORMAT);
        extractpdf_drop_pdf_edit(edit);
    }
}

static void test_text_group_and_inheritance(void)
{
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_form *form = NULL;
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_GROUPS_PDF);
    extractpdf_form_field_ref group = field_ref_by_name(edit, "g");
    extractpdf_form_field_ref target = field_ref_by_name(edit, "target");

    make_text_update(&value, &update, "new-group", 9);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &group, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "g", EXTRACTPDF_FORM_VALUE_PRESENT, "new-group");
    expect_text_field(form, "target", EXTRACTPDF_FORM_VALUE_PRESENT, "shared");
    expect_text_field(form, "sibling", EXTRACTPDF_FORM_VALUE_PRESENT, "shared");
    extractpdf_drop_form(form); form = NULL;

    update.presence = EXTRACTPDF_FORM_VALUE_MISSING;
    update.values = NULL;
    update.value_count = 0;
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &target, &update) == EXTRACTPDF_ERROR_UNSUPPORTED);

    make_text_update(&value, &update, "local", 5);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &target, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_text_field(form, "target", EXTRACTPDF_FORM_VALUE_PRESENT, "local");
    expect_text_field(form, "sibling", EXTRACTPDF_FORM_VALUE_PRESENT, "shared");
    extractpdf_drop_form(form);
    extractpdf_drop_pdf_edit(edit);
}

static void test_button_mutation_and_ap_preservation(void)
{
    extractpdf_pdf_edit *edit = open_edit(FORM_MUTATION_BASIC_PDF);
    extractpdf_form_field_ref check = field_ref_by_name(edit, "check");
    extractpdf_form_field_ref radio = field_ref_by_name(edit, "radio");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_form *form = NULL;
    extractpdf_output *output = NULL;
    extractpdf_document *reopened = NULL;
    unsigned char *bytes = NULL;
    size_t size = 0;

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "check", EXTRACTPDF_FORM_FIELD_CHECKBOX,
        EXTRACTPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    expect_button_field(form, "radio", EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,
        EXTRACTPDF_FORM_VALUE_PRESENT, 1, 0);
    extractpdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 0);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &check, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "check", EXTRACTPDF_FORM_FIELD_CHECKBOX,
        EXTRACTPDF_FORM_VALUE_PRESENT, 1, 0);
    extractpdf_drop_form(form); form = NULL;

    make_empty_update(&update, EXTRACTPDF_FORM_VALUE_PRESENT);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &check, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "check", EXTRACTPDF_FORM_FIELD_CHECKBOX,
        EXTRACTPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    extractpdf_drop_form(form); form = NULL;

    make_empty_update(&update, EXTRACTPDF_FORM_VALUE_MISSING);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &check, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "check", EXTRACTPDF_FORM_FIELD_CHECKBOX,
        EXTRACTPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    extractpdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 1);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "radio", EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,
        EXTRACTPDF_FORM_VALUE_PRESENT, 1, 1);
    extractpdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 0);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_OK);
    make_empty_update(&update, EXTRACTPDF_FORM_VALUE_PRESENT);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "radio", EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,
        EXTRACTPDF_FORM_VALUE_PRESENT, 0, SIZE_MAX);
    extractpdf_drop_form(form); form = NULL;

    make_empty_update(&update, EXTRACTPDF_FORM_VALUE_MISSING);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "radio", EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,
        EXTRACTPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    extractpdf_drop_form(form); form = NULL;

    make_option_update(&value, &update, 99);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_ERROR_ARGUMENT);
    make_text_update(&value, &update, "bad", 3);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &radio, &update) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    copy_output(output, &bytes, &size);
    CHECK(count_bytes(bytes, size, "CHECK-AP-KEEP-OFF") == 1);
    CHECK(count_bytes(bytes, size, "CHECK-AP-KEEP-YES") == 1);
    CHECK(count_bytes(bytes, size, "RADIO-AP-KEEP-OFF") == 1);
    CHECK(count_bytes(bytes, size, "RADIO-AP-KEEP-ON") == 1);
    free(bytes);

    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    CHECK(extractpdf_output_save_file(output, FORM_MUTATION_ROUNDTRIP_PDF) == EXTRACTPDF_OK);
    CHECK(extractpdf_open(FORM_MUTATION_ROUNDTRIP_PDF, NULL, &reopened) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(reopened, &form) == EXTRACTPDF_OK);
    expect_button_field(form, "check", EXTRACTPDF_FORM_FIELD_CHECKBOX,
        EXTRACTPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    expect_button_field(form, "radio", EXTRACTPDF_FORM_FIELD_RADIO_BUTTON,
        EXTRACTPDF_FORM_VALUE_MISSING, 0, SIZE_MAX);
    extractpdf_drop_form(form);
    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    remove(FORM_MUTATION_ROUNDTRIP_PDF);
    extractpdf_drop_pdf_edit(edit);
}

static void test_api_reset(void)
{
    extractpdf_form *form = (extractpdf_form *)(uintptr_t)1;
    extractpdf_form_field_ref ref = {{UINT64_MAX, UINT64_MAX}};
    CHECK(extractpdf_pdf_edit_form_snapshot(NULL, &form) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(form == NULL);
    CHECK(extractpdf_pdf_edit_form_snapshot(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(NULL, 0, &ref) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(ref.opaque[0] == 0 && ref.opaque[1] == 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(NULL, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
}

int main(void)
{
    compile_surface();
    test_api_reset();
    test_editor_snapshot_and_refs();
    test_need_appearances_observation_is_byte_preserving();
    test_widget_prepare_is_byte_preserving();
    test_zero_widget_text_roundtrip();
    test_text_validation_and_noop();
    test_text_modes_and_preflight();
    test_text_group_and_inheritance();
    test_button_mutation_and_ap_preservation();
    return EXIT_SUCCESS;
}
