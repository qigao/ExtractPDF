#include "pdf_poster_internal.h"

#include <stdint.h>
#include <stdlib.h>

static void poster_discard_log(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static pdf_obj *poster_create_tile_page(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *source_page,
    const extractpdf_pdf_poster_split_plan *split,
    const extractpdf_pdf_poster_tile_plan *tile)
{
    pdf_obj *page_dict = NULL;
    pdf_obj *page_ref = NULL;
    pdf_obj *value;

    fz_var(page_dict);
    fz_var(page_ref);
    fz_try(ctx)
    {
        page_dict = pdf_new_dict(ctx, document, 10);
        pdf_dict_put(ctx, page_dict, PDF_NAME(Type), PDF_NAME(Page));
        pdf_dict_put_rect(ctx, page_dict, PDF_NAME(MediaBox), tile->pdf_rect);
        pdf_dict_put_rect(ctx, page_dict, PDF_NAME(CropBox), tile->pdf_rect);

        value = pdf_dict_get(ctx, source_page, PDF_NAME(Contents));
        if (value != NULL)
            pdf_dict_put(ctx, page_dict, PDF_NAME(Contents), value);
        value = pdf_dict_get_inheritable(ctx, source_page, PDF_NAME(Resources));
        if (value != NULL)
            pdf_dict_put(ctx, page_dict, PDF_NAME(Resources), value);
        value = pdf_dict_get(ctx, source_page, PDF_NAME(Group));
        if (value != NULL)
            pdf_dict_put(ctx, page_dict, PDF_NAME(Group), value);
        value = pdf_dict_gets(ctx, source_page, "Tabs");
        if (value != NULL)
            pdf_dict_puts(ctx, page_dict, "Tabs", value);

        if (split->page.rotate_degrees != 0)
            pdf_dict_put_int(
                ctx, page_dict, PDF_NAME(Rotate), split->page.rotate_degrees);
        value = pdf_dict_get(ctx, source_page, PDF_NAME(UserUnit));
        if (value != NULL && split->page.user_unit != 1.0f)
            pdf_dict_put_real(
                ctx, page_dict, PDF_NAME(UserUnit), split->page.user_unit);

        page_ref = pdf_add_object(ctx, document, page_dict);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, page_dict);
    }
    fz_catch(ctx)
    {
        pdf_drop_obj(ctx, page_ref);
        fz_rethrow(ctx);
    }
    return page_ref;
}

static void poster_drop_private_splits(
    fz_context *ctx,
    extractpdf_pdf_poster_private_split *runtime,
    size_t split_count)
{
    size_t split_index;

    if (runtime == NULL)
        return;
    for (split_index = 0; split_index < split_count; ++split_index) {
        size_t tile_index;
        for (tile_index = 0; tile_index < runtime[split_index].tile_count;
             ++tile_index)
            pdf_drop_obj(ctx, runtime[split_index].tile_pages[tile_index]);
        free(runtime[split_index].tile_pages);
        pdf_drop_obj(ctx, runtime[split_index].source_page);
    }
    free(runtime);
}

static extractpdf_status poster_build_private_tiles(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_poster_plan *plan,
    extractpdf_pdf_poster_private_split **out_runtime)
{
    extractpdf_pdf_poster_private_split *runtime;
    size_t split_index;
    int caught_code = FZ_ERROR_NONE;

    *out_runtime = NULL;
    if (plan->split_count > SIZE_MAX / sizeof(*runtime))
        return EXTRACTPDF_ERROR_NOMEM;
    runtime = (extractpdf_pdf_poster_private_split *)calloc(
        plan->split_count, sizeof(*runtime));
    if (runtime == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    fz_var(caught_code);
    fz_try(ctx)
    {
        for (split_index = 0; split_index < plan->split_count; ++split_index) {
            const extractpdf_pdf_poster_split_plan *split =
                &plan->splits[split_index];
            extractpdf_pdf_poster_private_split *private_split =
                &runtime[split_index];
            size_t tile_index;

            if (!split->changed)
                continue;
            private_split->source_page = pdf_keep_obj(
                ctx, pdf_lookup_page_obj(ctx, document, split->page_index));
            if (private_split->source_page == NULL)
                fz_throw(ctx, FZ_ERROR_FORMAT, "poster source page missing");
            if (split->tile_count > SIZE_MAX / sizeof(*private_split->tile_pages))
                fz_throw(ctx, FZ_ERROR_LIMIT, "poster tile count too large");
            private_split->tile_pages = (pdf_obj **)calloc(
                split->tile_count, sizeof(*private_split->tile_pages));
            if (private_split->tile_pages == NULL)
                fz_throw(ctx, FZ_ERROR_SYSTEM, "out of memory");
            private_split->tile_count = split->tile_count;

            for (tile_index = 0; tile_index < split->tile_count; ++tile_index) {
                private_split->tile_pages[tile_index] = poster_create_tile_page(
                    ctx,
                    document,
                    private_split->source_page,
                    split,
                    &split->tiles[tile_index]);
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        poster_drop_private_splits(ctx, runtime, plan->split_count);
        return extractpdf_status_from_mupdf(caught_code);
    }
    *out_runtime = runtime;
    return EXTRACTPDF_OK;
}

static extractpdf_status poster_splice_private_tiles(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_poster_plan *plan,
    extractpdf_pdf_poster_private_split *runtime)
{
    size_t reverse;
    int caught_code = FZ_ERROR_NONE;

    fz_var(caught_code);
    fz_try(ctx)
    {
        for (reverse = plan->split_count; reverse > 0; --reverse) {
            size_t split_index = reverse - 1;
            const extractpdf_pdf_poster_split_plan *split =
                &plan->splits[split_index];
            size_t tile_index;
            int source_index;

            if (!split->changed)
                continue;
            source_index = split->page_index;
            for (tile_index = 0; tile_index < split->tile_count; ++tile_index) {
                pdf_insert_page(
                    ctx,
                    document,
                    source_index + (int)tile_index,
                    runtime[split_index].tile_pages[tile_index]);
            }
            pdf_delete_page(
                ctx, document, source_index + (int)split->tile_count);
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return EXTRACTPDF_OK;
}

static extractpdf_status poster_transform_changed(
    fz_context *source_ctx,
    pdf_document *source_pdf,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    extractpdf_pdf_poster_plan *source_plan,
    extractpdf_output **out_output)
{
    extractpdf_output *seed = NULL;
    fz_context *private_ctx = NULL;
    fz_stream *stream = NULL;
    pdf_document *private_document = NULL;
    extractpdf_pdf_poster_plan *private_plan = NULL;
    extractpdf_pdf_poster_private_split *runtime = NULL;
    extractpdf_status status;
    int caught_code = FZ_ERROR_NONE;

    status = extractpdf_serialize_pdf(source_ctx, source_pdf, &seed);
    if (status != EXTRACTPDF_OK)
        return status;

    private_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (private_ctx == NULL) {
        extractpdf_drop_output(seed);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    fz_set_error_callback(private_ctx, poster_discard_log, NULL);
    fz_set_warning_callback(private_ctx, poster_discard_log, NULL);

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

    status = extractpdf_pdf_poster_check_security(private_ctx, private_document);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_build_plan(
        private_ctx,
        private_document,
        splits,
        split_count,
        1,
        &private_plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_annotations_preflight(
        private_ctx, private_document, private_plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_widget_provenance_preflight(
        private_ctx, private_document, private_plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_navigation_preflight(
        private_ctx, private_document, private_plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;

    source_plan->expansion_policy_applied = 1;
    private_plan->expansion_policy_applied = 1;
    if (!extractpdf_pdf_poster_plan_equivalent(source_plan, private_plan) ||
        !extractpdf_pdf_poster_annotation_plans_equivalent(
            source_plan, private_plan) ||
        !extractpdf_pdf_poster_navigation_plans_equivalent(
            source_plan, private_plan)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto cleanup;
    }

    status = poster_build_private_tiles(
        private_ctx, private_document, private_plan, &runtime);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_apply_navigation(
        private_ctx, private_document, private_plan, runtime);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_poster_apply_annotations(
        private_ctx, private_document, private_plan, runtime);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = poster_splice_private_tiles(
        private_ctx, private_document, private_plan, runtime);
    if (status != EXTRACTPDF_OK)
        goto cleanup;

    status = extractpdf_serialize_pdf(
        private_ctx, private_document, out_output);

cleanup:
    poster_drop_private_splits(
        private_ctx, runtime, private_plan != NULL ? private_plan->split_count : 0);
    extractpdf_pdf_poster_drop_annotation_plans(private_plan);
    extractpdf_pdf_poster_drop_plan(private_plan);
    pdf_drop_document(private_ctx, private_document);
    fz_drop_context(private_ctx);
    extractpdf_drop_output(seed);
    return status;
}

extractpdf_status extractpdf_poster_split_pages(
    extractpdf_document *document,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    extractpdf_output **out_output)
{
    pdf_document *source_pdf;
    extractpdf_pdf_poster_plan *plan = NULL;
    extractpdf_status status;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        splits == NULL || split_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_poster_check_security(document->ctx, source_pdf);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_poster_build_plan(
        document->ctx, source_pdf, splits, split_count, 0, &plan);
    if (status != EXTRACTPDF_OK)
        return status;

    if (!plan->any_changed) {
        status = extractpdf_serialize_pdf(document->ctx, source_pdf, out_output);
    } else {
        status = extractpdf_pdf_poster_annotations_preflight(
            document->ctx, source_pdf, plan);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_poster_widget_provenance_preflight(
                document->ctx, source_pdf, plan);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_poster_navigation_preflight(
                document->ctx, source_pdf, plan);
        if (status == EXTRACTPDF_OK) {
            plan->expansion_policy_applied = 1;
            status = poster_transform_changed(
                document->ctx,
                source_pdf,
                splits,
                split_count,
                plan,
                out_output);
        }
    }

    extractpdf_pdf_poster_drop_annotation_plans(plan);
    extractpdf_pdf_poster_drop_plan(plan);
    return status;
}
