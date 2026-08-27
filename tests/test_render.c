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

int main(void)
{
    int sentinel = 0;
    extractpdf_document *doc = NULL;
    extractpdf_page *page = NULL;
    extractpdf_bitmap *bitmap = (extractpdf_bitmap *)&sentinel;
    extractpdf_render_options options = { sizeof(options), 144.0f };
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
    extractpdf_drop_page(page);
    extractpdf_close(doc);
    return EXIT_SUCCESS;
}
