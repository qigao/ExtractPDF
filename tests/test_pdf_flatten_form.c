#include "test_pdf_flatten_internal.h"

#include "../src/internal.h"
#include "../src/pdf_appearance_common.h"

#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int check_source_widget_count(
    extractpdf_document *document,
    size_t expected)
{
    extractpdf_form *form = NULL;
    size_t widget_count = 0;

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == expected);
    extractpdf_drop_form(form);
    return 0;
}

static int check_source_widget_appearance(extractpdf_document *document)
{
    pdf_document *pdf;
    pdf_obj *page;
    pdf_obj *annots;
    pdf_obj *widget;
    pdf_obj *appearance = NULL;
    extractpdf_pdf_appearance_view view;
    extractpdf_status status;

    memset(&view, 0, sizeof(view));
    pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    CHECK(pdf != NULL);
    page = pdf_lookup_page_obj(document->ctx, pdf, 0);
    CHECK(pdf_is_dict(document->ctx, page));
    annots = pdf_dict_get(document->ctx, page, PDF_NAME(Annots));
    CHECK(pdf_is_array(document->ctx, annots));
    CHECK(pdf_array_len(document->ctx, annots) == 1);
    widget = pdf_array_get(document->ctx, annots, 0);
    CHECK(pdf_is_indirect(document->ctx, widget));
    CHECK(pdf_is_dict(document->ctx, widget));

    status = extractpdf_pdf_appearance_resolve(
        document->ctx, pdf, widget, &view, &appearance);
    fprintf(stderr, "merged widget appearance status=%d\n", (int)status);
    CHECK(status == EXTRACTPDF_OK);
    CHECK(appearance != NULL);
    CHECK(pdf_is_indirect(document->ctx, appearance));
    CHECK(pdf_is_stream(document->ctx, appearance));
    extractpdf_pdf_appearance_drop_view(&view);
    return 0;
}

int extractpdf_test_pdf_flatten_form(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    extractpdf_status status;

    CHECK(extractpdf_open(FLATTEN_WIDGETS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_widget_count(document, 1) == 0);
    CHECK(check_source_widget_appearance(document) == 0);

    status = extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output);
    fprintf(stderr, "merged widget flatten status=%d\n", (int)status);
    CHECK(status == EXTRACTPDF_OK);
    CHECK(output != NULL);

    CHECK(check_source_widget_count(document, 1) == 0);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
