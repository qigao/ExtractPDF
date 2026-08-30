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

static int close_float(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_source_form_valid(extractpdf_document *source)
{
    extractpdf_form *form = NULL;
    extractpdf_status status = extractpdf_document_form(source, &form);
    if (status != EXTRACTPDF_OK)
        fprintf(stderr, "source form status: %s (%d)\n",
            extractpdf_status_string(status), (int)status);
    CHECK(status == EXTRACTPDF_OK);
    CHECK(form != NULL);
    extractpdf_drop_form(form);
}

static extractpdf_output *split_interactive(extractpdf_document *source)
{
    extractpdf_page_poster_split split;
    extractpdf_output *output = NULL;
    extractpdf_status status;
    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = 2;
    split.rows = 2;
    status = extractpdf_poster_split_pages(source, &split, 1, &output);
    if (status != EXTRACTPDF_OK)
        fprintf(stderr, "interactive poster split status: %s (%d)\n",
            extractpdf_status_string(status), (int)status);
    CHECK(status == EXTRACTPDF_OK);
    CHECK(output != NULL);
    CHECK(extractpdf_output_save_file(output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    return output;
}

static void expect_uri_link(
    extractpdf_document *document,
    int page_index,
    size_t expected_count,
    const char *uri,
    extractpdf_rect expected_rect)
{
    extractpdf_page *page = NULL;
    extractpdf_link_page *links = NULL;
    size_t count = 0;
    size_t index;
    int found = 0;

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == expected_count);
    for (index = 0; index < count; ++index) {
        extractpdf_link_info info;
        const char *actual_uri = NULL;
        size_t uri_size = 0;
        info.struct_size = sizeof(info);
        CHECK(extractpdf_link_get_info(links, index, &info) == EXTRACTPDF_OK);
        if (info.kind != EXTRACTPDF_LINK_URI)
            continue;
        CHECK(extractpdf_link_uri(links, index, &actual_uri, &uri_size) == EXTRACTPDF_OK);
        if (strlen(uri) == uri_size && memcmp(actual_uri, uri, uri_size) == 0) {
            CHECK(close_float(info.hotspot.x0, expected_rect.x0));
            CHECK(close_float(info.hotspot.y0, expected_rect.y0));
            CHECK(close_float(info.hotspot.x1, expected_rect.x1));
            CHECK(close_float(info.hotspot.y1, expected_rect.y1));
            found = 1;
        }
    }
    CHECK(found);
    extractpdf_drop_link_page(links);
    extractpdf_drop_page(page);
}

static void expect_no_links(extractpdf_document *document, int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_link_page *links = NULL;
    size_t count = 1;
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == 0);
    extractpdf_drop_link_page(links);
    extractpdf_drop_page(page);
}

static void expect_square(extractpdf_document *document)
{
    extractpdf_page *page = NULL;
    extractpdf_annotation_page *annotations = NULL;
    extractpdf_annotation_info info;
    const char *contents = NULL;
    size_t contents_size = 0;
    size_t count = 0;

    CHECK(extractpdf_load_page(document, 2, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_annotations(page, &annotations) == EXTRACTPDF_OK);
    CHECK(extractpdf_annotation_count(annotations, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    info.struct_size = sizeof(info);
    CHECK(extractpdf_annotation_get_info(annotations, 0, &info) == EXTRACTPDF_OK);
    CHECK(info.type == EXTRACTPDF_ANNOTATION_SQUARE);
    CHECK(close_float(info.bounds.x0, 30.0f));
    CHECK(close_float(info.bounds.y0, 60.0f));
    CHECK(close_float(info.bounds.x1, 80.0f));
    CHECK(close_float(info.bounds.y1, 110.0f));
    CHECK(extractpdf_annotation_contents(
              annotations, 0, &contents, &contents_size) == EXTRACTPDF_OK);
    CHECK(contents_size == strlen("POSTER-ANNOT"));
    CHECK(memcmp(contents, "POSTER-ANNOT", contents_size) == 0);
    extractpdf_drop_annotation_page(annotations);
    extractpdf_drop_page(page);
}

static void expect_widget(extractpdf_document *document)
{
    extractpdf_form *form = NULL;
    extractpdf_form_widget_info widget;
    extractpdf_form_field_info field;
    const char *name = NULL;
    const char *value = NULL;
    size_t name_size = 0;
    size_t value_size = 0;
    size_t field_count = 0;
    size_t widget_count = 0;
    extractpdf_form_value_info value_info;

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count >= 1);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == 1);

    widget.struct_size = sizeof(widget);
    CHECK(extractpdf_form_widget_get_info(form, 0, &widget) == EXTRACTPDF_OK);
    CHECK(widget.page_index == 3);
    CHECK(close_float(widget.bounds.x0, 50.0f));
    CHECK(close_float(widget.bounds.y0, 70.0f));
    CHECK(close_float(widget.bounds.x1, 150.0f));
    CHECK(close_float(widget.bounds.y1, 110.0f));

    field.struct_size = sizeof(field);
    CHECK(extractpdf_form_field_get_info(form, widget.field_index, &field) == EXTRACTPDF_OK);
    CHECK(field.widget_count == 1);
    CHECK(field.value_count == 1);
    CHECK(extractpdf_form_field_name(
              form, widget.field_index, &name, &name_size) == EXTRACTPDF_OK);
    CHECK(name_size == strlen("poster.text"));
    CHECK(memcmp(name, "poster.text", name_size) == 0);
    value_info.struct_size = sizeof(value_info);
    CHECK(extractpdf_form_field_value_get_info(
              form, widget.field_index, 0, &value_info) == EXTRACTPDF_OK);
    CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
    CHECK(extractpdf_form_field_value_utf8(
              form, widget.field_index, 0, &value, &value_size) == EXTRACTPDF_OK);
    CHECK(value_size == strlen("POSTER-VALUE"));
    CHECK(memcmp(value, "POSTER-VALUE", value_size) == 0);

    extractpdf_drop_form(form);
}

int poster_run_interactive_tests(void)
{
    extractpdf_document *source = open_document(POSTER_INTERACTIVE_PDF);
    extractpdf_output *output;
    extractpdf_document *reopened;
    int count = 0;

    expect_source_form_valid(source);
    output = split_interactive(source);
    reopened = open_document(POSTER_OUTPUT_PDF);

    CHECK(extractpdf_page_count(reopened, &count) == EXTRACTPDF_OK);
    CHECK(count == 5);

    expect_uri_link(
        reopened, 0, 2, "https://example.com/contained",
        (extractpdf_rect){20.0f, 40.0f, 120.0f, 80.0f});
    expect_uri_link(
        reopened, 0, 2, "https://example.com/crossing",
        (extractpdf_rect){150.0f, 40.0f, 200.0f, 90.0f});
    expect_uri_link(
        reopened, 1, 1, "https://example.com/crossing",
        (extractpdf_rect){0.0f, 40.0f, 50.0f, 90.0f});
    expect_no_links(reopened, 2);
    expect_no_links(reopened, 3);

    expect_square(reopened);
    expect_widget(reopened);

    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_close(source);
    return 0;
}
