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
    extractpdf_form *form = NULL;
    extractpdf_output *output = NULL;
    extractpdf_status status;
    size_t widget_count = 0;

    CHECK(extractpdf_open(FLATTEN_WIDGETS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == 7);
    extractpdf_drop_form(form);
    form = NULL;

    status = extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output);
    fprintf(stderr, "widget flatten status=%d\n", (int)status);
    CHECK(status == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
