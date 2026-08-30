#include <extractpdf/extractpdf.h>

#include <stdio.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int check_form_counts(
    extractpdf_document *document,
    size_t expected_fields,
    size_t expected_widgets)
{
    extractpdf_form *form = NULL;
    size_t field_count = 0;
    size_t widget_count = 0;

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == expected_fields);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == expected_widgets);
    extractpdf_drop_form(form);
    return 0;
}

int extractpdf_test_pdf_flatten_form_multi(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(
        FLATTEN_MULTI_WIDGET_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_form_counts(document, 1, 2) == 0);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    CHECK(check_form_counts(document, 1, 2) == 0);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
