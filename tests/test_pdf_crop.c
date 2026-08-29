#include <extractpdf/extractpdf.h>

#include <math.h>
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

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
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

static void expect_crop_error(
    extractpdf_document *document,
    const extractpdf_page_crop *crops,
    size_t count,
    extractpdf_status expected)
{
    extractpdf_output *output = output_sentinel();

    CHECK(extractpdf_crop_pages(document, crops, count, &output) == expected);
    CHECK(output == NULL);
}

static extractpdf_rect page_bounds(extractpdf_document *document, int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static void check_rect_equal(extractpdf_rect a, extractpdf_rect b)
{
    CHECK(a.x0 == b.x0);
    CHECK(a.y0 == b.y0);
    CHECK(a.x1 == b.x1);
    CHECK(a.y1 == b.y1);
}

static extractpdf_document *open_document(const char *path, const char *password)
{
    extractpdf_document *document = NULL;

    CHECK(extractpdf_open(path, password, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_document *other = NULL;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    extractpdf_output *output = output_sentinel();
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    extractpdf_rect source_before;
    extractpdf_rect source_after;
    extractpdf_page_crop full;
    extractpdf_page_crop crop;
    extractpdf_page_crop pair[2];
    extractpdf_page_crop bad;

    document = open_document(CROP_INTERACTIVE_PDF, NULL);
    source_before = page_bounds(document, 0);
    CHECK(source_before.x0 == 0.0f);
    CHECK(source_before.y0 == 0.0f);
    CHECK(source_before.x1 == 400.0f);
    CHECK(source_before.y1 == 300.0f);

    full = make_crop(0, 0.0f, 0.0f, 400.0f, 300.0f);
    crop = make_crop(0, 50.0f, 40.0f, 350.0f, 260.0f);

    CHECK(extractpdf_crop_pages(document, &crop, 1, NULL) ==
          EXTRACTPDF_ERROR_ARGUMENT);

    expect_crop_error(NULL, &crop, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_crop_error(document, NULL, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_crop_error(document, &crop, 0, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.struct_size =
        offsetof(extractpdf_page_crop, bounds) + sizeof(extractpdf_rect) - 1;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.page_index = -1;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.page_index = 2;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    pair[0] = crop;
    pair[1] = make_crop(0, 60.0f, 50.0f, 340.0f, 250.0f);
    expect_crop_error(document, pair, 2, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x0 = NAN;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.y0 = INFINITY;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.x1 = -INFINITY;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x1 = bad.bounds.x0;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.y1 = bad.bounds.y0;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.x0 = 360.0f;
    bad.bounds.x1 = 350.0f;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x0 = -1.0f;
    expect_crop_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    other = open_document(NON_PDF, NULL);
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(ENCRYPTED_PDF, "user-pass");
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(SIGNED_PDF, NULL);
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_BOX_PDF, NULL);
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_ROTATE_PDF, NULL);
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_USERUNIT_PDF, NULL);
    expect_crop_error(other, &full, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    CHECK(extractpdf_crop_pages(document, &full, 1, &first) == EXTRACTPDF_OK);
    CHECK(first != NULL);
    CHECK(extractpdf_crop_pages(document, &full, 1, &second) == EXTRACTPDF_OK);
    CHECK(second != NULL);
    CHECK(extractpdf_output_data(first, &first_data, &first_size) ==
          EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(second, &second_data, &second_size) ==
          EXTRACTPDF_OK);
    CHECK(first_data != NULL);
    CHECK(second_data != NULL);
    CHECK(first_size != 0);
    CHECK(second_size == first_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    source_after = page_bounds(document, 0);
    check_rect_equal(source_before, source_after);

    output = output_sentinel();
    if (extractpdf_crop_pages(document, &crop, 1, &output) != EXTRACTPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid crop failed\n");
        CHECK(output == NULL);
        extractpdf_drop_output(first);
        extractpdf_drop_output(second);
        extractpdf_close(document);
        return EXIT_FAILURE;
    }

    extractpdf_drop_output(output);
    extractpdf_drop_output(first);
    extractpdf_drop_output(second);
    extractpdf_close(document);
    return EXIT_SUCCESS;
}
