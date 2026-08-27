#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void test_decode_retained_occurrence(void)
{
    static const unsigned char expected[] = {
        0xff, 0x00, 0x00,
        0x00, 0xff, 0x00
    };
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_image_page *images = NULL;
    extractpdf_bitmap *bitmap = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(extractpdf_open(PAGE_IMAGES_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_images(page, &images) == EXTRACTPDF_OK);

    /* Decode must use the retained occurrence, not the source page. */
    extractpdf_drop_page(page);
    page = NULL;

    CHECK(extractpdf_image_render(images, 0, &bitmap) == EXTRACTPDF_OK);
    CHECK(bitmap != NULL);

    /* The returned bitmap owns its decoded pixels independently. */
    extractpdf_drop_image_page(images);
    images = NULL;

    CHECK(extractpdf_bitmap_dimensions(
        bitmap,
        &width,
        &height,
        &stride,
        &components) == EXTRACTPDF_OK);
    CHECK(width == 2);
    CHECK(height == 1);
    CHECK(stride == 6);
    CHECK(components == 3);

    CHECK(extractpdf_bitmap_data(bitmap, &data, &size) == EXTRACTPDF_OK);
    CHECK(data != NULL);
    CHECK(size == sizeof(expected));
    CHECK(memcmp(data, expected, sizeof(expected)) == 0);

    extractpdf_drop_bitmap(bitmap);
    extractpdf_close(document);
}

static void test_argument_contract(void)
{
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_image_page *images = NULL;
    extractpdf_bitmap *bitmap = (extractpdf_bitmap *)&sentinel;

    CHECK(extractpdf_image_render(NULL, 0, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_image_render(NULL, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_open(PAGE_IMAGES_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_images(page, &images) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);

    bitmap = (extractpdf_bitmap *)&sentinel;
    CHECK(extractpdf_image_render(images, 99, &bitmap) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(extractpdf_image_render(images, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    extractpdf_drop_image_page(images);
    extractpdf_close(document);
}

int main(void)
{
    test_decode_retained_occurrence();
    test_argument_contract();
    return EXIT_SUCCESS;
}
