#include <extractpdf/extractpdf.h>
#include "test_pdf_crop_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int extractpdf_pdf_crop_base_main(void);

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

static extractpdf_page_crop make_crop(
    int page_index,
    float x0,
    float y0,
    float x1,
    float y1)
{
    extractpdf_page_crop crop;

    crop.struct_size = sizeof(crop);
    crop.page_index = page_index;
    crop.bounds.x0 = x0;
    crop.bounds.y0 = y0;
    crop.bounds.x1 = x1;
    crop.bounds.y1 = y1;
    return crop;
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static extractpdf_rect page_bounds(extractpdf_document *document)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static void check_rect_close(extractpdf_rect actual, extractpdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void run_transformed_case(
    const char *path,
    float inset_x,
    float inset_y,
    const float expected_raw[4])
{
    extractpdf_document *document = open_document(path);
    extractpdf_document *reopened = NULL;
    extractpdf_output *baseline = NULL;
    extractpdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    extractpdf_rect source_bounds = page_bounds(document);
    extractpdf_rect source_after;
    extractpdf_rect output_bounds;
    extractpdf_page_crop full;
    extractpdf_page_crop crop;

    CHECK(source_bounds.x0 < source_bounds.x1);
    CHECK(source_bounds.y0 < source_bounds.y1);
    CHECK(source_bounds.x1 - source_bounds.x0 > 2.0f * inset_x);
    CHECK(source_bounds.y1 - source_bounds.y0 > 2.0f * inset_y);

    full = make_crop(
        0,
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x1,
        source_bounds.y1);
    crop = make_crop(
        0,
        source_bounds.x0 + inset_x,
        source_bounds.y0 + inset_y,
        source_bounds.x1 - inset_x,
        source_bounds.y1 - inset_y);

    CHECK(extractpdf_crop_pages(document, &full, 1, &baseline) == EXTRACTPDF_OK);
    CHECK(baseline != NULL);
    CHECK(extractpdf_output_data(
              baseline, &baseline_data, &baseline_size) == EXTRACTPDF_OK);
    CHECK(baseline_data != NULL && baseline_size != 0);
    CHECK(crop_raw_expect_no_local_default_boxes(
              baseline_data, baseline_size, 0));

    CHECK(extractpdf_crop_pages(document, &crop, 1, &changed) == EXTRACTPDF_OK);
    CHECK(changed != NULL);
    CHECK(extractpdf_output_data(
              changed, &changed_data, &changed_size) == EXTRACTPDF_OK);
    CHECK(changed_data != NULL && changed_size != 0);
    CHECK(crop_raw_expect_local_cropbox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(crop_raw_expect_no_local_default_boxes(
              changed_data, changed_size, 0));
    CHECK(crop_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    source_after = page_bounds(document);
    check_rect_close(source_after, source_bounds);

    (void)remove(CROP_OUTPUT_PDF);
    CHECK(write_bytes(CROP_OUTPUT_PDF, changed_data, changed_size));
    reopened = open_document(CROP_OUTPUT_PDF);
    output_bounds = page_bounds(reopened);
    check_rect_close(
        output_bounds,
        (extractpdf_rect){
            0.0f,
            0.0f,
            crop.bounds.x1 - crop.bounds.x0,
            crop.bounds.y1 - crop.bounds.y0});

    extractpdf_close(reopened);
    extractpdf_drop_output(changed);
    extractpdf_drop_output(baseline);
    extractpdf_close(document);
    (void)remove(CROP_OUTPUT_PDF);
}

int main(void)
{
    static const float rotate_raw[4] = {20.0f, 20.0f, 380.0f, 280.0f};
    static const float userunit_raw[4] = {10.0f, 10.0f, 190.0f, 140.0f};
    static const float outside_raw[4] = {10.0f, 10.0f, 270.0f, 180.0f};
    int result = extractpdf_pdf_crop_base_main();

    if (result != EXIT_SUCCESS)
        return result;

    run_transformed_case(CROP_ROTATE_90_PDF, 20.0f, 20.0f, rotate_raw);
    run_transformed_case(CROP_USERUNIT_PDF, 20.0f, 20.0f, userunit_raw);
    run_transformed_case(
        CROP_OUTSIDE_MEDIA_PDF, 10.0f, 10.0f, outside_raw);
    return EXIT_SUCCESS;
}
