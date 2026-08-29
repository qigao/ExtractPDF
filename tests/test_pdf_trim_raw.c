#include "test_pdf_trim_internal.h"

#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <stddef.h>

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

static int dict_has_local_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count;
    int index;

    if (out_value != NULL)
        *out_value = NULL;
    if (!pdf_is_dict(ctx, dictionary))
        return 0;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key)) {
            if (out_value != NULL)
                *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static int box_matches(
    fz_context *ctx,
    pdf_obj *box,
    const float expected[4])
{
    int index;

    if (expected == NULL || !pdf_is_array(ctx, box) ||
        pdf_array_len(ctx, box) != 4)
        return 0;
    for (index = 0; index < 4; ++index) {
        pdf_obj *item = pdf_array_get(ctx, box, index);
        if (!pdf_is_number(ctx, item) ||
            !close_float(pdf_to_real(ctx, item), expected[index]))
            return 0;
    }
    return 1;
}

int trim_raw_expect_local_mediabox(
    const unsigned char *data,
    size_t size,
    int page_index,
    int expect_present,
    const float expected[4])
{
    fz_context *ctx = NULL;
    pdf_document *document = NULL;
    int ok = 0;

    if (data == NULL || size == 0 || page_index < 0)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(document);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *page;
        pdf_obj *box = NULL;
        int present;

        document = open_pdf_bytes(ctx, data, size);
        page = pdf_lookup_page_obj(ctx, document, page_index);
        present = dict_has_local_key(ctx, page, PDF_NAME(MediaBox), &box);
        if (present != expect_present)
            ok = 0;
        else if (!expect_present)
            ok = 1;
        else
            ok = box_matches(ctx, box, expected);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}

static int obj_equal_deep_or_both_missing(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    if (left == NULL || pdf_is_null(ctx, left))
        return right == NULL || pdf_is_null(ctx, right);
    if (right == NULL || pdf_is_null(ctx, right))
        return 0;
    return pdf_objcmp_deep(ctx, left, right) == 0;
}

int trim_raw_expect_preserved_cropbox(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size,
    int page_index)
{
    fz_context *ctx = NULL;
    pdf_document *before_doc = NULL;
    pdf_document *after_doc = NULL;
    int ok = 0;

    if (before == NULL || before_size == 0 || after == NULL ||
        after_size == 0 || page_index < 0)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(before_doc);
    fz_var(after_doc);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *before_page;
        pdf_obj *after_page;
        pdf_obj *before_crop;
        pdf_obj *after_crop;
        int before_local;
        int after_local;

        before_doc = open_pdf_bytes(ctx, before, before_size);
        after_doc = open_pdf_bytes(ctx, after, after_size);
        before_page = pdf_lookup_page_obj(ctx, before_doc, page_index);
        after_page = pdf_lookup_page_obj(ctx, after_doc, page_index);
        before_local = dict_has_local_key(
            ctx, before_page, PDF_NAME(CropBox), NULL);
        after_local = dict_has_local_key(
            ctx, after_page, PDF_NAME(CropBox), NULL);
        before_crop = pdf_dict_get_inheritable(
            ctx, before_page, PDF_NAME(CropBox));
        after_crop = pdf_dict_get_inheritable(
            ctx, after_page, PDF_NAME(CropBox));
        ok = before_local == after_local &&
            obj_equal_deep_or_both_missing(ctx, before_crop, after_crop);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, after_doc);
    pdf_drop_document(ctx, before_doc);
    fz_drop_context(ctx);
    return ok;
}

static int compare_page_payload(
    fz_context *ctx,
    pdf_document *before,
    pdf_document *after,
    int page_index)
{
    pdf_obj *left = pdf_lookup_page_obj(ctx, before, page_index);
    pdf_obj *right = pdf_lookup_page_obj(ctx, after, page_index);

    if (!pdf_is_dict(ctx, left) || !pdf_is_dict(ctx, right))
        return 0;
    if (!obj_equal_deep_or_both_missing(
            ctx,
            pdf_dict_get(ctx, left, PDF_NAME(Contents)),
            pdf_dict_get(ctx, right, PDF_NAME(Contents))))
        return 0;
    if (!obj_equal_deep_or_both_missing(
            ctx,
            pdf_dict_get(ctx, left, PDF_NAME(Resources)),
            pdf_dict_get(ctx, right, PDF_NAME(Resources))))
        return 0;

    {
        pdf_obj *left_annots = pdf_dict_get(ctx, left, PDF_NAME(Annots));
        pdf_obj *right_annots = pdf_dict_get(ctx, right, PDF_NAME(Annots));
        int left_count = pdf_array_len(ctx, left_annots);
        int right_count = pdf_array_len(ctx, right_annots);
        int index;

        if (left_count != right_count)
            return 0;
        for (index = 0; index < left_count; ++index) {
            pdf_obj *left_annot = pdf_array_get(ctx, left_annots, index);
            pdf_obj *right_annot = pdf_array_get(ctx, right_annots, index);
            static pdf_obj *keys[] = {
                PDF_NAME(Subtype), PDF_NAME(Rect), PDF_NAME(F),
                PDF_NAME(Contents), PDF_NAME(A), PDF_NAME(Dest),
                PDF_NAME(AS), NULL
            };
            int key_index;

            if (!pdf_is_dict(ctx, left_annot) || !pdf_is_dict(ctx, right_annot))
                return 0;
            for (key_index = 0; keys[key_index] != NULL; ++key_index) {
                if (!obj_equal_deep_or_both_missing(
                        ctx,
                        pdf_dict_get(ctx, left_annot, keys[key_index]),
                        pdf_dict_get(ctx, right_annot, keys[key_index])))
                    return 0;
            }
        }
    }
    return 1;
}

static int compare_root_semantics(
    fz_context *ctx,
    pdf_document *before,
    pdf_document *after)
{
    pdf_obj *left_root = pdf_dict_get(
        ctx, pdf_trailer(ctx, before), PDF_NAME(Root));
    pdf_obj *right_root = pdf_dict_get(
        ctx, pdf_trailer(ctx, after), PDF_NAME(Root));
    pdf_obj *left_acroform;
    pdf_obj *right_acroform;
    pdf_obj *left_outlines;
    pdf_obj *right_outlines;

    if (!pdf_is_dict(ctx, left_root) || !pdf_is_dict(ctx, right_root))
        return 0;

    left_acroform = pdf_dict_get(ctx, left_root, PDF_NAME(AcroForm));
    right_acroform = pdf_dict_get(ctx, right_root, PDF_NAME(AcroForm));
    if (!obj_equal_deep_or_both_missing(ctx, left_acroform, right_acroform)) {
        if ((left_acroform == NULL) != (right_acroform == NULL))
            return 0;
        if (left_acroform != NULL) {
            pdf_obj *left_fields = pdf_dict_get(
                ctx, left_acroform, PDF_NAME(Fields));
            pdf_obj *right_fields = pdf_dict_get(
                ctx, right_acroform, PDF_NAME(Fields));
            if (pdf_array_len(ctx, left_fields) !=
                pdf_array_len(ctx, right_fields))
                return 0;
        }
    }

    left_outlines = pdf_dict_get(ctx, left_root, PDF_NAME(Outlines));
    right_outlines = pdf_dict_get(ctx, right_root, PDF_NAME(Outlines));
    if ((left_outlines == NULL) != (right_outlines == NULL))
        return 0;
    if (left_outlines != NULL) {
        pdf_obj *left_first = pdf_dict_get(
            ctx, left_outlines, PDF_NAME(First));
        pdf_obj *right_first = pdf_dict_get(
            ctx, right_outlines, PDF_NAME(First));
        static pdf_obj *keys[] = {
            PDF_NAME(Title), PDF_NAME(Dest), PDF_NAME(A), NULL
        };
        int index;

        if (!pdf_is_dict(ctx, left_first) || !pdf_is_dict(ctx, right_first))
            return 0;
        for (index = 0; keys[index] != NULL; ++index) {
            if (!obj_equal_deep_or_both_missing(
                    ctx,
                    pdf_dict_get(ctx, left_first, keys[index]),
                    pdf_dict_get(ctx, right_first, keys[index])))
                return 0;
        }
    }
    return 1;
}

int trim_raw_expect_preserved_graph(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size)
{
    fz_context *ctx = NULL;
    pdf_document *before_doc = NULL;
    pdf_document *after_doc = NULL;
    int ok = 0;

    if (before == NULL || before_size == 0 || after == NULL || after_size == 0)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(before_doc);
    fz_var(after_doc);
    fz_var(ok);
    fz_try(ctx)
    {
        int page_count;
        int index;

        before_doc = open_pdf_bytes(ctx, before, before_size);
        after_doc = open_pdf_bytes(ctx, after, after_size);
        page_count = pdf_count_pages(ctx, before_doc);
        ok = page_count == pdf_count_pages(ctx, after_doc);
        for (index = 0; ok && index < page_count; ++index)
            ok = compare_page_payload(ctx, before_doc, after_doc, index);
        if (ok)
            ok = compare_root_semantics(ctx, before_doc, after_doc);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, after_doc);
    pdf_drop_document(ctx, before_doc);
    fz_drop_context(ctx);
    return ok;
}
