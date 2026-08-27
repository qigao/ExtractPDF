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

static void check_rect(
    extractpdf_rect rect,
    float x0,
    float y0,
    float x1,
    float y1)
{
    CHECK(rect.x0 == x0);
    CHECK(rect.y0 == y0);
    CHECK(rect.x1 == x1);
    CHECK(rect.y1 == y1);
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = { 11.0f, 12.0f, 13.0f, 14.0f };
    int rotation = -1;

    CHECK(extractpdf_open(GEOMETRY_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);

    CHECK(extractpdf_page_bounds(NULL, EXTRACTPDF_PAGE_BOX_CROP, &bounds) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    check_rect(bounds, 11.0f, 12.0f, 13.0f, 14.0f);

    CHECK(extractpdf_page_bounds(page, EXTRACTPDF_PAGE_BOX_CROP, NULL) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_page_bounds(page, (extractpdf_page_box)99, &bounds) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    check_rect(bounds, 11.0f, 12.0f, 13.0f, 14.0f);

    CHECK(extractpdf_page_bounds(page, EXTRACTPDF_PAGE_BOX_CROP, &bounds) ==
          EXTRACTPDF_OK);
    check_rect(bounds, 0.0f, 0.0f, 160.0f, 300.0f);

    CHECK(extractpdf_page_bounds(page, EXTRACTPDF_PAGE_BOX_MEDIA, &bounds) ==
          EXTRACTPDF_OK);
    check_rect(bounds, -20.0f, -50.0f, 180.0f, 350.0f);

    CHECK(extractpdf_page_rotation(NULL, &rotation) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(rotation == -1);
    CHECK(extractpdf_page_rotation(page, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_page_rotation(page, &rotation) == EXTRACTPDF_OK);
    CHECK(rotation == 90);

    extractpdf_drop_page(page);
    extractpdf_close(document);
    return EXIT_SUCCESS;
}
