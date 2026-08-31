#include "pdf_internal.h"

#include <limits.h>
#include <stddef.h>

static void quantapdf_merge_discard_log(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static quantapdf_status quantapdf_merge_one_output(
    fz_context *ctx,
    pdf_document *destination,
    const quantapdf_output *input,
    int *total_page_count)
{
    fz_stream *stream = NULL;
    pdf_document *source = NULL;
    pdf_graft_map *graft = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    int source_page_count = 0;
    int new_total = *total_page_count;
    int caught_code = FZ_ERROR_NONE;
    int page;

    fz_var(stream);
    fz_var(source);
    fz_var(graft);
    fz_var(status);
    fz_var(source_page_count);
    fz_var(new_total);
    fz_var(caught_code);

    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, input->data, input->size);
        source = pdf_open_document_with_stream(ctx, stream);
        source_page_count = pdf_count_pages(ctx, source);

        if (source_page_count < 0) {
            status = QUANTAPDF_ERROR_BACKEND;
        } else if (source_page_count > INT_MAX - *total_page_count) {
            status = QUANTAPDF_ERROR_ARGUMENT;
        } else {
            new_total = *total_page_count + source_page_count;
            graft = pdf_new_graft_map(ctx, destination);
            for (page = 0; page < source_page_count; ++page)
                pdf_graft_mapped_page(ctx, graft, -1, source, page);
        }
    }
    fz_always(ctx)
    {
        if (graft != NULL)
            pdf_drop_graft_map(ctx, graft);
        if (source != NULL)
            pdf_drop_document(ctx, source);
        if (stream != NULL)
            fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    if (status != QUANTAPDF_OK)
        return status;

    *total_page_count = new_total;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output)
{
    fz_context *ctx = NULL;
    pdf_document *destination = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    int total_page_count = 0;
    int caught_code = FZ_ERROR_NONE;
    size_t i;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (inputs == NULL || input_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    for (i = 0; i < input_count; ++i) {
        if (inputs[i] == NULL)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    fz_set_error_callback(ctx, quantapdf_merge_discard_log, NULL);
    fz_set_warning_callback(ctx, quantapdf_merge_discard_log, NULL);

    fz_var(destination);
    fz_var(caught_code);

    fz_try(ctx)
    {
        destination = pdf_create_document(ctx);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        status = quantapdf_status_from_backend(caught_code);
        fz_drop_context(ctx);
        return status;
    }

    for (i = 0; i < input_count; ++i) {
        status = quantapdf_merge_one_output(
            ctx, destination, inputs[i], &total_page_count);
        if (status != QUANTAPDF_OK) {
            pdf_drop_document(ctx, destination);
            fz_drop_context(ctx);
            return status;
        }
    }

    status = quantapdf_serialize_pdf(ctx, destination, out_output);
    pdf_drop_document(ctx, destination);
    fz_drop_context(ctx);
    return status;
}
