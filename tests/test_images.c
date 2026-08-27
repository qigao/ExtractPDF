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

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void quad_bounds(
    const extractpdf_quad *quad,
    float *x0,
    float *y0,
    float *x1,
    float *y1)
{
    const extractpdf_point points[4] = {
        quad->ul, quad->ur, quad->ll, quad->lr
    };
    size_t i;

    *x0 = *x1 = points[0].x;
    *y0 = *y1 = points[0].y;
    for (i = 1; i < 4; ++i) {
        if (points[i].x < *x0)
            *x0 = points[i].x;
        if (points[i].x > *x1)
            *x1 = points[i].x;
        if (points[i].y < *y0)
            *y0 = points[i].y;
        if (points[i].y > *y1)
            *y1 = points[i].y;
    }
}

static void check_info(
    const extractpdf_image_info *info,
    float expected_x0,
    float expected_y0,
    float expected_x1,
    float expected_y1)
{
    float x0;
    float y0;
    float x1;
    float y1;

    CHECK(info->struct_size == sizeof(*info));
    CHECK(info->pixel_width == 2);
    CHECK(info->pixel_height == 1);
    CHECK(info->components == 3);
    CHECK(info->bits_per_component == 8);
    CHECK(info->has_alpha == 0);

    quad_bounds(&info->quad, &x0, &y0, &x1, &y1);
    CHECK(close_float(x0, expected_x0));
    CHECK(close_float(y0, expected_y0));
    CHECK(close_float(x1, expected_x1));
    CHECK(close_float(y1, expected_y1));
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_image_page *images = NULL;
    extractpdf_image_info info0 = { sizeof(info0) };
    extractpdf_image_info info1 = { sizeof(info1) };
    size_t count = 0;

    CHECK(extractpdf_open(PAGE_IMAGES_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_images(page, &images) == EXTRACTPDF_OK);
    CHECK(images != NULL);

    /* The image snapshot may outlive the page, but not the document. */
    extractpdf_drop_page(page);
    page = NULL;

    CHECK(extractpdf_image_count(images, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);

    CHECK(extractpdf_image_get_info(images, 0, &info0) == EXTRACTPDF_OK);
    CHECK(extractpdf_image_get_info(images, 1, &info1) == EXTRACTPDF_OK);

    check_info(&info0, 10.0f, 60.0f, 50.0f, 80.0f);
    check_info(&info1, 100.0f, 10.0f, 120.0f, 50.0f);

    extractpdf_drop_image_page(images);
    extractpdf_close(document);
    return EXIT_SUCCESS;
}
