#include "test_pdf_poster_split_internal.h"

#include <extractpdf/extractpdf.h>

#include <math.h>
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

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static extractpdf_page_poster_split make_split(
    int page_index,
    size_t columns,
    size_t rows)
{
    extractpdf_page_poster_split split;
    split.struct_size = sizeof(split);
    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    return split;
}

static extractpdf_rect page_bounds(extractpdf_document *document, int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static char *page_text(
    extractpdf_document *document,
    int page_index,
    size_t *out_size)
{
    extractpdf_page *page = NULL;
    char *text = NULL;

    *out_size = 0;
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_text(page, &text, out_size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    extractpdf_drop_page(page);
    return text;
}

static int bytes_contain(
    const char *bytes,
    size_t size,
    const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t index;

    if (needle_size > size)
        return 0;
    for (index = 0; index + needle_size <= size; ++index) {
        if (memcmp(bytes + index, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static void check_bounds(
    extractpdf_document *document,
    int page_index,
    float width,
    float height)
{
    extractpdf_rect bounds = page_bounds(document, page_index);
    CHECK(close_float(bounds.x0, 0.0f));
    CHECK(close_float(bounds.y0, 0.0f));
    CHECK(close_float(bounds.x1 - bounds.x0, width));
    CHECK(close_float(bounds.y1 - bounds.y0, height));
}

static void compare_output_bytes(
    extractpdf_output *left,
    extractpdf_output *right)
{
    const unsigned char *left_data = NULL;
    const unsigned char *right_data = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(extractpdf_output_data(left, &left_data, &left_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(right, &right_data, &right_size) == EXTRACTPDF_OK);
    CHECK(left_data != NULL && right_data != NULL);
    CHECK(left_size != 0 && left_size == right_size);
    CHECK(memcmp(left_data, right_data, left_size) == 0);
}

static void test_batch_order_and_determinism(void)
{
    extractpdf_document *source = open_document(POSTER_BASIC_PDF);
    extractpdf_page_poster_split forward[2];
    extractpdf_page_poster_split reverse[2];
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    extractpdf_output *third = NULL;
    int source_count = 0;
    extractpdf_rect before0;
    extractpdf_rect before1;
    extractpdf_rect before2;
    char *before_text = NULL;
    char *after_text = NULL;
    size_t before_text_size = 0;
    size_t after_text_size = 0;

    forward[0] = make_split(0, 2, 1);
    forward[1] = make_split(1, 1, 2);
    reverse[0] = forward[1];
    reverse[1] = forward[0];

    CHECK(extractpdf_page_count(source, &source_count) == EXTRACTPDF_OK);
    CHECK(source_count == 3);
    before0 = page_bounds(source, 0);
    before1 = page_bounds(source, 1);
    before2 = page_bounds(source, 2);
    before_text = page_text(source, 1, &before_text_size);

    CHECK(extractpdf_poster_split_pages(source, forward, 2, &first) == EXTRACTPDF_OK);
    CHECK(extractpdf_poster_split_pages(source, reverse, 2, &second) == EXTRACTPDF_OK);
    CHECK(extractpdf_poster_split_pages(source, forward, 2, &third) == EXTRACTPDF_OK);
    CHECK(first != NULL && second != NULL && third != NULL);
    compare_output_bytes(first, second);
    compare_output_bytes(first, third);

    CHECK(extractpdf_output_save_file(first, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    {
        extractpdf_document *output = open_document(POSTER_OUTPUT_PDF);
        int count = 0;
        CHECK(extractpdf_page_count(output, &count) == EXTRACTPDF_OK);
        CHECK(count == 5);
        check_bounds(output, 0, 100.0f, 200.0f);
        check_bounds(output, 1, 100.0f, 200.0f);
        check_bounds(output, 2, 400.0f, 150.0f);
        check_bounds(output, 3, 400.0f, 150.0f);
        check_bounds(output, 4, 200.0f, 200.0f);
        extractpdf_close(output);
    }

    CHECK(extractpdf_page_count(source, &source_count) == EXTRACTPDF_OK);
    CHECK(source_count == 3);
    {
        extractpdf_rect after0 = page_bounds(source, 0);
        extractpdf_rect after1 = page_bounds(source, 1);
        extractpdf_rect after2 = page_bounds(source, 2);
        CHECK(memcmp(&before0, &after0, sizeof(before0)) == 0);
        CHECK(memcmp(&before1, &after1, sizeof(before1)) == 0);
        CHECK(memcmp(&before2, &after2, sizeof(before2)) == 0);
    }
    after_text = page_text(source, 1, &after_text_size);
    CHECK(before_text_size == after_text_size);
    CHECK(memcmp(before_text, after_text, before_text_size) == 0);

    extractpdf_free(after_text);
    extractpdf_free(before_text);
    extractpdf_drop_output(third);
    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(source);
}

static void test_duplicate_rejection_order_independent(void)
{
    extractpdf_document *source = open_document(POSTER_BASIC_PDF);
    extractpdf_page_poster_split pair[2];
    extractpdf_output *output = NULL;

    pair[0] = make_split(1, 2, 1);
    pair[1] = make_split(1, 1, 2);
    CHECK(extractpdf_poster_split_pages(source, pair, 2, &output) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);

    {
        extractpdf_page_poster_split temp = pair[0];
        pair[0] = pair[1];
        pair[1] = temp;
    }
    CHECK(extractpdf_poster_split_pages(source, pair, 2, &output) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    extractpdf_close(source);
}

static void expect_atomic_failure(
    const char *path,
    extractpdf_status expected)
{
    extractpdf_document *source = open_document(path);
    extractpdf_page_poster_split splits[2];
    extractpdf_output *output = NULL;
    extractpdf_rect before0 = page_bounds(source, 0);
    extractpdf_rect before1 = page_bounds(source, 1);
    int before_count = 0;
    int after_count = 0;

    CHECK(extractpdf_page_count(source, &before_count) == EXTRACTPDF_OK);
    CHECK(before_count == 2);
    splits[0] = make_split(0, 2, 1);
    splits[1] = make_split(1, 2, 1);
    CHECK(extractpdf_poster_split_pages(source, splits, 2, &output) == expected);
    CHECK(output == NULL);
    CHECK(extractpdf_page_count(source, &after_count) == EXTRACTPDF_OK);
    CHECK(after_count == before_count);
    {
        extractpdf_rect after0 = page_bounds(source, 0);
        extractpdf_rect after1 = page_bounds(source, 1);
        CHECK(memcmp(&before0, &after0, sizeof(before0)) == 0);
        CHECK(memcmp(&before1, &after1, sizeof(before1)) == 0);
    }
    extractpdf_close(source);
}

static void test_failure_atomicity(void)
{
    expect_atomic_failure(
        POSTER_UNSELECTED_ACTIONS_PDF,
        EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_atomic_failure(
        POSTER_MALFORMED_ANNOTS_PDF,
        EXTRACTPDF_ERROR_FORMAT);
}

static void test_mixed_noop_and_real_split_policy(void)
{
    extractpdf_document *source = open_document(POSTER_UNSELECTED_ACTIONS_PDF);
    extractpdf_page_poster_split mixed[2];
    extractpdf_page_poster_split noop[2];
    extractpdf_output *output = NULL;

    mixed[0] = make_split(0, 2, 1);
    mixed[1] = make_split(1, 1, 1);
    CHECK(extractpdf_poster_split_pages(source, mixed, 2, &output) ==
          EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);

    noop[0] = make_split(0, 1, 1);
    noop[1] = make_split(1, 1, 1);
    CHECK(extractpdf_poster_split_pages(source, noop, 2, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    extractpdf_drop_output(output);
    extractpdf_close(source);
}

static void check_interactive_source(extractpdf_document *source)
{
    extractpdf_page *page = NULL;
    extractpdf_link_page *links = NULL;
    extractpdf_annotation_page *annotations = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_widget_info widget;
    const char *value = NULL;
    size_t value_size = 0;
    size_t count = 0;
    int page_count = 0;

    CHECK(extractpdf_page_count(source, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    CHECK(extractpdf_load_page(source, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);
    CHECK(extractpdf_extract_annotations(page, &annotations) == EXTRACTPDF_OK);
    CHECK(extractpdf_annotation_count(annotations, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_document_form(source, &form) == EXTRACTPDF_OK);
    CHECK(extractpdf_form_widget_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    widget.struct_size = sizeof(widget);
    CHECK(extractpdf_form_widget_get_info(form, 0, &widget) == EXTRACTPDF_OK);
    CHECK(widget.page_index == 0);
    CHECK(extractpdf_form_field_value_utf8(
              form, widget.field_index, 0, &value, &value_size) == EXTRACTPDF_OK);
    CHECK(value_size == strlen("POSTER-VALUE"));
    CHECK(memcmp(value, "POSTER-VALUE", value_size) == 0);

    extractpdf_drop_form(form);
    extractpdf_drop_annotation_page(annotations);
    extractpdf_drop_link_page(links);
    extractpdf_drop_page(page);
}

static void check_navigation_source(extractpdf_document *source)
{
    extractpdf_outline *outline = NULL;
    extractpdf_outline_info info;
    size_t count = 0;
    int page_count = 0;

    CHECK(extractpdf_page_count(source, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    CHECK(extractpdf_document_outline(source, &outline) == EXTRACTPDF_OK);
    CHECK(outline != NULL);
    CHECK(extractpdf_outline_count(outline, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);
    info.struct_size = sizeof(info);
    CHECK(extractpdf_outline_get_info(outline, 0, &info) == EXTRACTPDF_OK);
    CHECK(info.destination_kind == EXTRACTPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 0);
    extractpdf_drop_outline(outline);
}

static void test_source_immutability_and_output_lifetime(void)
{
    extractpdf_document *interactive = open_document(POSTER_INTERACTIVE_PDF);
    extractpdf_page_poster_split split = make_split(0, 2, 2);
    extractpdf_output *interactive_output = NULL;

    check_interactive_source(interactive);
    CHECK(extractpdf_poster_split_pages(
              interactive, &split, 1, &interactive_output) == EXTRACTPDF_OK);
    CHECK(interactive_output != NULL);
    check_interactive_source(interactive);
    extractpdf_close(interactive);
    interactive = NULL;
    CHECK(extractpdf_output_save_file(
              interactive_output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    {
        extractpdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
        extractpdf_form *form = NULL;
        extractpdf_page *page = NULL;
        extractpdf_annotation_page *annotations = NULL;
        extractpdf_link_page *links = NULL;
        size_t count = 0;
        int page_count = 0;
        CHECK(extractpdf_page_count(reopened, &page_count) == EXTRACTPDF_OK);
        CHECK(page_count == 5);
        CHECK(extractpdf_document_form(reopened, &form) == EXTRACTPDF_OK);
        CHECK(extractpdf_form_widget_count(form, &count) == EXTRACTPDF_OK);
        CHECK(count == 1);
        CHECK(extractpdf_load_page(reopened, 0, &page) == EXTRACTPDF_OK);
        CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
        CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
        CHECK(count == 2);
        extractpdf_drop_link_page(links);
        extractpdf_drop_page(page);
        page = NULL;
        CHECK(extractpdf_load_page(reopened, 2, &page) == EXTRACTPDF_OK);
        CHECK(extractpdf_extract_annotations(page, &annotations) == EXTRACTPDF_OK);
        CHECK(extractpdf_annotation_count(annotations, &count) == EXTRACTPDF_OK);
        CHECK(count == 1);
        extractpdf_drop_annotation_page(annotations);
        extractpdf_drop_page(page);
        extractpdf_drop_form(form);
        extractpdf_close(reopened);
    }
    extractpdf_drop_output(interactive_output);

    {
        extractpdf_document *navigation = open_document(POSTER_NAVIGATION_PDF);
        extractpdf_output *navigation_output = NULL;
        split = make_split(0, 2, 2);
        check_navigation_source(navigation);
        CHECK(extractpdf_poster_split_pages(
                  navigation, &split, 1, &navigation_output) == EXTRACTPDF_OK);
        CHECK(navigation_output != NULL);
        check_navigation_source(navigation);
        extractpdf_close(navigation);
        navigation = NULL;
        CHECK(extractpdf_output_save_file(
                  navigation_output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
        {
            extractpdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
            extractpdf_outline *outline = NULL;
            size_t count = 0;
            CHECK(extractpdf_document_outline(reopened, &outline) == EXTRACTPDF_OK);
            CHECK(extractpdf_outline_count(outline, &count) == EXTRACTPDF_OK);
            CHECK(count == 2);
            extractpdf_drop_outline(outline);
            extractpdf_close(reopened);
        }
        extractpdf_drop_output(navigation_output);
    }

    {
        extractpdf_document *basic = open_document(POSTER_BASIC_PDF);
        extractpdf_output *basic_output = NULL;
        char *text = NULL;
        size_t text_size = 0;
        split = make_split(1, 2, 2);
        CHECK(extractpdf_poster_split_pages(
                  basic, &split, 1, &basic_output) == EXTRACTPDF_OK);
        CHECK(basic_output != NULL);
        extractpdf_close(basic);
        basic = NULL;
        CHECK(extractpdf_output_save_file(
                  basic_output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
        {
            extractpdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
            int page_count = 0;
            CHECK(extractpdf_page_count(reopened, &page_count) == EXTRACTPDF_OK);
            CHECK(page_count == 6);
            text = page_text(reopened, 1, &text_size);
            CHECK(bytes_contain(text, text_size, "POSTER-00"));
            extractpdf_free(text);
            extractpdf_close(reopened);
        }
        extractpdf_drop_output(basic_output);
    }
}

int poster_run_batch_tests(void)
{
    test_batch_order_and_determinism();
    test_duplicate_rejection_order_independent();
    test_failure_atomicity();
    test_mixed_noop_and_real_split_policy();
    test_source_immutability_and_output_lifetime();
    return 0;
}
