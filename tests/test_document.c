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

static void test_arguments_and_errors(void)
{
    int sentinel = 0;
    extractpdf_document *doc = (extractpdf_document *)&sentinel;
    int pages = 123;

    CHECK(extractpdf_open(NULL, NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open("", NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(MISSING_PDF, NULL, &doc) == EXTRACTPDF_ERROR_IO);
    CHECK(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(ENCRYPTED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(ENCRYPTED_PDF, "wrong", &doc) == EXTRACTPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    doc = NULL;
    CHECK(extractpdf_open(ENCRYPTED_PDF, "user-pass", &doc) == EXTRACTPDF_OK);
    CHECK(doc != NULL);
    extractpdf_close(doc);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(TRUNCATED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(doc == NULL);

    pages = 123;
    CHECK(extractpdf_page_count(NULL, &pages) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(pages == 123);

    doc = NULL;
    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(doc, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    extractpdf_close(doc);
}

static void test_repeated_lifecycle(void)
{
    int i;
    for (i = 0; i < 100; ++i) {
        extractpdf_document *doc = NULL;
        int pages = 0;
        CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
        CHECK(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
        CHECK(pages == 1);
        extractpdf_close(doc);
    }
}

static void test_handle_isolation(void)
{
    extractpdf_document *a = NULL;
    extractpdf_document *b = NULL;
    int a_pages = 0;
    int b_pages = 0;

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &a) == EXTRACTPDF_OK);
    CHECK(extractpdf_open(TWO_PAGE_PDF, NULL, &b) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(b, &b_pages) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(a, &a_pages) == EXTRACTPDF_OK);
    CHECK(a_pages == 1);
    CHECK(b_pages == 2);
    extractpdf_close(a);
    extractpdf_close(b);
}

static void test_utf8_path(void)
{
    extractpdf_document *doc = NULL;
    int pages = -1;

    CHECK(extractpdf_open(UTF8_PDF, NULL, &doc) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
    CHECK(pages == 1);
    extractpdf_close(doc);
}

int main(void)
{
    test_arguments_and_errors();
    test_repeated_lifecycle();
    test_handle_isolation();
    test_utf8_path();
    extractpdf_close(NULL);
    return EXIT_SUCCESS;
}
