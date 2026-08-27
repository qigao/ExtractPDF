#include <extractpdf/extractpdf.h>

#include <stddef.h>
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

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void check_rect(
    const extractpdf_rect *rect,
    float x0,
    float y0,
    float x1,
    float y1)
{
    CHECK(close_float(rect->x0, x0));
    CHECK(close_float(rect->y0, y0));
    CHECK(close_float(rect->x1, x1));
    CHECK(close_float(rect->y1, y1));
}

static void test_links_and_document_independent_lifetime(void)
{
    static const char expected_uri[] =
        "https://example.com/extractpdf-phase3";
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_link_page *links = NULL;
    extractpdf_link_info external = { 0 };
    extractpdf_link_info internal = { 0 };
    const char *uri = NULL;
    size_t uri_size = 0;
    size_t count = 0;

    external.struct_size = sizeof(external);
    internal.struct_size = sizeof(internal);

    CHECK(extractpdf_open(PAGE_LINKS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(links != NULL);

    /*
     * Link snapshots contain only ExtractPDF-owned copies. They must not
     * retain or require the source page, document, or MuPDF link objects.
     */
    extractpdf_drop_page(page);
    page = NULL;
    extractpdf_close(document);
    document = NULL;

    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);

    CHECK(extractpdf_link_get_info(links, 0, &external) == EXTRACTPDF_OK);
    CHECK(external.struct_size == sizeof(external));
    CHECK(external.kind == EXTRACTPDF_LINK_URI);
    check_rect(&external.hotspot, 20.0f, 40.0f, 100.0f, 60.0f);
    CHECK(external.target_page == -1);
    CHECK(close_float(external.target.x, 0.0f));
    CHECK(close_float(external.target.y, 0.0f));

    CHECK(extractpdf_link_uri(links, 0, &uri, &uri_size) == EXTRACTPDF_OK);
    CHECK(uri != NULL);
    CHECK(uri_size == strlen(expected_uri));
    CHECK(memcmp(uri, expected_uri, uri_size) == 0);
    CHECK(uri[uri_size] == '\0');

    CHECK(extractpdf_link_get_info(links, 1, &internal) == EXTRACTPDF_OK);
    CHECK(internal.struct_size == sizeof(internal));
    CHECK(internal.kind == EXTRACTPDF_LINK_INTERNAL);
    check_rect(&internal.hotspot, 20.0f, 80.0f, 100.0f, 100.0f);
    CHECK(internal.target_page == 1);
    CHECK(close_float(internal.target.x, 30.0f));
    CHECK(close_float(internal.target.y, 50.0f));

    extractpdf_drop_link_page(links);
}

static void test_argument_contract(void)
{
    int sentinel = 0;
    extractpdf_link_page *links = (extractpdf_link_page *)&sentinel;
    extractpdf_link_info info = { 0 };
    const char *uri = (const char *)&sentinel;
    size_t count = 99;
    size_t uri_size = 99;

    info.struct_size = sizeof(info);

    CHECK(extractpdf_extract_links(NULL, &links) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(links == NULL);
    CHECK(extractpdf_extract_links(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_link_count(NULL, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(extractpdf_link_count(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_link_get_info(NULL, 0, &info) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_link_get_info(NULL, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_link_uri(NULL, 0, &uri, &uri_size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(uri == NULL);
    CHECK(uri_size == 0);
    CHECK(extractpdf_link_uri(NULL, 0, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    extractpdf_drop_link_page(NULL);
}

int main(void)
{
    test_links_and_document_independent_lifetime();
    test_argument_contract();
    return EXIT_SUCCESS;
}
