#include <extractpdf/extractpdf.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORM_AP_OUTPUT_A "acroform-mutation-appearance-a.pdf"
#define FORM_AP_OUTPUT_B "acroform-mutation-appearance-b.pdf"
#define FORM_AP_OUTPUT_CHOICE "acroform-mutation-appearance-choice.pdf"

static void ap_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define AP_CHECK(x) ap_check_impl((x), #x, __LINE__)

static size_t ap_field_index_by_name(const extractpdf_form *form, const char *wanted)
{
    size_t count = 0;
    size_t i;

    AP_CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (i = 0; i < count; ++i) {
        const char *name = NULL;
        size_t size = 0;
        AP_CHECK(extractpdf_form_field_name(form, i, &name, &size) == EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) && memcmp(name, wanted, size) == 0)
            return i;
    }
    AP_CHECK(0);
    return SIZE_MAX;
}

static extractpdf_form_field_ref ap_field_ref_by_name(
    extractpdf_pdf_edit *edit,
    const char *wanted)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    size_t index;

    AP_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    index = ap_field_index_by_name(form, wanted);
    AP_CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, index, &ref) == EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    return ref;
}

static extractpdf_pdf_edit *ap_open_edit(const char *path)
{
    extractpdf_document *document = NULL;
    extractpdf_pdf_edit *edit = NULL;

    AP_CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_pdf_edit_begin(document, &edit) == EXTRACTPDF_OK);
    extractpdf_close(document);
    return edit;
}

static void ap_expect_text(
    const extractpdf_form *form,
    const char *name,
    const char *expected)
{
    size_t index = ap_field_index_by_name(form, name);
    extractpdf_form_field_info info = {0};
    extractpdf_form_value_info value_info = {0};
    const char *text = NULL;
    size_t size = 0;

    info.struct_size = sizeof(info);
    AP_CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    AP_CHECK(info.type == EXTRACTPDF_FORM_FIELD_TEXT);
    AP_CHECK(info.value_presence == EXTRACTPDF_FORM_VALUE_PRESENT);
    AP_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    AP_CHECK(extractpdf_form_field_value_get_info(form, index, 0, &value_info) ==
        EXTRACTPDF_OK);
    AP_CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
    AP_CHECK(extractpdf_form_field_value_utf8(form, index, 0, &text, &size) ==
        EXTRACTPDF_OK);
    AP_CHECK(text != NULL && size == strlen(expected));
    AP_CHECK(memcmp(text, expected, size) == 0);
}

static void ap_expect_choice_option(
    const extractpdf_form *form,
    const char *name,
    extractpdf_form_field_type type,
    size_t expected_option)
{
    size_t index = ap_field_index_by_name(form, name);
    extractpdf_form_field_info info = {0};
    extractpdf_form_value_info value_info = {0};

    info.struct_size = sizeof(info);
    AP_CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    AP_CHECK(info.type == type);
    AP_CHECK(info.value_presence == EXTRACTPDF_FORM_VALUE_PRESENT);
    AP_CHECK(info.value_count == 1);
    value_info.struct_size = sizeof(value_info);
    AP_CHECK(extractpdf_form_field_value_get_info(form, index, 0, &value_info) ==
        EXTRACTPDF_OK);
    AP_CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_OPTION);
    AP_CHECK(value_info.option_index == expected_option);
}

static extractpdf_form_widget_info ap_widget_by_field(
    const extractpdf_form *form,
    const char *name)
{
    size_t field_index = ap_field_index_by_name(form, name);
    size_t count = 0;
    size_t i;
    size_t matches = 0;
    extractpdf_form_widget_info found = {0};

    AP_CHECK(extractpdf_form_widget_count(form, &count) == EXTRACTPDF_OK);
    for (i = 0; i < count; ++i) {
        extractpdf_form_widget_info info = {0};
        info.struct_size = sizeof(info);
        AP_CHECK(extractpdf_form_widget_get_info(form, i, &info) == EXTRACTPDF_OK);
        if (info.field_index != field_index)
            continue;
        found = info;
        ++matches;
    }
    AP_CHECK(matches == 1);
    return found;
}

static extractpdf_form_widget_info ap_widget_from_path(
    const char *path,
    const char *name)
{
    extractpdf_document *document = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_widget_info info;

    AP_CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    info = ap_widget_by_field(form, name);
    extractpdf_drop_form(form);
    extractpdf_close(document);
    return info;
}

static void ap_render_clip(
    const char *path,
    const extractpdf_form_widget_info *widget,
    unsigned char **out_data,
    size_t *out_size)
{
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_bitmap *bitmap = NULL;
    extractpdf_render_options options = {0};
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    AP_CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_load_page(document, widget->page_index, &page) == EXTRACTPDF_OK);
    options.struct_size = sizeof(options);
    options.dpi = 72.0f;
    options.rotation_degrees = 0.0f;
    options.clip_enabled = 1;
    options.clip = widget->bounds;
    options.alpha = 0;
    AP_CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) ==
        EXTRACTPDF_OK);
    AP_CHECK(extractpdf_bitmap_data(bitmap, &data, &size) == EXTRACTPDF_OK);
    AP_CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    AP_CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
    extractpdf_drop_bitmap(bitmap);
    extractpdf_drop_page(page);
    extractpdf_close(document);
}

static void ap_copy_output(
    const extractpdf_output *output,
    unsigned char **out_data,
    size_t *out_size)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    AP_CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    AP_CHECK(data != NULL && size != 0);
    *out_data = (unsigned char *)malloc(size);
    AP_CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
}

static size_t ap_count_bytes(
    const unsigned char *data,
    size_t size,
    const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t i;
    size_t count = 0;

    for (i = 0; needle_size != 0 && i + needle_size <= size; ++i)
        if (memcmp(data + i, needle, needle_size) == 0)
            ++count;
    return count;
}

static void ap_make_text_update(
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

static void ap_make_option_update(
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

static void ap_check_reopened_text(const char *path)
{
    extractpdf_document *document = NULL;
    extractpdf_form *form = NULL;

    AP_CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    ap_expect_text(form, "target", "AFTER");
    ap_expect_text(form, "unrelated", "UNCHANGED");
    ap_expect_text(form, "calc", "CALC-SENTINEL");
    extractpdf_drop_form(form);
    extractpdf_close(document);
}

static void test_text_target_refresh_and_no_execution(void)
{
    extractpdf_form_widget_info target_widget =
        ap_widget_from_path(FORM_MUTATION_EVENTS_PDF, "target");
    extractpdf_form_widget_info unrelated_widget =
        ap_widget_from_path(FORM_MUTATION_EVENTS_PDF, "unrelated");
    extractpdf_pdf_edit *edit = ap_open_edit(FORM_MUTATION_EVENTS_PDF);
    extractpdf_form_field_ref ref = ap_field_ref_by_name(edit, "target");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_form *form = NULL;
    extractpdf_output *historical = NULL;
    extractpdf_output *after_a = NULL;
    extractpdf_output *after_b = NULL;
    const unsigned char *historical_now = NULL;
    size_t historical_now_size = 0;
    unsigned char *historical_bytes = NULL;
    size_t historical_size = 0;
    unsigned char *after_bytes = NULL;
    size_t after_size = 0;
    unsigned char *target_before = NULL;
    unsigned char *target_after = NULL;
    unsigned char *unrelated_before = NULL;
    unsigned char *unrelated_after = NULL;
    size_t target_before_size = 0;
    size_t target_after_size = 0;
    size_t unrelated_before_size = 0;
    size_t unrelated_after_size = 0;

    remove(FORM_AP_OUTPUT_A);
    remove(FORM_AP_OUTPUT_B);
    ap_render_clip(FORM_MUTATION_EVENTS_PDF, &target_widget,
        &target_before, &target_before_size);
    ap_render_clip(FORM_MUTATION_EVENTS_PDF, &unrelated_widget,
        &unrelated_before, &unrelated_before_size);

    AP_CHECK(extractpdf_pdf_edit_snapshot(edit, &historical) == EXTRACTPDF_OK);
    ap_copy_output(historical, &historical_bytes, &historical_size);
    ap_make_text_update(&value, &update, "AFTER");
    AP_CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) == EXTRACTPDF_OK);

    AP_CHECK(extractpdf_output_data(historical,
        &historical_now, &historical_now_size) == EXTRACTPDF_OK);
    AP_CHECK(historical_now_size == historical_size);
    AP_CHECK(memcmp(historical_now, historical_bytes, historical_size) == 0);

    AP_CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    ap_expect_text(form, "target", "AFTER");
    ap_expect_text(form, "unrelated", "UNCHANGED");
    ap_expect_text(form, "calc", "CALC-SENTINEL");
    extractpdf_drop_form(form);
    form = NULL;

    AP_CHECK(extractpdf_pdf_edit_snapshot(edit, &after_a) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_pdf_edit_snapshot(edit, &after_b) == EXTRACTPDF_OK);
    ap_copy_output(after_a, &after_bytes, &after_size);
    AP_CHECK(ap_count_bytes(after_bytes, after_size, "UNRELATED-AP-KEEP") == 1);
    AP_CHECK(ap_count_bytes(after_bytes, after_size, "CALC-AP-KEEP") == 1);
    AP_CHECK(extractpdf_output_save_file(after_a, FORM_AP_OUTPUT_A) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_output_save_file(after_b, FORM_AP_OUTPUT_B) == EXTRACTPDF_OK);
    ap_check_reopened_text(FORM_AP_OUTPUT_A);
    ap_check_reopened_text(FORM_AP_OUTPUT_B);

    ap_render_clip(FORM_AP_OUTPUT_A, &target_widget,
        &target_after, &target_after_size);
    ap_render_clip(FORM_AP_OUTPUT_A, &unrelated_widget,
        &unrelated_after, &unrelated_after_size);
    AP_CHECK(target_before_size == target_after_size);
    AP_CHECK(memcmp(target_before, target_after, target_before_size) != 0);
    AP_CHECK(unrelated_before_size == unrelated_after_size);
    AP_CHECK(memcmp(unrelated_before, unrelated_after, unrelated_before_size) == 0);

    free(historical_bytes);
    free(after_bytes);
    free(target_before);
    free(target_after);
    free(unrelated_before);
    free(unrelated_after);
    extractpdf_drop_output(historical);
    extractpdf_drop_output(after_a);
    extractpdf_drop_output(after_b);
    extractpdf_drop_pdf_edit(edit);
    remove(FORM_AP_OUTPUT_A);
    remove(FORM_AP_OUTPUT_B);
}

static void test_choice_target_refresh(void)
{
    extractpdf_form_widget_info combo_widget =
        ap_widget_from_path(FORM_MUTATION_CHOICE_PDF, "combo");
    extractpdf_pdf_edit *edit = ap_open_edit(FORM_MUTATION_CHOICE_PDF);
    extractpdf_form_field_ref combo = ap_field_ref_by_name(edit, "combo");
    extractpdf_form_field_ref single = ap_field_ref_by_name(edit, "single");
    extractpdf_form_value_input value;
    extractpdf_form_value_update update;
    extractpdf_output *output = NULL;
    extractpdf_document *document = NULL;
    extractpdf_form *form = NULL;
    extractpdf_page *page = NULL;
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;

    remove(FORM_AP_OUTPUT_CHOICE);
    ap_render_clip(FORM_MUTATION_CHOICE_PDF, &combo_widget, &before, &before_size);
    ap_make_option_update(&value, &update, 0);
    AP_CHECK(extractpdf_pdf_edit_form_set_values(edit, &combo, &update) == EXTRACTPDF_OK);
    ap_make_option_update(&value, &update, 2);
    AP_CHECK(extractpdf_pdf_edit_form_set_values(edit, &single, &update) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_pdf_edit_snapshot(edit, &output) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_output_save_file(output, FORM_AP_OUTPUT_CHOICE) == EXTRACTPDF_OK);

    AP_CHECK(extractpdf_open(FORM_AP_OUTPUT_CHOICE, NULL, &document) == EXTRACTPDF_OK);
    AP_CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    ap_expect_choice_option(form, "combo", EXTRACTPDF_FORM_FIELD_COMBO_BOX, 0);
    ap_expect_choice_option(form, "single", EXTRACTPDF_FORM_FIELD_LIST_BOX, 2);
    extractpdf_drop_form(form);
    form = NULL;
    AP_CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    extractpdf_close(document);
    document = NULL;

    ap_render_clip(FORM_AP_OUTPUT_CHOICE, &combo_widget, &after, &after_size);
    AP_CHECK(before_size == after_size);
    AP_CHECK(memcmp(before, after, before_size) != 0);

    free(before);
    free(after);
    extractpdf_drop_output(output);
    extractpdf_drop_pdf_edit(edit);
    remove(FORM_AP_OUTPUT_CHOICE);
}

int extractpdf_pdf_form_appearance_main(void)
{
    test_text_target_refresh_and_no_execution();
    test_choice_target_refresh();
    return EXIT_SUCCESS;
}
