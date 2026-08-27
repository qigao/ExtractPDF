#include <extractpdf/extractpdf.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void check_rgb_pixel(
    const unsigned char *pixels,
    int stride,
    int x,
    int y,
    unsigned char r,
    unsigned char g,
    unsigned char b)
{
    const unsigned char *pixel = pixels + (size_t)y * (size_t)stride +
                                 (size_t)x * 3u;
    CHECK(pixel[0] == r);
    CHECK(pixel[1] == g);
    CHECK(pixel[2] == b);
}

static void check_rgba_pixel(
    const unsigned char *pixels,
    int stride,
    int x,
    int y,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a)
{
    const unsigned char *pixel = pixels + (size_t)y * (size_t)stride +
                                 (size_t)x * 4u;
    CHECK(pixel[0] == r);
    CHECK(pixel[1] == g);
    CHECK(pixel[2] == b);
    CHECK(pixel[3] == a);
}

int main(void)
{
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_bitmap *rgb = (extractpdf_bitmap *)&sentinel;
    extractpdf_bitmap *rgba = NULL;
    extractpdf_bitmap_info info = { 0 };
    extractpdf_render_options options;
    const unsigned char *pixels = NULL;
    size_t size = 0;

    CHECK(extractpdf_open(RENDER_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);

    CHECK(extractpdf_render_page(NULL, NULL, &rgb) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(rgb == NULL);
    CHECK(extractpdf_render_page(page, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    options.struct_size = 0;
    options.pixel_format = EXTRACTPDF_PIXEL_FORMAT_RGB8;
    rgb = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page(page, &options, &rgb) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(rgb == NULL);

    options.struct_size = sizeof(options);
    options.pixel_format = (extractpdf_pixel_format)99;
    rgb = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page(page, &options, &rgb) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(rgb == NULL);

    rgb = NULL;
    CHECK(extractpdf_render_page(page, NULL, &rgb) == EXTRACTPDF_OK);
    CHECK(rgb != NULL);
    CHECK(extractpdf_bitmap_get_info(rgb, &info) == EXTRACTPDF_OK);
    CHECK(info.width == 100);
    CHECK(info.height == 50);
    CHECK(info.stride == 300);
    CHECK(info.pixel_format == EXTRACTPDF_PIXEL_FORMAT_RGB8);
    CHECK(info.data_size == 15000u);
    CHECK(extractpdf_bitmap_get_pixels(rgb, &pixels, &size) == EXTRACTPDF_OK);
    CHECK(pixels != NULL);
    CHECK(size == info.data_size);
    check_rgb_pixel(pixels, info.stride, 0, 0, 255, 255, 255);
    check_rgb_pixel(pixels, info.stride, 20, 25, 0, 0, 0);

    options.struct_size = sizeof(options);
    options.pixel_format = EXTRACTPDF_PIXEL_FORMAT_RGBA8;
    CHECK(extractpdf_render_page(page, &options, &rgba) == EXTRACTPDF_OK);
    CHECK(rgba != NULL);
    CHECK(extractpdf_bitmap_get_info(rgba, &info) == EXTRACTPDF_OK);
    CHECK(info.width == 100);
    CHECK(info.height == 50);
    CHECK(info.stride == 400);
    CHECK(info.pixel_format == EXTRACTPDF_PIXEL_FORMAT_RGBA8);
    CHECK(info.data_size == 20000u);
    CHECK(extractpdf_bitmap_get_pixels(rgba, &pixels, &size) == EXTRACTPDF_OK);
    CHECK(size == info.data_size);
    check_rgba_pixel(pixels, info.stride, 0, 0, 0, 0, 0, 0);
    check_rgba_pixel(pixels, info.stride, 20, 25, 0, 0, 0, 255);

    extractpdf_drop_page(page);
    extractpdf_close(document);

    /* Rendered bitmaps own their pixels and outlive the source page/document. */
    CHECK(extractpdf_bitmap_get_info(rgb, &info) == EXTRACTPDF_OK);
    CHECK(info.width == 100);
    CHECK(extractpdf_bitmap_get_pixels(rgb, &pixels, &size) == EXTRACTPDF_OK);
    check_rgb_pixel(pixels, info.stride, 20, 25, 0, 0, 0);

    CHECK(extractpdf_bitmap_get_info(NULL, &info) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_bitmap_get_info(rgb, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_bitmap_get_pixels(NULL, &pixels, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_bitmap_get_pixels(rgb, NULL, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_bitmap_get_pixels(rgb, &pixels, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    extractpdf_drop_bitmap(rgb);
    extractpdf_drop_bitmap(rgba);
    extractpdf_drop_bitmap(NULL);
    return EXIT_SUCCESS;
}
