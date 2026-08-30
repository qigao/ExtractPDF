#include "test_pdf_flatten_internal.h"
#include "../src/pdf_appearance_common.h"

#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void flatten_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) flatten_check_impl((x), #x, __LINE__)

static pdf_document *open_pdf(fz_context *ctx, const char *filename)
{
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    fz_var(stream);
    fz_var(document);
    fz_try(ctx)
    {
        stream = fz_open_file(ctx, filename);
        document = pdf_open_document_with_stream(ctx, stream);
    }
    fz_always(ctx)
    {
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        pdf_drop_document(ctx, document);
        fz_rethrow(ctx);
    }
    return document;
}

static pdf_obj *page_annot(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    int annot_index)
{
    pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
    pdf_obj *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));

    CHECK(pdf_is_dict(ctx, page));
    CHECK(pdf_is_array(ctx, annots));
    CHECK(annot_index >= 0 && annot_index < pdf_array_len(ctx, annots));
    return pdf_array_get(ctx, annots, annot_index);
}

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.00001f;
}

static void expect_status(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_status expected)
{
    extractpdf_pdf_appearance_view view;
    pdf_obj *form = NULL;
    extractpdf_status status;

    memset(&view, 0, sizeof(view));
    status = extractpdf_pdf_appearance_resolve(
        ctx, document, page_annot(ctx, document, page_index, 0), &view, &form);
    CHECK(status == expected);
    if (status == EXTRACTPDF_OK) {
        CHECK(form != NULL);
        CHECK(pdf_is_indirect(ctx, form));
        CHECK(pdf_is_stream(ctx, form));
    }
    extractpdf_pdf_appearance_drop_view(&view);
}

int extractpdf_test_pdf_flatten_appearance(void)
{
    fz_context *ctx = NULL;
    pdf_document *positive = NULL;
    pdf_document *negative = NULL;
    int caught = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    CHECK(ctx != NULL);

    fz_var(positive);
    fz_var(negative);
    fz_var(caught);
    fz_try(ctx)
    {
        extractpdf_pdf_appearance_view view;
        pdf_obj *form = NULL;
        extractpdf_status status;

        positive = open_pdf(ctx, FLATTEN_APPEARANCE_PDF);
        negative = open_pdf(ctx, FLATTEN_APPEARANCE_MALFORMED_PDF);

        expect_status(ctx, positive, 0, EXTRACTPDF_OK);
        expect_status(ctx, positive, 1, EXTRACTPDF_OK);
        expect_status(ctx, positive, 2, EXTRACTPDF_OK);

        memset(&view, 0, sizeof(view));
        status = extractpdf_pdf_appearance_resolve(
            ctx, positive, page_annot(ctx, positive, 3, 0), &view, &form);
        CHECK(status == EXTRACTPDF_OK);
        CHECK(form != NULL);
        CHECK(close_float(view.bbox.x0, 10.0f));
        CHECK(close_float(view.bbox.y0, 20.0f));
        CHECK(close_float(view.bbox.x1, 50.0f));
        CHECK(close_float(view.bbox.y1, 60.0f));
        CHECK(close_float(view.matrix.a, 2.0f));
        CHECK(close_float(view.matrix.d, 3.0f));
        CHECK(close_float(view.matrix.e, 5.0f));
        CHECK(close_float(view.matrix.f, 7.0f));
        CHECK(close_float(view.placement.a, 2.0f));
        CHECK(close_float(view.placement.b, 0.0f));
        CHECK(close_float(view.placement.c, 0.0f));
        CHECK(close_float(view.placement.d, 2.0f));
        CHECK(close_float(view.placement.e, 50.0f));
        CHECK(close_float(view.placement.f, 66.0f));
        extractpdf_pdf_appearance_drop_view(&view);

        memset(&view, 0, sizeof(view));
        form = NULL;
        status = extractpdf_pdf_appearance_resolve(
            ctx, positive, page_annot(ctx, positive, 4, 0), &view, &form);
        CHECK(status == EXTRACTPDF_OK);
        CHECK(view.stateful == 1);
        CHECK(view.state_name != NULL);
        CHECK(view.state_name_size == 2);
        CHECK(memcmp(view.state_name, "On", 2) == 0);
        CHECK(form != NULL);
        CHECK(pdf_to_num(ctx, form) == 22);
        extractpdf_pdf_appearance_drop_view(&view);

        expect_status(ctx, negative, 0, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 1, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 2, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 3, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 4, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 5, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 6, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 7, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 8, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 9, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 10, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 11, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 12, EXTRACTPDF_ERROR_UNSUPPORTED);
        expect_status(ctx, negative, 13, EXTRACTPDF_ERROR_FORMAT);
        expect_status(ctx, negative, 14, EXTRACTPDF_ERROR_FORMAT);
    }
    fz_catch(ctx)
    {
        caught = 1;
        fz_report_error(ctx);
    }

    pdf_drop_document(ctx, negative);
    pdf_drop_document(ctx, positive);
    fz_drop_context(ctx);
    CHECK(!caught);
    return 0;
}
