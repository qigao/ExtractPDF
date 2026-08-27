#include <extractpdf/extractpdf.h>
#include <math.h>
#include <stddef.h>
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

int main(void)
{
    int sentinel = 0;
    extractpdf_document *doc = NULL;
    extractpdf_page *page = NULL;
    extractpdf_bitmap *bitmap = (extractpdf_bitmap *)&sentinel;
    extractpdf_render_options options = { sizeof(options), 144.0f, 0.0f, 0, { 0 } };
    const unsigned char *data = NULL;
    size_t data_size = 0;
    size_t i;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(doc, 0, &page) == EXTRACTPDF_OK);

    CHECK(extractpdf_render_page_with_options(NULL, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_render_page_with_options(page, NULL, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_render_page_with_options(page, &options, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    options.struct_size = 0;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.struct_size = sizeof(options);
    options.dpi = 0.0f;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.dpi = 144.0f;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(bitmap != NULL);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 144);
    CHECK(height == 144);
    CHECK(stride == 144 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    /* An older caller-provided options size must ignore a newer alpha field. */
    options.struct_size = offsetof(extractpdf_render_options, alpha);
    options.dpi = 72.0f;
    options.rotation_degrees = 0.0f;
    options.clip_enabled = 0;
    options.alpha = 1;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    options.struct_size = sizeof(options);
    options.alpha = 1;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 4);
    CHECK(components == 4);
    CHECK(extractpdf_bitmap_data(bitmap, &data, &data_size) == EXTRACTPDF_OK);
    CHECK(data_size == (size_t)72 * 72 * 4);
    for (i = 0; i < data_size; ++i)
        CHECK(data[i] == 0);
    extractpdf_drop_bitmap(bitmap);

    options.alpha = 2;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    options.alpha = 0;

    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_thumbnail(NULL, 144, 144, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_render_thumbnail(page, 0, 144, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_render_thumbnail(page, 144, 0, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_render_thumbnail(page, 144, 144, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    /* Thumbnail policy never upscales above the page's 72-DPI size. */
    CHECK(extractpdf_render_thumbnail(page, 144, 144, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    extractpdf_drop_page(page);
    extractpdf_close(doc);

    doc = NULL;
    page = NULL;
    bitmap = NULL;
    CHECK(extractpdf_open(PAGE_BOXES_PDF, NULL, &doc) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(doc, 0, &page) == EXTRACTPDF_OK);

    /* A caller using the v1 struct size gets the new field's default behavior. */
    options.struct_size = offsetof(extractpdf_render_options, rotation_degrees);
    options.dpi = 72.0f;
    options.rotation_degrees = 90.0f;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 180);
    CHECK(height == 60);
    extractpdf_drop_bitmap(bitmap);

    options.struct_size = offsetof(extractpdf_render_options, clip_enabled);
    options.rotation_degrees = 90.0f;
    options.clip_enabled = 1;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 60);
    CHECK(height == 180);
    extractpdf_drop_bitmap(bitmap);

    options.struct_size = sizeof(options);
    options.rotation_degrees = 90.0f;
    options.clip_enabled = 0;
    options.alpha = 0;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 60);
    CHECK(height == 180);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    options.rotation_degrees = NAN;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.rotation_degrees = 0.0f;
    options.clip_enabled = 1;
    options.clip.x0 = 20.0f;
    options.clip.y0 = 10.0f;
    options.clip.x1 = 80.0f;
    options.clip.y1 = 40.0f;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 60);
    CHECK(height == 30);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    options.alpha = 1;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 60);
    CHECK(height == 30);
    CHECK(stride == 60 * 4);
    CHECK(components == 4);
    CHECK(extractpdf_bitmap_data(bitmap, &data, &data_size) == EXTRACTPDF_OK);
    CHECK(data_size == (size_t)60 * 30 * 4);
    for (i = 0; i < data_size; ++i)
        CHECK(data[i] == 0);
    extractpdf_drop_bitmap(bitmap);
    options.alpha = 0;

    options.rotation_degrees = 90.0f;
    bitmap = NULL;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 30);
    CHECK(height == 60);
    extractpdf_drop_bitmap(bitmap);

    options.rotation_degrees = 0.0f;
    options.clip.x1 = options.clip.x0;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.clip.x1 = 80.0f;
    options.clip.y0 = NAN;
    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_render_page_with_options(page, &options, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    /* Fit within the requested pixel box while preserving aspect ratio. */
    bitmap = NULL;
    CHECK(extractpdf_render_thumbnail(page, 90, 90, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 90);
    CHECK(height == 30);
    CHECK(stride == 90 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    bitmap = NULL;
    CHECK(extractpdf_render_thumbnail(page, 100, 20, &bitmap) == EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == EXTRACTPDF_OK);
    CHECK(width == 60);
    CHECK(height == 20);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    extractpdf_drop_bitmap(bitmap);

    extractpdf_drop_page(page);
    extractpdf_close(doc);
    return EXIT_SUCCESS;
}
