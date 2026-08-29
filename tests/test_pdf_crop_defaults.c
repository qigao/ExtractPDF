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

static int dict_has_local_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key)
{
    int count;
    int index;

    if (!pdf_is_dict(ctx, dictionary))
        return 0;
    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key))
            return 1;
    }
    return 0;
}

int crop_raw_expect_no_local_default_boxes(
    const unsigned char *data,
    size_t size,
    int page_index)
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

        document = open_pdf_bytes(ctx, data, size);
        page = pdf_lookup_page_obj(ctx, document, page_index);
        ok = pdf_is_dict(ctx, page) &&
            !dict_has_local_key(ctx, page, PDF_NAME(BleedBox)) &&
            !dict_has_local_key(ctx, page, PDF_NAME(TrimBox)) &&
            !dict_has_local_key(ctx, page, PDF_NAME(ArtBox));
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}
