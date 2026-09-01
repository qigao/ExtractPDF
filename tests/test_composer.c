#include <quantapdf/quantapdf.h>

#include "composer_test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static quantapdf_composer *composer_sentinel(void)
{
    return (quantapdf_composer *)(uintptr_t)1;
}

static int test_create_contract(void)
{
    quantapdf_composer *composer = composer_sentinel();
    quantapdf_composer_options options = {0};

    CHECK(quantapdf_composer_create(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(composer != NULL && composer != composer_sentinel());
    quantapdf_drop_composer(composer);
    quantapdf_drop_composer(NULL);

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE - 1u;
    composer = composer_sentinel();
    CHECK(quantapdf_composer_create(&options, &composer) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(composer == NULL);
    return 0;
}

static int test_page_validation_and_capacity(void)
{
    quantapdf_composer_options options = {0};
    quantapdf_composer_page_options page = {0};
    quantapdf_composer *composer = NULL;
    size_t page_index = SIZE_MAX;

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    options.max_pages = 1u;
    CHECK(quantapdf_composer_create(&options, &composer) == QUANTAPDF_OK);

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 612.0f;
    page.height_points = 792.0f;
    page.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_OK);
    CHECK(page_index == 0u);

    page_index = 19u;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(page_index == SIZE_MAX);

    page.width_points = 0.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);
    page.width_points = 612.0f;
    page.height_points = -1.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_finish_text_document(void)
{
    static const char expected[] = "Hello copied text";
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {36.0f, 48.0f, 360.0f, 120.0f};
    quantapdf_rect actual_bounds = {0};
    char text_input[] = "Hello copied text";
    char *extracted = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t extracted_size = 0u;
    size_t page_index = SIZE_MAX;
    int page_count = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 612.0f;
    page_options.height_points = 792.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);

    text_options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text_options.font_size = 18.0f;
    text_options.argb = UINT32_C(0xff204080);
    text_options.line_height_multiplier = 1.2f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, text_input, &bounds, &text_options) ==
          QUANTAPDF_OK);
    memset(text_input, 'X', sizeof(text_input) - 1u);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size > 8u);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(memcmp(first_data, "%PDF-", 5u) == 0);
    CHECK(quantapdf_output_save_file(first, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &actual_bounds) == QUANTAPDF_OK);
    CHECK(actual_bounds.x1 == 612.0f);
    CHECK(actual_bounds.y1 == 792.0f);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted_size >= sizeof(expected) - 1u);
    CHECK(memcmp(extracted, expected, sizeof(expected) - 1u) == 0);

    quantapdf_free(extracted);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_jpeg_resource_and_placement(void)
{
    static const unsigned char invalid_image[] = {0xffu, 0xd8u, 0xffu};
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_composer_image_id image_id = UINT32_MAX;
    quantapdf_image_info info = {0};
    quantapdf_rect bounds = {36.0f, 200.0f, 236.0f, 300.0f};
    unsigned char *jpeg = NULL;
    size_t jpeg_size = 0u;
    size_t page_index = SIZE_MAX;
    size_t image_count = 0u;

    CHECK(quantapdf_test_make_jpeg(&jpeg, &jpeg_size));
    CHECK(jpeg != NULL && jpeg_size > 4u);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 300.0f;
    page_options.height_points = 400.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);

    CHECK(quantapdf_composer_add_image(
              composer, invalid_image, sizeof(invalid_image), &image_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(image_id == 0u);
    CHECK(quantapdf_composer_add_image(
              composer, jpeg, jpeg_size, &image_id) == QUANTAPDF_OK);
    CHECK(image_id == 1u);
    memset(jpeg, 0, jpeg_size);
    free(jpeg);

    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN;
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, image_id, &bounds, &image_options) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &image_count) == QUANTAPDF_OK);
    CHECK(image_count == 1u);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 0u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 8);
    CHECK(info.pixel_height == 4);
    CHECK(info.components == 3);
    CHECK(info.quad.ul.x == 36.0f);
    CHECK(info.quad.ul.y == 200.0f);
    CHECK(info.quad.lr.x == 236.0f);
    CHECK(info.quad.lr.y == 300.0f);

    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

static int test_png_and_alpha(void)
{
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_render_options render_options = {0};
    quantapdf_composer_image_id opaque_id = 0u;
    quantapdf_composer_image_id alpha_id = 0u;
    quantapdf_composer_image_id corrupt_id = UINT32_MAX;
    quantapdf_image_info info = {0};
    quantapdf_rect opaque_bounds = {10.0f, 10.0f, 50.0f, 30.0f};
    quantapdf_rect alpha_bounds = {10.0f, 40.0f, 50.0f, 60.0f};
    unsigned char *opaque_png = NULL;
    unsigned char *alpha_png = NULL;
    const unsigned char *pixels = NULL;
    size_t opaque_size = 0u;
    size_t alpha_size = 0u;
    size_t page_index = SIZE_MAX;
    size_t image_count = 0u;
    size_t pixel_size = 0u;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_test_make_png(0, &opaque_png, &opaque_size));
    CHECK(quantapdf_test_make_png(1, &alpha_png, &alpha_size));
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 100.0f;
    page_options.height_points = 100.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, opaque_png, opaque_size, &opaque_id) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_image(
              composer, alpha_png, alpha_size, &alpha_id) == QUANTAPDF_OK);
    alpha_png[alpha_size - 1u] ^= 1u;
    CHECK(quantapdf_composer_add_image(
              composer, alpha_png, alpha_size, &corrupt_id) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(corrupt_id == 0u);
    free(opaque_png);
    free(alpha_png);
    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, opaque_id, &opaque_bounds,
              &image_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_image(
              composer, page_index, alpha_id, &alpha_bounds,
              &image_options) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &image_count) == QUANTAPDF_OK);
    CHECK(image_count == 2u);
    info.struct_size = sizeof(info);
    CHECK(quantapdf_image_get_info(images, 1u, &info) == QUANTAPDF_OK);
    CHECK(info.pixel_width == 2);
    CHECK(info.pixel_height == 1);
    CHECK(info.has_alpha == 1);

    render_options.struct_size = sizeof(render_options);
    render_options.dpi = 72.0f;
    CHECK(quantapdf_render_page_with_options(
              page, &render_options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 100 && height == 100 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size == (size_t)stride * (size_t)height);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u + 1u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 15u * 3u + 2u] > 245u);
    CHECK(pixels[50u * (size_t)stride + 45u * 3u] < 80u);
    CHECK(pixels[50u * (size_t)stride + 45u * 3u + 2u] > 180u);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(output);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_create_contract() == 0);
    CHECK(test_page_validation_and_capacity() == 0);
    CHECK(test_finish_text_document() == 0);
    CHECK(test_jpeg_resource_and_placement() == 0);
    CHECK(test_png_and_alpha() == 0);
    return 0;
}
