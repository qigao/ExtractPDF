#include "pdf_flatten_internal.h"

#include <stdint.h>

static void flatten_discard_log(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static extractpdf_status flatten_transform_changed(
    fz_context *source_ctx,
    pdf_document *source_pdf,
    uint32_t flags,
    const extractpdf_pdf_flatten_plan *source_plan,
    extractpdf_output **out_output)
{
    extractpdf_output *seed = NULL;
    fz_context *private_ctx = NULL;
    fz_stream *stream = NULL;
    pdf_document *private_document = NULL;
    extractpdf_pdf_flatten_plan *private_plan = NULL;
    extractpdf_status status;
    int caught_code = FZ_ERROR_NONE;

    (void)out_output;

    status = extractpdf_serialize_pdf(source_ctx, source_pdf, &seed);
    if (status != EXTRACTPDF_OK)
        return status;

    private_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (private_ctx == NULL) {
        extractpdf_drop_output(seed);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    fz_set_error_callback(private_ctx, flatten_discard_log, NULL);
    fz_set_warning_callback(private_ctx, flatten_discard_log, NULL);

    fz_var(stream);
    fz_var(private_document);
    fz_var(caught_code);
    fz_try(private_ctx)
    {
        stream = fz_open_memory(private_ctx, seed->data, seed->size);
        private_document = pdf_open_document_with_stream(private_ctx, stream);
        pdf_disable_js(private_ctx, private_document);
    }
    fz_always(private_ctx)
    {
        fz_drop_stream(private_ctx, stream);
        stream = NULL;
    }
    fz_catch(private_ctx)
    {
        caught_code = fz_caught(private_ctx);
        fz_report_error(private_ctx);
    }
    if (caught_code != FZ_ERROR_NONE) {
        status = extractpdf_status_from_mupdf(caught_code);
        goto cleanup;
    }

    status = extractpdf_pdf_flatten_check_security(
        private_ctx, private_document);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_flatten_build_plan(
        private_ctx, private_document, flags, &private_plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    if (!extractpdf_pdf_flatten_plan_equivalent(source_plan, private_plan)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto cleanup;
    }

    status = EXTRACTPDF_ERROR_STATE;

cleanup:
    extractpdf_pdf_flatten_drop_plan(private_plan);
    pdf_drop_document(private_ctx, private_document);
    fz_drop_context(private_ctx);
    extractpdf_drop_output(seed);
    return status;
}

extractpdf_status extractpdf_flatten_interactive(
    extractpdf_document *document,
    uint32_t flags,
    extractpdf_output **out_output)
{
    const uint32_t known =
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS;
    pdf_document *source_pdf;
    extractpdf_pdf_flatten_plan *source_plan = NULL;
    extractpdf_status status;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        flags == 0 || (flags & ~known) != 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_flatten_check_security(document->ctx, source_pdf);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_flatten_build_plan(
        document->ctx, source_pdf, flags, &source_plan);
    if (status != EXTRACTPDF_OK)
        return status;

    if (!source_plan->any_changed) {
        status = extractpdf_serialize_pdf(
            document->ctx, source_pdf, out_output);
    } else {
        status = flatten_transform_changed(
            document->ctx,
            source_pdf,
            flags,
            source_plan,
            out_output);
    }

    extractpdf_pdf_flatten_drop_plan(source_plan);
    return status;
}
