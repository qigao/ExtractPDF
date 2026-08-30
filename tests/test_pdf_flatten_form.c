#include "test_pdf_flatten_internal.h"

#include <extractpdf/extractpdf.h>

#include <stdio.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

int extractpdf_test_pdf_flatten_form(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(FLATTEN_WIDGETS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
