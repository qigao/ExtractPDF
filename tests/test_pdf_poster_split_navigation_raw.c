#include "test_pdf_poster_split_internal.h"

#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <string.h>

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static pdf_document *open_pdf_bytes(
    fz_context *ctx,
    const unsigned char *data,
    size_t size)
{
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    fz_var(stream);
    fz_var(document);
    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, data, size);
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

static int destination_matches(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *destination,
    int expected_page,
    float expected_x,
    float expected_y,
    float expected_zoom)
{
    pdf_obj *page;
    pdf_obj *kind;

    if (!pdf_is_array(ctx, destination) || pdf_array_len(ctx, destination) < 5)
        return 0;
    page = pdf_array_get(ctx, destination, 0);
    kind = pdf_array_get(ctx, destination, 1);
    if (!pdf_is_indirect(ctx, page) ||
        pdf_lookup_page_number(ctx, document, page) != expected_page ||
        !pdf_name_eq(ctx, kind, PDF_NAME(XYZ)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, destination, 2)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, destination, 3)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, destination, 4)))
        return 0;
    return close_float(pdf_to_real(ctx, pdf_array_get(ctx, destination, 2)), expected_x) &&
        close_float(pdf_to_real(ctx, pdf_array_get(ctx, destination, 3)), expected_y) &&
        close_float(pdf_to_real(ctx, pdf_array_get(ctx, destination, 4)), expected_zoom);
}

static pdf_obj *destination_value(fz_context *ctx, pdf_obj *value)
{
    if (pdf_is_array(ctx, value))
        return value;
    if (pdf_is_dict(ctx, value))
        return pdf_dict_get(ctx, value, PDF_NAME(D));
    return NULL;
}

int poster_raw_check_navigation(
    const unsigned char *data,
    size_t size)
{
    fz_context *ctx = NULL;
    pdf_document *document = NULL;
    int ok = 0;

    if (data == NULL || size == 0)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(document);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *root;
        pdf_obj *page4;
        pdf_obj *annots;
        pdf_obj *annotation;
        pdf_obj *action;
        pdf_obj *outlines;
        pdf_obj *first;
        pdf_obj *second;
        pdf_obj *names;
        pdf_obj *name_values;
        pdf_obj *legacy;

        document = open_pdf_bytes(ctx, data, size);
        if (pdf_count_pages(ctx, document) != 5)
            goto done;
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        if (!pdf_is_dict(ctx, root))
            goto done;

        page4 = pdf_lookup_page_obj(ctx, document, 4);
        annots = pdf_dict_get(ctx, page4, PDF_NAME(Annots));
        if (!pdf_is_array(ctx, annots) || pdf_array_len(ctx, annots) != 5)
            goto done;

        annotation = pdf_array_get(ctx, annots, 0);
        if (!destination_matches(
                ctx, document, pdf_dict_get(ctx, annotation, PDF_NAME(Dest)),
                0, 50.0f, 225.0f, 1.0f))
            goto done;

        annotation = pdf_array_get(ctx, annots, 1);
        action = pdf_dict_get(ctx, annotation, PDF_NAME(A));
        if (!pdf_is_dict(ctx, action) ||
            !destination_matches(
                ctx, document, pdf_dict_get(ctx, action, PDF_NAME(D)),
                1, 250.0f, 225.0f, 1.0f))
            goto done;

        annotation = pdf_array_get(ctx, annots, 2);
        if (!pdf_is_string(ctx, pdf_dict_get(ctx, annotation, PDF_NAME(Dest))) ||
            strcmp(
                pdf_to_text_string(
                    ctx, pdf_dict_get(ctx, annotation, PDF_NAME(Dest))),
                "named.one") != 0)
            goto done;

        annotation = pdf_array_get(ctx, annots, 3);
        if (!pdf_is_name(ctx, pdf_dict_get(ctx, annotation, PDF_NAME(Dest))) ||
            strcmp(
                pdf_to_name(ctx, pdf_dict_get(ctx, annotation, PDF_NAME(Dest))),
                "legacy.one") != 0)
            goto done;

        annotation = pdf_array_get(ctx, annots, 4);
        if (!destination_matches(
                ctx, document, pdf_dict_get(ctx, annotation, PDF_NAME(Dest)),
                1, 200.0f, 225.0f, 1.0f))
            goto done;

        outlines = pdf_dict_get(ctx, root, PDF_NAME(Outlines));
        first = pdf_dict_get(ctx, outlines, PDF_NAME(First));
        second = pdf_dict_get(ctx, first, PDF_NAME(Next));
        if (!destination_matches(
                ctx, document, pdf_dict_get(ctx, first, PDF_NAME(Dest)),
                2, 50.0f, 75.0f, 1.0f))
            goto done;
        action = pdf_dict_get(ctx, second, PDF_NAME(A));
        if (!pdf_is_dict(ctx, action) ||
            !destination_matches(
                ctx, document, pdf_dict_get(ctx, action, PDF_NAME(D)),
                3, 250.0f, 75.0f, 1.0f))
            goto done;

        names = pdf_dict_get(ctx, root, PDF_NAME(Names));
        names = pdf_dict_get(ctx, names, PDF_NAME(Dests));
        name_values = pdf_dict_get(ctx, names, PDF_NAME(Names));
        if (!pdf_is_array(ctx, name_values) || pdf_array_len(ctx, name_values) != 4)
            goto done;
        if (!destination_matches(
                ctx, document, destination_value(ctx, pdf_array_get(ctx, name_values, 1)),
                3, 250.0f, 75.0f, 1.0f) ||
            !destination_matches(
                ctx, document, destination_value(ctx, pdf_array_get(ctx, name_values, 3)),
                2, 50.0f, 75.0f, 1.0f))
            goto done;

        legacy = pdf_dict_get(ctx, root, PDF_NAME(Dests));
        if (!pdf_is_dict(ctx, legacy) ||
            !destination_matches(
                ctx, document, destination_value(ctx, pdf_dict_gets(ctx, legacy, "legacy.one")),
                0, 50.0f, 225.0f, 1.0f) ||
            !destination_matches(
                ctx, document, destination_value(ctx, pdf_dict_gets(ctx, legacy, "legacy.dict")),
                1, 250.0f, 225.0f, 1.0f))
            goto done;

        ok = 1;
done:
        ;
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}
