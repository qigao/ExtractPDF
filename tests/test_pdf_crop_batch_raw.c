#include "test_pdf_crop_internal.h"

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

static int local_value(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int index;
    int count;

    *out_value = NULL;
    if (!pdf_is_dict(ctx, dictionary))
        return 0;
    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key)) {
            *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

static int local_key_equal(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right,
    pdf_obj *key)
{
    pdf_obj *left_value = NULL;
    pdf_obj *right_value = NULL;
    int left_present = local_value(ctx, left, key, &left_value);
    int right_present = local_value(ctx, right, key, &right_value);

    if (left_present != right_present)
        return 0;
    if (!left_present)
        return 1;
    return pdf_objcmp_deep(ctx, left_value, right_value) == 0;
}

int crop_raw_expect_page_local_geometry_equal(
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
        pdf_obj *left;
        pdf_obj *right;
        static pdf_obj *keys[] = {
            PDF_NAME(MediaBox), PDF_NAME(CropBox), PDF_NAME(Rotate),
            PDF_NAME(UserUnit), PDF_NAME(BleedBox), PDF_NAME(TrimBox),
            PDF_NAME(ArtBox), NULL
        };
        int index;

        before_doc = open_pdf_bytes(ctx, before, before_size);
        after_doc = open_pdf_bytes(ctx, after, after_size);
        left = pdf_lookup_page_obj(ctx, before_doc, page_index);
        right = pdf_lookup_page_obj(ctx, after_doc, page_index);
        ok = pdf_is_dict(ctx, left) && pdf_is_dict(ctx, right);
        for (index = 0; ok && keys[index] != NULL; ++index)
            ok = local_key_equal(ctx, left, right, keys[index]);
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
