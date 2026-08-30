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

static extractpdf_rect page_bounds(
    extractpdf_document *document,
    int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static void expect_bounds(
    extractpdf_document *document,
    int page_index,
    float width,
    float height)
{
    extractpdf_rect bounds = page_bounds(document, page_index);
    CHECK(close_float(bounds.x0, 0.0f));
    CHECK(close_float(bounds.y0, 0.0f));
    CHECK(close_float(bounds.x1, width));
    CHECK(close_float(bounds.y1, height));
}

static void expect_text(extractpdf_document *document, int page_index, const char *needle)
{
    extractpdf_page *page = NULL;
    char *text = NULL;
    size_t size = 0;
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_text(page, &text, &size) == EXTRACTPDF_OK);
    CHECK(text != NULL && size != 0);
    CHECK(strstr(text, needle) != NULL);
    extractpdf_free(text);
    extractpdf_drop_page(page);
}

static void compare_render(
    extractpdf_document *source,
    int source_page_index,
    extractpdf_rect source_clip,
    extractpdf_document *output,
    int output_page_index)
{
    extractpdf_page *source_page = NULL;
    extractpdf_page *output_page = NULL;
    extractpdf_bitmap *source_bitmap = NULL;
    extractpdf_bitmap *output_bitmap = NULL;
    extractpdf_render_options source_options = {0};
    extractpdf_render_options output_options = {0};
    const unsigned char *source_data = NULL;
    const unsigned char *output_data = NULL;
    size_t source_size = 0;
    size_t output_size = 0;
    int sw, sh, ss, sc;
    int ow, oh, os, oc;

    source_options.struct_size = sizeof(source_options);
    source_options.dpi = 72.0f;
    source_options.rotation_degrees = 0.0f;
    source_options.clip_enabled = 1;
    source_options.clip = source_clip;
    source_options.alpha = 0;
    output_options = source_options;
    output_options.clip_enabled = 0;

    CHECK(extractpdf_load_page(source, source_page_index, &source_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(output, output_page_index, &output_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_render_page_with_options(
              source_page, &source_options, &source_bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_render_page_with_options(
              output_page, &output_options, &output_bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(source_bitmap, &sw, &sh, &ss, &sc) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(output_bitmap, &ow, &oh, &os, &oc) == EXTRACTPDF_OK);
    CHECK(sw == ow && sh == oh && ss == os && sc == oc);
    CHECK(extractpdf_bitmap_data(source_bitmap, &source_data, &source_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_data(output_bitmap, &output_data, &output_size) == EXTRACTPDF_OK);
    CHECK(source_size == output_size);
    CHECK(source_size != 0);
    CHECK(memcmp(source_data, output_data, source_size) == 0);

    extractpdf_drop_bitmap(output_bitmap);
    extractpdf_drop_bitmap(source_bitmap);
    extractpdf_drop_page(output_page);
    extractpdf_drop_page(source_page);
}

static extractpdf_output *split_to_file(
    extractpdf_document *source,
    int page_index,
    size_t columns,
    size_t rows)
{
    extractpdf_page_poster_split split;
    extractpdf_output *output = NULL;

    split.struct_size = sizeof(split);
    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    CHECK(extractpdf_poster_split_pages(source, &split, 1, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    CHECK(extractpdf_output_save_file(output, POSTER_OUTPUT_PDF) == EXTRACTPDF_OK);
    return output;
}

static void test_basic(void)
{
    static const extractpdf_rect clips[4] = {
        {0.0f, 0.0f, 200.0f, 150.0f},
        {200.0f, 0.0f, 400.0f, 150.0f},
        {0.0f, 150.0f, 200.0f, 300.0f},
        {200.0f, 150.0f, 400.0f, 300.0f}
    };
    static const float raw_boxes[4][4] = {
        {0.0f, 150.0f, 200.0f, 300.0f},
        {200.0f, 150.0f, 400.0f, 300.0f},
        {0.0f, 0.0f, 200.0f, 150.0f},
        {200.0f, 0.0f, 400.0f, 150.0f}
    };
    extractpdf_document *source = open_document(POSTER_BASIC_PDF);
    extractpdf_document *reopened;
    extractpdf_output *output = split_to_file(source, 1, 2, 2);
    const unsigned char *data = NULL;
    size_t size = 0;
    int count = 0;
    int tile;

    reopened = open_document(POSTER_OUTPUT_PDF);
    CHECK(extractpdf_page_count(reopened, &count) == EXTRACTPDF_OK);
    CHECK(count == 6);
    expect_text(reopened, 0, "POSTER-BEFORE");
    expect_text(reopened, 5, "POSTER-AFTER");
    for (tile = 0; tile < 4; ++tile) {
        expect_bounds(reopened, tile + 1, 200.0f, 150.0f);
        compare_render(source, 1, clips[tile], reopened, tile + 1);
    }

    CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    CHECK(poster_raw_check_basic_tiles(
        data, size, 1, 4, raw_boxes, 0, 1.0f));

    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_close(source);
}

static void test_rotate_90(void)
{
    extractpdf_document *source = open_document(POSTER_ROTATE_90_PDF);
    extractpdf_output *output = split_to_file(source, 0, 2, 2);
    extractpdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
    extractpdf_rect source_bounds = page_bounds(source, 0);
    extractpdf_rect clip = {
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x0 + (source_bounds.x1 - source_bounds.x0) / 2.0f,
        source_bounds.y0 + (source_bounds.y1 - source_bounds.y0) / 2.0f
    };
    int count = 0;

    CHECK(extractpdf_page_count(reopened, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);
    expect_bounds(reopened, 0, 150.0f, 200.0f);
    compare_render(source, 0, clip, reopened, 0);

    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_close(source);
}

static void test_userunit(void)
{
    extractpdf_document *source = open_document(POSTER_USERUNIT_PDF);
    extractpdf_output *output = split_to_file(source, 0, 2, 2);
    extractpdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
    extractpdf_rect source_bounds = page_bounds(source, 0);
    extractpdf_rect clip = {
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x0 + (source_bounds.x1 - source_bounds.x0) / 2.0f,
        source_bounds.y0 + (source_bounds.y1 - source_bounds.y0) / 2.0f
    };
    int count = 0;

    CHECK(extractpdf_page_count(reopened, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);
    expect_bounds(reopened, 0, 200.0f, 150.0f);
    compare_render(source, 0, clip, reopened, 0);

    extractpdf_close(reopened);
    extractpdf_drop_output(output);
    extractpdf_close(source);
}

int poster_run_geometry_tests(void)
{
    test_basic();
    test_rotate_90();
    test_userunit();
    return 0;
}
