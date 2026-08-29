#include <extractpdf/extractpdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return EXIT_SUCCESS;
}
