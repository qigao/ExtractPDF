#include "pdf_crop_internal.h"

#include <stdint.h>
#include <stdlib.h>

static void quantapdf_pdf_crop_discard_log(
    void *user,
    const char *message)
{
    (void)user;
    (void)message;
}

static void quantapdf_pdf_crop_put_box(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *page_obj,
    fz_rect box)
{
    pdf_obj *array = NULL;

    fz_var(array);
    fz_try(ctx)
    {
        array = pdf_new_array(ctx, document, 4);
        pdf_array_push_real(ctx, array, box.x0);
        pdf_array_push_real(ctx, array, box.y0);
        pdf_array_push_real(ctx, array, box.x1);
        pdf_array_push_real(ctx, array, box.y1);
        pdf_dict_put(ctx, page_obj, PDF_NAME(CropBox), array);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, array);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}

static quantapdf_status quantapdf_pdf_crop_transform_changed(
    fz_context *source_ctx,
    pdf_document *source_pdf,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output)
{
    quantapdf_output *seed = NULL;
    quantapdf_pdf_crop_plan *private_plans = NULL;
    quantapdf_pdf_page_box_view *private_views = NULL;
    fz_context *private_ctx = NULL;
    fz_stream *stream = NULL;
    pdf_document *private_document = NULL;
    quantapdf_status status;
    int private_any_changed = 0;
    int caught_code = FZ_ERROR_NONE;
    size_t index;

    status = quantapdf_serialize_pdf(source_ctx, source_pdf, &seed);
    if (status != QUANTAPDF_OK)
        return status;

    private_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (private_ctx == NULL) {
        quantapdf_drop_output(seed);
        return QUANTAPDF_ERROR_NOMEM;
    }
    fz_set_error_callback(
        private_ctx, quantapdf_pdf_crop_discard_log, NULL);
    fz_set_warning_callback(
        private_ctx, quantapdf_pdf_crop_discard_log, NULL);

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
        status = quantapdf_status_from_mupdf(caught_code);
        pdf_drop_document(private_ctx, private_document);
        fz_drop_context(private_ctx);
        quantapdf_drop_output(seed);
        return status;
    }

    status = quantapdf_pdf_crop_check_security(
        private_ctx, private_document);
    if (status != QUANTAPDF_OK)
        goto cleanup;

    if (crop_count > SIZE_MAX / sizeof(*private_plans) ||
        crop_count > SIZE_MAX / sizeof(*private_views)) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    private_plans = (quantapdf_pdf_crop_plan *)calloc(
        crop_count, sizeof(*private_plans));
    private_views = (quantapdf_pdf_page_box_view *)calloc(
        crop_count, sizeof(*private_views));
    if (private_plans == NULL || private_views == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }

    status = quantapdf_pdf_crop_build_plan(
        private_ctx,
        private_document,
        crops,
        crop_count,
        private_plans,
        &private_any_changed);
    if (status != QUANTAPDF_OK)
        goto cleanup;
    if (!private_any_changed) {
        status = QUANTAPDF_ERROR_FORMAT;
        goto cleanup;
    }

    for (index = 0; index < crop_count; ++index) {
        status = quantapdf_pdf_page_box_resolve(
            private_ctx,
            private_document,
            private_plans[index].page_index,
            &private_views[index]);
        if (status != QUANTAPDF_OK)
            goto cleanup;
    }

    caught_code = FZ_ERROR_NONE;
    fz_var(caught_code);
    fz_try(private_ctx)
    {
        for (index = 0; index < crop_count; ++index) {
            if (!private_plans[index].changed)
                continue;
            quantapdf_pdf_crop_put_box(
                private_ctx,
                private_document,
                private_views[index].page_obj,
                private_plans[index].requested_pdf);
        }
    }
    fz_catch(private_ctx)
    {
        caught_code = fz_caught(private_ctx);
        fz_report_error(private_ctx);
    }
    if (caught_code != FZ_ERROR_NONE) {
        status = quantapdf_status_from_mupdf(caught_code);
        goto cleanup;
    }

    status = quantapdf_serialize_pdf(
        private_ctx, private_document, out_output);

cleanup:
    free(private_views);
    free(private_plans);
    pdf_drop_document(private_ctx, private_document);
    fz_drop_context(private_ctx);
    quantapdf_drop_output(seed);
    return status;
}

quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output)
{
    pdf_document *source_pdf;
    quantapdf_pdf_crop_plan *plans = NULL;
    quantapdf_status status;
    int any_changed = 0;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        crops == NULL || crop_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    status = quantapdf_pdf_crop_check_security(document->ctx, source_pdf);
    if (status != QUANTAPDF_OK)
        return status;

    if (crop_count > SIZE_MAX / sizeof(*plans))
        return QUANTAPDF_ERROR_NOMEM;
    plans = (quantapdf_pdf_crop_plan *)calloc(crop_count, sizeof(*plans));
    if (plans == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    status = quantapdf_pdf_crop_build_plan(
        document->ctx,
        source_pdf,
        crops,
        crop_count,
        plans,
        &any_changed);
    if (status == QUANTAPDF_OK) {
        if (any_changed) {
            status = quantapdf_pdf_crop_transform_changed(
                document->ctx,
                source_pdf,
                crops,
                crop_count,
                out_output);
        }
        else {
            status = quantapdf_serialize_pdf(
                document->ctx, source_pdf, out_output);
        }
    }

    free(plans);
    return status;
}
