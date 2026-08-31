#include "pdf_internal.h"

#include <limits.h>
#include <stdlib.h>

static void quantapdf_drop_pdf_export_state(
    fz_context *ctx,
    pdf_document *destination,
    pdf_graft_map *graft)
{
    if (graft != NULL)
        pdf_drop_graft_map(ctx, graft);
    if (destination != NULL)
        pdf_drop_document(ctx, destination);
}

quantapdf_status quantapdf_export_pages(
    quantapdf_document *document,
    const int *page_indices,
    size_t page_count,
    quantapdf_output **out_output)
{
    fz_context *ctx;
    pdf_document *source_pdf;
    pdf_document *destination = NULL;
    pdf_graft_map *graft = NULL;
    int source_page_count = 0;
    int caught_code = FZ_ERROR_NONE;
    size_t i;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || page_indices == NULL || page_count == 0 ||
        page_count > (size_t)INT_MAX)
        return QUANTAPDF_ERROR_ARGUMENT;

    ctx = document->ctx;
    source_pdf = pdf_specifics(ctx, document->doc);
    if (source_pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    fz_var(source_page_count);
    fz_var(caught_code);

    fz_try(ctx)
    {
        source_page_count = pdf_count_pages(ctx, source_pdf);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);

    for (i = 0; i < page_count; ++i) {
        if (page_indices[i] < 0 || page_indices[i] >= source_page_count)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    caught_code = FZ_ERROR_NONE;

    fz_var(destination);
    fz_var(graft);
    fz_var(caught_code);

    fz_try(ctx)
    {
        destination = pdf_create_document(ctx);
        graft = pdf_new_graft_map(ctx, destination);

        for (i = 0; i < page_count; ++i)
            pdf_graft_mapped_page(ctx, graft, -1, source_pdf, page_indices[i]);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status status = quantapdf_status_from_backend(caught_code);
        quantapdf_drop_pdf_export_state(ctx, destination, graft);
        return status;
    }

    {
        quantapdf_status status = quantapdf_serialize_pdf(
            ctx, destination, out_output);
        quantapdf_drop_pdf_export_state(ctx, destination, graft);
        return status;
    }
}

quantapdf_status quantapdf_output_data(
    const quantapdf_output *output,
    const unsigned char **out_data,
    size_t *out_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (output == NULL || out_data == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_data = output->data;
    *out_size = output->size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_output(quantapdf_output *output)
{
    if (output == NULL)
        return;

    free(output->data);
    free(output);
}
