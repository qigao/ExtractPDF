#include "pdf_poster_internal.h"

#include <math.h>
#include <stdlib.h>

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static int public_rect_equal(quantapdf_rect left, quantapdf_rect right)
{
    return close_float(left.x0, right.x0) &&
        close_float(left.y0, right.y0) &&
        close_float(left.x1, right.x1) &&
        close_float(left.y1, right.y1);
}

int quantapdf_pdf_poster_annotation_plans_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right)
{
    size_t split_index;

    if (left == NULL || right == NULL || left->split_count != right->split_count)
        return 0;
    for (split_index = 0; split_index < left->split_count; ++split_index) {
        const quantapdf_pdf_poster_split_plan *a = &left->splits[split_index];
        const quantapdf_pdf_poster_split_plan *b = &right->splits[split_index];
        size_t annot_index;

        if (a->annot_count != b->annot_count)
            return 0;
        for (annot_index = 0; annot_index < a->annot_count; ++annot_index) {
            const quantapdf_pdf_poster_annot_plan *x = &a->annots[annot_index];
            const quantapdf_pdf_poster_annot_plan *y = &b->annots[annot_index];
            size_t tile_index;

            if (x->source_annot_index != y->source_annot_index ||
                x->kind != y->kind || x->tile_count != y->tile_count ||
                x->form_field_index != y->form_field_index ||
                x->form_widget_index != y->form_widget_index ||
                !public_rect_equal(x->source_public_rect, y->source_public_rect))
                return 0;
            for (tile_index = 0; tile_index < x->tile_count; ++tile_index) {
                if (x->tile_indices[tile_index] != y->tile_indices[tile_index])
                    return 0;
            }
        }
    }
    return 1;
}

static quantapdf_rect intersect_public(
    quantapdf_rect left,
    quantapdf_rect right)
{
    quantapdf_rect result;
    result.x0 = fmaxf(left.x0, right.x0);
    result.y0 = fmaxf(left.y0, right.y0);
    result.x1 = fminf(left.x1, right.x1);
    result.y1 = fminf(left.y1, right.y1);
    return result;
}

static fz_rect public_to_raw(
    quantapdf_rect rect,
    fz_matrix pdf_to_public)
{
    fz_matrix public_to_pdf = fz_invert_matrix(pdf_to_public);
    fz_rect public_rect;
    fz_rect raw;

    public_rect.x0 = rect.x0;
    public_rect.y0 = rect.y0;
    public_rect.x1 = rect.x1;
    public_rect.y1 = rect.y1;
    raw = fz_transform_rect(public_rect, public_to_pdf);
    {
        fz_rect normalized;
        normalized.x0 = fminf(raw.x0, raw.x1);
        normalized.y0 = fminf(raw.y0, raw.y1);
        normalized.x1 = fmaxf(raw.x0, raw.x1);
        normalized.y1 = fmaxf(raw.y0, raw.y1);
        return normalized;
    }
}

static quantapdf_status update_annotation_page(
    fz_context *ctx,
    pdf_obj *annotation,
    pdf_obj *source_page,
    pdf_obj *tile_page,
    int force_page)
{
    pdf_obj *page = pdf_dict_get(ctx, annotation, PDF_NAME(P));

    if (page != NULL && pdf_objcmp_resolve(ctx, page, source_page) != 0)
        return QUANTAPDF_ERROR_FORMAT;
    if (page != NULL || force_page)
        pdf_dict_put(ctx, annotation, PDF_NAME(P), tile_page);
    return QUANTAPDF_OK;
}

static quantapdf_status append_link_instance(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_split_plan *split,
    const quantapdf_pdf_poster_annot_plan *annot_plan,
    pdf_obj *source_annotation,
    pdf_obj *source_page,
    pdf_obj **tile_pages,
    pdf_obj **tile_annots,
    size_t hit_index,
    int clone)
{
    size_t tile_index = annot_plan->tile_indices[hit_index];
    pdf_obj *annotation = NULL;
    pdf_obj *clone_dict = NULL;
    quantapdf_status status;

    if (clone) {
        clone_dict = pdf_copy_dict(ctx, source_annotation);
        annotation = pdf_add_object(ctx, document, clone_dict);
        pdf_drop_obj(ctx, clone_dict);
        clone_dict = NULL;
    } else {
        annotation = pdf_keep_obj(ctx, source_annotation);
    }
    if (annotation == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (annot_plan->tile_count > 1) {
        quantapdf_rect clipped = intersect_public(
            annot_plan->source_public_rect,
            split->tiles[tile_index].public_rect);
        fz_rect raw = public_to_raw(clipped, split->page.pdf_to_public);
        pdf_dict_put_rect(ctx, annotation, PDF_NAME(Rect), raw);
    }

    status = update_annotation_page(
        ctx,
        annotation,
        source_page,
        tile_pages[tile_index],
        clone);
    if (status != QUANTAPDF_OK) {
        pdf_drop_obj(ctx, annotation);
        return status;
    }
    pdf_array_push(ctx, tile_annots[tile_index], annotation);
    pdf_drop_obj(ctx, annotation);
    return QUANTAPDF_OK;
}

static quantapdf_status append_link(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_split_plan *split,
    const quantapdf_pdf_poster_annot_plan *annot_plan,
    pdf_obj *source_annotation,
    pdf_obj *source_page,
    pdf_obj **tile_pages,
    pdf_obj **tile_annots)
{
    size_t hit_index;
    quantapdf_status status;

    /*
     * Clone every later row-major intersection while source_annotation still
     * carries its original /P and /Rect. Only after all clones exist may the
     * original Link be moved to the first intersection.
     */
    for (hit_index = 1; hit_index < annot_plan->tile_count; ++hit_index) {
        status = append_link_instance(
            ctx,
            document,
            split,
            annot_plan,
            source_annotation,
            source_page,
            tile_pages,
            tile_annots,
            hit_index,
            1);
        if (status != QUANTAPDF_OK)
            return status;
    }

    return append_link_instance(
        ctx,
        document,
        split,
        annot_plan,
        source_annotation,
        source_page,
        tile_pages,
        tile_annots,
        0,
        0);
}

static quantapdf_status append_single_annotation(
    fz_context *ctx,
    const quantapdf_pdf_poster_annot_plan *annot_plan,
    pdf_obj *source_annotation,
    pdf_obj *source_page,
    pdf_obj **tile_pages,
    pdf_obj **tile_annots)
{
    size_t tile_index = annot_plan->tile_indices[0];
    quantapdf_status status = update_annotation_page(
        ctx,
        source_annotation,
        source_page,
        tile_pages[tile_index],
        0);

    if (status != QUANTAPDF_OK)
        return status;
    pdf_array_push(ctx, tile_annots[tile_index], source_annotation);
    return QUANTAPDF_OK;
}

static quantapdf_status apply_split_annotations(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_split_plan *split,
    quantapdf_pdf_poster_private_split *runtime)
{
    pdf_obj *source_annots = pdf_dict_get(
        ctx, runtime->source_page, PDF_NAME(Annots));
    pdf_obj **tile_annots = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    size_t tile_index;
    size_t annot_index;

    if (split->annot_count == 0)
        return QUANTAPDF_OK;
    if (!pdf_is_array(ctx, source_annots) ||
        (size_t)pdf_array_len(ctx, source_annots) != split->annot_count)
        return QUANTAPDF_ERROR_FORMAT;
    if (split->tile_count > SIZE_MAX / sizeof(*tile_annots))
        return QUANTAPDF_ERROR_NOMEM;

    tile_annots = (pdf_obj **)calloc(split->tile_count, sizeof(*tile_annots));
    if (tile_annots == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    fz_try(ctx)
    {
        for (tile_index = 0; tile_index < split->tile_count; ++tile_index)
            tile_annots[tile_index] = pdf_new_array(ctx, document, 4);

        for (annot_index = 0; annot_index < split->annot_count; ++annot_index) {
            const quantapdf_pdf_poster_annot_plan *annot_plan =
                &split->annots[annot_index];
            pdf_obj *source_annotation = pdf_array_get(
                ctx, source_annots, (int)annot_plan->source_annot_index);

            if (annot_plan->kind == QUANTAPDF_PDF_POSTER_ANNOT_LINK) {
                status = append_link(
                    ctx,
                    document,
                    split,
                    annot_plan,
                    source_annotation,
                    runtime->source_page,
                    runtime->tile_pages,
                    tile_annots);
            } else {
                status = append_single_annotation(
                    ctx,
                    annot_plan,
                    source_annotation,
                    runtime->source_page,
                    runtime->tile_pages,
                    tile_annots);
            }
            if (status != QUANTAPDF_OK)
                break;
        }

        if (status == QUANTAPDF_OK) {
            for (tile_index = 0; tile_index < split->tile_count; ++tile_index) {
                if (pdf_array_len(ctx, tile_annots[tile_index]) != 0) {
                    pdf_dict_put(
                        ctx,
                        runtime->tile_pages[tile_index],
                        PDF_NAME(Annots),
                        tile_annots[tile_index]);
                }
            }
        }
    }
    fz_always(ctx)
    {
        for (tile_index = 0; tile_index < split->tile_count; ++tile_index)
            pdf_drop_obj(ctx, tile_annots[tile_index]);
        free(tile_annots);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
    return status;
}

quantapdf_status quantapdf_pdf_poster_apply_annotations(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_plan *plan,
    quantapdf_pdf_poster_private_split *runtime)
{
    size_t split_index;
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        for (split_index = 0; split_index < plan->split_count; ++split_index) {
            if (!plan->splits[split_index].changed)
                continue;
            status = apply_split_annotations(
                ctx,
                document,
                &plan->splits[split_index],
                &runtime[split_index]);
            if (status != QUANTAPDF_OK)
                break;
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }
    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    return status;
}
