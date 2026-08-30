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

static extractpdf_output *split_page_zero(
    extractpdf_document *document,
    extractpdf_status expected)
{
    extractpdf_page_poster_split split;
    extractpdf_output *output = NULL;

    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = 2;
    split.rows = 2;
    CHECK(extractpdf_poster_split_pages(document, &split, 1, &output) == expected);
    if (expected == EXTRACTPDF_OK)
        CHECK(output != NULL);
    else
        CHECK(output == NULL);
    return output;
}

static void expect_malformed_destination(void)
{
    extractpdf_document *document = open_document(POSTER_MALFORMED_DESTINATION_PDF);
    extractpdf_output *output = split_page_zero(document, EXTRACTPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    extractpdf_close(document);
}

static void expect_internal_link(
    const extractpdf_link_page *links,
    size_t index,
    int target_page,
    float target_x,
    float target_y)
{
    extractpdf_link_info info;

    info.struct_size = sizeof(info);
    CHECK(extractpdf_link_get_info(links, index, &info) == EXTRACTPDF_OK);
    CHECK(info.kind == EXTRACTPDF_LINK_INTERNAL);
    CHECK(info.target_page == target_page);
    CHECK(close_float(info.target.x, target_x));
    CHECK(close_float(info.target.y, target_y));
}

static void expect_primary_navigation(void)
{
    extractpdf_document *source = open_document(POSTER_NAVIGATION_PDF);
    extractpdf_output *output = split_page_zero(source, EXTRACTPDF_OK);
    const unsigned char *data = NULL;
    size_t size = 0;
    extractpdf_document *reopened;
    extractpdf_page *page = NULL;
    extractpdf_link_page *links = NULL;
    extractpdf_outline *outline = NULL;
    extractpdf_outline_info info;
    size_t count = 0;

    CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    CHECK(data != NULL && size != 0);
    CHECK(poster_raw_check_navigation(data, size));
    CHECK(extractpdf_output_save_file(output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    reopened = open_document(POSTER_OUTPUT_PDF);

    CHECK(extractpdf_load_page(reopened, 4, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == 5);
    expect_internal_link(links, 0, 0, 50.0f, 75.0f);
    expect_internal_link(links, 1, 1, 50.0f, 75.0f);
    expect_internal_link(links, 2, 2, 50.0f, 75.0f);
    expect_internal_link(links, 3, 0, 50.0f, 75.0f);
    expect_internal_link(links, 4, 1, 0.0f, 75.0f);

    CHECK(extractpdf_document_outline(reopened, &outline) == EXTRACTPDF_OK);
    CHECK(outline != NULL);
    CHECK(extractpdf_outline_count(outline, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);
    info.struct_size = sizeof(info);
    CHECK(extractpdf_outline_get_info(outline, 0, &info) == EXTRACTPDF_OK);
    CHECK(info.destination_kind == EXTRACTPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 2);
    CHECK(close_float(info.target.x, 50.0f));
    CHECK(close_float(info.target.y, 75.0f));
    info.struct_size = sizeof(info);
    CHECK(extractpdf_outline_get_info(outline, 1, &info) == EXTRACTPDF_OK);
    CHECK(info.destination_kind == EXTRACTPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 3);
    CHECK(close_float(info.target.x, 50.0f));
    CHECK(close_float(info.target.y, 75.0f));

    extractpdf_drop_outline(outline);
    extractpdf_drop_link_page(links);
    extractpdf_drop_page(page);
    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_close(source);
}

static void expect_transform_navigation(const char *path)
{
    extractpdf_document *source = open_document(path);
    extractpdf_page *source_page = NULL;
    extractpdf_page *link_page = NULL;
    extractpdf_link_page *source_links = NULL;
    extractpdf_link_info source_info;
    extractpdf_rect source_bounds;
    extractpdf_output *output;
    extractpdf_document *reopened;
    extractpdf_page *output_link_page = NULL;
    extractpdf_link_page *output_links = NULL;
    extractpdf_link_info output_info;
    float mid_x;
    float mid_y;
    float tile_x0;
    float tile_y0;
    size_t row;
    size_t column;
    int expected_page;

    CHECK(extractpdf_load_page(source, 0, &source_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_bounds(source_page, &source_bounds) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(source, 1, &link_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(link_page, &source_links) == EXTRACTPDF_OK);
    source_info.struct_size = sizeof(source_info);
    CHECK(extractpdf_link_get_info(source_links, 0, &source_info) == EXTRACTPDF_OK);
    CHECK(source_info.kind == EXTRACTPDF_LINK_INTERNAL);
    CHECK(source_info.target_page == 0);

    mid_x = (source_bounds.x0 + source_bounds.x1) * 0.5f;
    mid_y = (source_bounds.y0 + source_bounds.y1) * 0.5f;
    column = source_info.target.x >= mid_x ? 1u : 0u;
    row = source_info.target.y >= mid_y ? 1u : 0u;
    expected_page = (int)(row * 2u + column);
    tile_x0 = column != 0 ? mid_x : source_bounds.x0;
    tile_y0 = row != 0 ? mid_y : source_bounds.y0;

    output = split_page_zero(source, EXTRACTPDF_OK);
    CHECK(extractpdf_output_save_file(output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    reopened = open_document(POSTER_OUTPUT_PDF);
    CHECK(extractpdf_load_page(reopened, 4, &output_link_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(output_link_page, &output_links) == EXTRACTPDF_OK);
    output_info.struct_size = sizeof(output_info);
    CHECK(extractpdf_link_get_info(output_links, 0, &output_info) == EXTRACTPDF_OK);
    CHECK(output_info.kind == EXTRACTPDF_LINK_INTERNAL);
    CHECK(output_info.target_page == expected_page);
    CHECK(close_float(output_info.target.x, source_info.target.x - tile_x0));
    CHECK(close_float(output_info.target.y, source_info.target.y - tile_y0));

    extractpdf_drop_link_page(output_links);
    extractpdf_drop_page(output_link_page);
    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_drop_link_page(source_links);
    extractpdf_drop_page(link_page);
    extractpdf_drop_page(source_page);
    extractpdf_close(source);
}

int poster_run_navigation_tests(void)
{
    expect_malformed_destination();
    expect_primary_navigation();
    expect_transform_navigation(POSTER_NAVIGATION_ROTATE_90_PDF);
    expect_transform_navigation(POSTER_NAVIGATION_USERUNIT_PDF);
    return 0;
}
