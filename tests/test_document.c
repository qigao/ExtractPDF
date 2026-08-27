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

static void trace_step(const char *step)
{
    fprintf(stderr, "[extractpdf.document] %s\n", step);
    fflush(stderr);
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void test_arguments_and_errors(void)
{
    int sentinel = 0;
    extractpdf_document *doc = (extractpdf_document *)&sentinel;
    int pages = 123;

    trace_step("argument validation");
    CHECK(extractpdf_open(NULL, NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open("", NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    trace_step("missing file");
    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(MISSING_PDF, NULL, &doc) == EXTRACTPDF_ERROR_IO);
    CHECK(doc == NULL);

    trace_step("encrypted file without password");
    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(ENCRYPTED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    trace_step("encrypted file with wrong password");
    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(ENCRYPTED_PDF, "wrong", &doc) == EXTRACTPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    trace_step("encrypted file with correct password");
    doc = NULL;
    CHECK(extractpdf_open(ENCRYPTED_PDF, "user-pass", &doc) == EXTRACTPDF_OK);
    CHECK(doc != NULL);
    extractpdf_close(doc);

    trace_step("truncated file");
    doc = (extractpdf_document *)&sentinel;
    CHECK(extractpdf_open(TRUNCATED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(doc == NULL);

    trace_step("page-count argument validation");
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
    trace_step("repeated lifecycle start");
    for (i = 0; i < 100; ++i) {
        extractpdf_document *doc = NULL;
        int pages = 0;
        if ((i % 10) == 0) {
            fprintf(stderr, "[extractpdf.document] repeated lifecycle iteration %d\n", i);
            fflush(stderr);
        }
        CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
        CHECK(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
        CHECK(pages == 1);
        extractpdf_close(doc);
    }
    trace_step("repeated lifecycle complete");
}

static void test_handle_isolation(void)
{
    extractpdf_document *a = NULL;
    extractpdf_document *b = NULL;
    int a_pages = 0;
    int b_pages = 0;

    trace_step("handle isolation");
    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &a) == EXTRACTPDF_OK);
    CHECK(extractpdf_open(TWO_PAGE_PDF, NULL, &b) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(b, &b_pages) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(a, &a_pages) == EXTRACTPDF_OK);
    CHECK(a_pages == 1);
    CHECK(b_pages == 2);
    extractpdf_close(a);
    extractpdf_close(b);
}

static void test_page_lifecycle(void)
{
    int sentinel = 0;
    extractpdf_document *doc = NULL;
    extractpdf_page *page = (extractpdf_page *)&sentinel;

    trace_step("page lifecycle");
    CHECK(extractpdf_load_page(NULL, 0, &page) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);

    page = (extractpdf_page *)&sentinel;
    CHECK(extractpdf_load_page(doc, -1, &page) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    page = (extractpdf_page *)&sentinel;
    CHECK(extractpdf_load_page(doc, 1, &page) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    CHECK(extractpdf_load_page(doc, 0, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    page = NULL;
    CHECK(extractpdf_load_page(doc, 0, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    extractpdf_drop_page(page);
    extractpdf_drop_page(NULL);

    extractpdf_close(doc);
}

static void test_utf8_path(void)
{
    extractpdf_document *doc = NULL;
    int pages = -1;

    trace_step("utf8 path");
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
    test_page_lifecycle();
    test_utf8_path();
    trace_step("close null");
    extractpdf_close(NULL);
    trace_step("complete");
    return EXIT_SUCCESS;
}
