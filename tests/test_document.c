#include <extractpdf/extractpdf.h>

#include <assert.h>

int main(void)
{
    extractpdf_document *doc = NULL;
    int pages = -1;

    assert(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
    assert(doc != NULL);
    assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
    assert(pages == 1);
    extractpdf_close(doc);

    doc = NULL;
    pages = -1;
    assert(extractpdf_open(TWO_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
    assert(doc != NULL);
    assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
    assert(pages == 2);
    extractpdf_close(doc);

    extractpdf_close(NULL);
    return 0;
}
