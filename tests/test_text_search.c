#include <extractpdf/extractpdf.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
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

static void check_point(extractpdf_point point)
{
    CHECK(isfinite(point.x));
    CHECK(isfinite(point.y));
}

static void check_quad(const extractpdf_quad *quad)
{
    check_point(quad->ul);
    check_point(quad->ur);
    check_point(quad->ll);
    check_point(quad->lr);
    CHECK(quad->ur.x > quad->ul.x);
    CHECK(quad->lr.x > quad->ll.x);
    CHECK(quad->ll.y > quad->ul.y);
    CHECK(quad->lr.y > quad->ur.y);
}

int main(void)
{
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_text_page *text = NULL;
    extractpdf_search_result result = { sizeof(result), { { 0 } } };
    extractpdf_search_result repeated[2] = {
        { sizeof(repeated[0]), { { 0 } } },
        { sizeof(repeated[1]), { { 0 } } }
    };
    size_t count = 0;

    CHECK(extractpdf_open(STRUCTURED_TEXT_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_structured_text(page, &text) == EXTRACTPDF_OK);

    /* Search must use only the immutable snapshot. */
    extractpdf_drop_page(page);
    extractpdf_close(document);
    page = NULL;
    document = NULL;

    /* UTF-8 search may cross style spans on the same line. */
    count = 123;
    CHECK(extractpdf_text_search(text, "lo Caf\xc3\xa9", NULL, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);

    count = 123;
    CHECK(extractpdf_text_search(text, "lo Caf\xc3\xa9", &result, 1, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(result.struct_size == sizeof(result));
    check_quad(&result.quad);

    /* Repeated matches exercise sizing, insufficient capacity, and fill. */
    count = 123;
    CHECK(extractpdf_text_search(text, "l", NULL, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);

    count = 123;
    CHECK(extractpdf_text_search(text, "l", repeated, 1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 2);

    count = 123;
    CHECK(extractpdf_text_search(text, "l", repeated, 2, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);
    check_quad(&repeated[0].quad);
    check_quad(&repeated[1].quad);
    CHECK(repeated[1].quad.ul.x > repeated[0].quad.ul.x);

    count = 123;
    CHECK(extractpdf_text_search(text, "missing", NULL, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 0);

    /* Invalid arguments reset count before a required count is known. */
    count = 123;
    CHECK(extractpdf_text_search(NULL, "l", NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    count = 123;
    CHECK(extractpdf_text_search(text, NULL, NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    count = 123;
    CHECK(extractpdf_text_search(text, "", NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    count = 123;
    CHECK(extractpdf_text_search(text, "\xc3\x28", NULL, 0, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    count = 123;
    CHECK(extractpdf_text_search(text, "l", NULL, 1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    CHECK(extractpdf_text_search(text, "l", repeated, 2, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    /* V1 result structs require the quad field to be present. */
    result.struct_size = offsetof(extractpdf_search_result, quad);
    count = 123;
    CHECK(extractpdf_text_search(text, "Caf\xc3\xa9", &result, 1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 1);

    extractpdf_drop_text_page(text);
    (void)sentinel;
    return EXIT_SUCCESS;
}
