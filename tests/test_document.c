#include <extractpdf/extractpdf.h>

#include <assert.h>

static void test_arguments_and_errors(void)
{
    int sentinel = 0;
    extractpdf_document *doc = (extractpdf_document *)&sentinel;
    int pages = 123;

    assert(extractpdf_open(NULL, NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    assert(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    assert(extractpdf_open("", NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
    assert(doc == NULL);

    assert(extractpdf_open(ONE_PAGE_PDF, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    doc = (extractpdf_document *)&sentinel;
    assert(extractpdf_open(MISSING_PDF, NULL, &doc) == EXTRACTPDF_ERROR_IO);
    assert(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    assert(extractpdf_open(ENCRYPTED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_PASSWORD);
    assert(doc == NULL);

    doc = (extractpdf_document *)&sentinel;
    assert(extractpdf_open(ENCRYPTED_PDF, "wrong", &doc) == EXTRACTPDF_ERROR_PASSWORD);
    assert(doc == NULL);

    doc = NULL;
    assert(extractpdf_open(ENCRYPTED_PDF, "user-pass", &doc) == EXTRACTPDF_OK);
    assert(doc != NULL);
    extractpdf_close(doc);

    doc = (extractpdf_document *)&sentinel;
    assert(extractpdf_open(TRUNCATED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_FORMAT);
    assert(doc == NULL);

    pages = 123;
    assert(extractpdf_page_count(NULL, &pages) == EXTRACTPDF_ERROR_ARGUMENT);
    assert(pages == 123);

    doc = NULL;
    assert(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
    assert(extractpdf_page_count(doc, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    extractpdf_close(doc);
}

static void test_repeated_lifecycle(void)
{
    int i;

    for (i = 0; i < 100; ++i) {
        extractpdf_document *doc = NULL;
        int pages = 0;

        assert(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
        assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
        assert(pages == 1);
        extractpdf_close(doc);
    }
}

static void test_handle_isolation(void)
{
    extractpdf_document *a = NULL;
    extractpdf_document *b = NULL;
    int a_pages = 0;
    int b_pages = 0;

    assert(extractpdf_open(ONE_PAGE_PDF, NULL, &a) == EXTRACTPDF_OK);
    assert(extractpdf_open(TWO_PAGE_PDF, NULL, &b) == EXTRACTPDF_OK);
    assert(extractpdf_page_count(b, &b_pages) == EXTRACTPDF_OK);
    assert(extractpdf_page_count(a, &a_pages) == EXTRACTPDF_OK);
    assert(a_pages == 1);
    assert(b_pages == 2);
    extractpdf_close(a);
    extractpdf_close(b);
}

int main(void)
{
    test_arguments_and_errors();
    test_repeated_lifecycle();
    test_handle_isolation();
    extractpdf_close(NULL);
    return 0;
}
