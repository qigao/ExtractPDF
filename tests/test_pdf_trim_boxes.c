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

static int local_value(
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
            fabsf(pdf_to_real(ctx, item) - expected[index]) >= 0.001f)
            return 0;
    }
    return 1;
}

static int key_matches(
    fz_context *ctx,
    pdf_obj *page,
    pdf_obj *key,
    int expect_present,
    const float expected[4])
{
    pdf_obj *value = NULL;
    int present = local_value(ctx, page, key, &value);

    if (present != expect_present)
        return 0;
    if (!expect_present)
        return 1;
    return box_matches(ctx, value, expected);
}

int trim_raw_expect_production_boxes(
    const unsigned char *data,
    size_t size,
    int page_index,
    int expect_bleed,
    const float bleed[4],
    int expect_trim,
    const float trim[4],
    int expect_art,
    const float art[4])
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
        ok = key_matches(
                 ctx, page, PDF_NAME(BleedBox), expect_bleed, bleed) &&
            key_matches(
                 ctx, page, PDF_NAME(TrimBox), expect_trim, trim) &&
            key_matches(
                 ctx, page, PDF_NAME(ArtBox), expect_art, art);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}
