#include "pdf_trim_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static void quantapdf_pdf_trim_discard_log(
    void *user,
    const char *message)
{
    (void)user;
    (void)message;
}

static int quantapdf_pdf_trim_close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static int quantapdf_pdf_trim_rect_equivalent(fz_rect left, fz_rect right)
{
    return quantapdf_pdf_trim_close_float(left.x0, right.x0) &&
        quantapdf_pdf_trim_close_float(left.y0, right.y0) &&
        quantapdf_pdf_trim_close_float(left.x1, right.x1) &&
        quantapdf_pdf_trim_close_float(left.y1, right.y1);
}

static int quantapdf_pdf_trim_plan_equivalent(
    const quantapdf_pdf_trim_plan *left,
    const quantapdf_pdf_trim_plan *right)
{
    return left->page_index == right->page_index &&
        left->requested_public.x0 == right->requested_public.x0 &&
        left->requested_public.y0 == right->requested_public.y0 &&
        left->requested_public.x1 == right->requested_public.x1 &&
        left->requested_public.y1 == right->requested_public.y1 &&
        quantapdf_pdf_trim_rect_equivalent(
            left->requested_media_pdf, right->requested_media_pdf) &&
        quantapdf_pdf_trim_rect_equivalent(
            left->output_visible_pdf, right->output_visible_pdf) &&
        left->changed == right->changed &&
        left->frame_changed == right->frame_changed;
}

static int quantapdf_pdf_trim_view_equivalent(
    const quantapdf_pdf_page_box_view *left,
    const quantapdf_pdf_page_box_view *right)
{
    return quantapdf_pdf_trim_rect_equivalent(
               left->media_pdf, right->media_pdf) &&
        quantapdf_pdf_trim_rect_equivalent(
            left->crop_pdf, right->crop_pdf) &&
        quantapdf_pdf_trim_rect_equivalent(
            left->visible_pdf, right->visible_pdf) &&
        left->has_explicit_crop == right->has_explicit_crop &&
        left->rotate_degrees == right->rotate_degrees &&
        quantapdf_pdf_trim_close_float(left->user_unit, right->user_unit);
}

static void quantapdf_pdf_trim_put_mediabox(
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
        pdf_dict_put(ctx, page_obj, PDF_NAME(MediaBox), array);
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

static quantapdf_status quantapdf_pdf_trim_transform_changed(
    fz_context *source_ctx,
    pdf_document *source_pdf,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    const quantapdf_pdf_trim_plan *source_plans,
    quantapdf_output **out_output)
{
    quantapdf_output *seed = NULL;
    quantapdf_pdf_trim_plan *private_plans = NULL;
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
        private_ctx, quantapdf_pdf_trim_discard_log, NULL);
    fz_set_warning_callback(
        private_ctx, quantapdf_pdf_trim_discard_log, NULL);

    fz_var(private_plans);
    fz_var(private_views);
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
        status = quantapdf_status_from_backend(caught_code);
        pdf_drop_document(private_ctx, private_document);
        fz_drop_context(private_ctx);
        quantapdf_drop_output(seed);
        return status;
    }

    status = quantapdf_pdf_trim_check_security(
        private_ctx, private_document);
    if (status != QUANTAPDF_OK)
        goto cleanup;

    if (trim_count > SIZE_MAX / sizeof(*private_plans) ||
        trim_count > SIZE_MAX / sizeof(*private_views)) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    private_plans = (quantapdf_pdf_trim_plan *)calloc(
        trim_count, sizeof(*private_plans));
    private_views = (quantapdf_pdf_page_box_view *)calloc(
        trim_count, sizeof(*private_views));
    if (private_plans == NULL || private_views == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }

    status = quantapdf_pdf_trim_build_plan(
        private_ctx,
        private_document,
        trims,
        trim_count,
        private_plans,
        &private_any_changed);
    if (status != QUANTAPDF_OK)
        goto cleanup;
    if (!private_any_changed) {
        status = QUANTAPDF_ERROR_FORMAT;
        goto cleanup;
    }

    for (index = 0; index < trim_count; ++index) {
        quantapdf_pdf_page_box_view source_view;

        if (!quantapdf_pdf_trim_plan_equivalent(
                &source_plans[index], &private_plans[index])) {
            status = QUANTAPDF_ERROR_FORMAT;
            goto cleanup;
        }

        status = quantapdf_pdf_page_box_resolve(
            source_ctx,
            source_pdf,
            source_plans[index].page_index,
            &source_view);
        if (status != QUANTAPDF_OK)
            goto cleanup;
        status = quantapdf_pdf_page_box_resolve(
            private_ctx,
            private_document,
            private_plans[index].page_index,
            &private_views[index]);
        if (status != QUANTAPDF_OK)
            goto cleanup;
        if (!quantapdf_pdf_trim_view_equivalent(
                &source_view, &private_views[index])) {
            status = QUANTAPDF_ERROR_FORMAT;
            goto cleanup;
        }
    }

    caught_code = FZ_ERROR_NONE;
    fz_var(caught_code);
    fz_try(private_ctx)
    {
        for (index = 0; index < trim_count; ++index) {
            if (!private_plans[index].changed)
                continue;
            quantapdf_pdf_trim_put_mediabox(
                private_ctx,
                private_document,
                private_views[index].page_obj,
                private_plans[index].requested_media_pdf);
        }
    }
    fz_catch(private_ctx)
    {
        caught_code = fz_caught(private_ctx);
        fz_report_error(private_ctx);
    }
    if (caught_code != FZ_ERROR_NONE) {
        status = quantapdf_status_from_backend(caught_code);
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

quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output)
{
    pdf_document *source_pdf;
    quantapdf_pdf_trim_plan *plans = NULL;
    quantapdf_status status;
    int any_changed = 0;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        trims == NULL || trim_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    status = quantapdf_pdf_trim_check_security(document->ctx, source_pdf);
    if (status != QUANTAPDF_OK)
        return status;

    if (trim_count > SIZE_MAX / sizeof(*plans))
        return QUANTAPDF_ERROR_NOMEM;
    plans = (quantapdf_pdf_trim_plan *)calloc(trim_count, sizeof(*plans));
    if (plans == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    status = quantapdf_pdf_trim_build_plan(
        document->ctx,
        source_pdf,
        trims,
        trim_count,
        plans,
        &any_changed);
    if (status == QUANTAPDF_OK) {
        if (any_changed) {
            status = quantapdf_pdf_trim_transform_changed(
                document->ctx,
                source_pdf,
                trims,
                trim_count,
                plans,
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
