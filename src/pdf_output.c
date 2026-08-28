#include "pdf_internal.h"

#include <stdlib.h>
#include <string.h>

static void extractpdf_drop_pdf_serialization_state(
    fz_context *ctx,
    fz_buffer *buffer,
    fz_output *memory_output)
{
    if (memory_output != NULL)
        fz_drop_output(ctx, memory_output);
    if (buffer != NULL)
        fz_drop_buffer(ctx, buffer);
}

extractpdf_status extractpdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_output **out_output)
{
    pdf_write_options options = pdf_default_write_options;
    fz_buffer *buffer = NULL;
    fz_output *memory_output = NULL;
    extractpdf_output *result = NULL;
    unsigned char *buffer_data = NULL;
    size_t buffer_size = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    options.reproducible = 1;
    options.dont_regenerate_id = 1;

    fz_var(buffer);
    fz_var(memory_output);
    fz_var(buffer_data);
    fz_var(buffer_size);
    fz_var(caught_code);

    fz_try(ctx)
    {
        buffer = fz_new_buffer(ctx, 0);
        memory_output = fz_new_output_with_buffer(ctx, buffer);
        pdf_write_document(ctx, document, memory_output, &options);
        fz_close_output(ctx, memory_output);
        buffer_size = fz_buffer_storage(ctx, buffer, &buffer_data);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_status status = extractpdf_status_from_mupdf(caught_code);
        extractpdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return status;
    }

    if (buffer_data == NULL || buffer_size == 0) {
        extractpdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return EXTRACTPDF_ERROR_MUPDF;
    }

    result = (extractpdf_output *)calloc(1, sizeof(*result));
    if (result == NULL) {
        extractpdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    result->data = (unsigned char *)malloc(buffer_size);
    if (result->data == NULL) {
        extractpdf_drop_pdf_serialization_state(ctx, buffer, memory_output);
        free(result);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    memcpy(result->data, buffer_data, buffer_size);
    result->size = buffer_size;
    extractpdf_drop_pdf_serialization_state(ctx, buffer, memory_output);

    *out_output = result;
    return EXTRACTPDF_OK;
}
