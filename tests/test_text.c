#include <extractpdf/extractpdf.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
    {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

int main(void)
{
    static const unsigned char expected_prefix[] = {
        'H', 'e', 'l', 'l', 'o', ' ', 'C', 'a', 'f', 0xC3, 0xA9
    };
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    char *text = (char *)&sentinel;
    size_t size = 123;
    size_t i;

    CHECK(extractpdf_open(TEXT_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);

    CHECK(extractpdf_extract_text(NULL, &text, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    size = 123;
    CHECK(extractpdf_extract_text(page, NULL, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(size == 0);

    text = (char *)&sentinel;
    CHECK(extractpdf_extract_text(page, &text, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);

    CHECK(extractpdf_extract_text(page, &text, &size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(size >= sizeof(expected_prefix));

    /* The returned text owns its bytes independently of MuPDF handles. */
    extractpdf_drop_page(page);
    extractpdf_close(document);
    page = NULL;
    document = NULL;

    CHECK(memcmp(text, expected_prefix, sizeof(expected_prefix)) == 0);
    for (i = 0; i < size; ++i)
        CHECK(text[i] != '\0');
    CHECK(text[size] == '\0');
    extractpdf_free(text);

    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    text = (char *)&sentinel;
    size = 123;
    CHECK(extractpdf_extract_text(page, &text, &size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(size == 0);
    CHECK(text[0] == '\0');
    extractpdf_free(text);

    extractpdf_drop_page(page);
    extractpdf_close(document);
    extractpdf_free(NULL);
    return EXIT_SUCCESS;
}
