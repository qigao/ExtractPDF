#include "pdf_poster_internal.h"
#include "pdf_rewrite_security.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

quantapdf_status quantapdf_pdf_poster_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    return quantapdf_pdf_rewrite_check_security(ctx, document);
}

static int poster_split_compare(const void *left, const void *right)
{
    const quantapdf_page_poster_split *a =
        (const quantapdf_page_poster_split *)left;
    const quantapdf_page_poster_split *b =
        (const quantapdf_page_poster_split *)right;

    if (a->page_index < b->page_index)
        return -1;
    if (a->page_index > b->page_index)
        return 1;
    return 0;
}

static fz_rect poster_normalize_rect(fz_rect rect)
{
    fz_rect result;
    result.x0 = fminf(rect.x0, rect.x1);
    result.y0 = fminf(rect.y0, rect.y1);
    result.x1 = fmaxf(rect.x0, rect.x1);
    result.y1 = fmaxf(rect.y0, rect.y1);
    return result;
}

static int poster_positive_finite_rect(fz_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1) &&
        rect.x0 < rect.x1 && rect.y0 < rect.y1;
}

static int poster_rect_inside(fz_rect inner, fz_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static quantapdf_status poster_build_edges(
    float start,
    float end,
    size_t count,
    float **out_edges)
{
    float *edges;
    size_t index;

    *out_edges = NULL;
    if (count == SIZE_MAX || count + 1 > SIZE_MAX / sizeof(*edges))
        return QUANTAPDF_ERROR_ARGUMENT;

    edges = (float *)malloc((count + 1) * sizeof(*edges));
    if (edges == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    edges[0] = start;
    edges[count] = end;
    for (index = 1; index < count; ++index) {
        double fraction = (double)index / (double)count;
        double value = (double)start +
            ((double)end - (double)start) * fraction;
        edges[index] = (float)value;
        if (!isfinite(edges[index])) {
            free(edges);
            return QUANTAPDF_ERROR_ARGUMENT;
        }
    }

    for (index = 0; index < count; ++index) {
        if (!(edges[index] < edges[index + 1])) {
            free(edges);
            return QUANTAPDF_ERROR_ARGUMENT;
        }
    }

    *out_edges = edges;
    return QUANTAPDF_OK;
}

void quantapdf_pdf_poster_drop_plan(quantapdf_pdf_poster_plan *plan)
{
    size_t index;

    if (plan == NULL)
        return;
    for (index = 0; index < plan->split_count; ++index) {
        free(plan->splits[index].tiles);
        free(plan->splits[index].x_edges);
        free(plan->splits[index].y_edges);
    }
    free(plan->splits);
    free(plan);
}

static quantapdf_status poster_build_plan_imp(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_poster_split *requests,
    size_t split_count,
    int expansion_policy,
    quantapdf_pdf_poster_plan **out_plan)
{
    const size_t minimum_size = QUANTAPDF_PAGE_POSTER_SPLIT_V1_MIN_SIZE;
    const size_t element_size = QUANTAPDF_PAGE_POSTER_SPLIT_V1_SIZE;
    quantapdf_page_poster_split *sorted = NULL;
    quantapdf_pdf_poster_plan *plan = NULL;
    size_t output_count;
    size_t index;
    quantapdf_status status = QUANTAPDF_OK;

    (void)expansion_policy;
    *out_plan = NULL;

    if (split_count > SIZE_MAX / sizeof(*sorted) ||
        split_count > SIZE_MAX / sizeof(*plan->splits))
        return QUANTAPDF_ERROR_NOMEM;

    sorted = (quantapdf_page_poster_split *)calloc(split_count, sizeof(*sorted));
    plan = (quantapdf_pdf_poster_plan *)calloc(1, sizeof(*plan));
    if (sorted == NULL || plan == NULL) {
        free(sorted);
        free(plan);
        return QUANTAPDF_ERROR_NOMEM;
    }

    plan->splits = (quantapdf_pdf_poster_split_plan *)calloc(
        split_count, sizeof(*plan->splits));
    if (plan->splits == NULL) {
        free(sorted);
        free(plan);
        return QUANTAPDF_ERROR_NOMEM;
    }
    plan->split_count = split_count;
    plan->source_page_count = pdf_count_pages(ctx, document);
    if (plan->source_page_count < 0) {
        status = QUANTAPDF_ERROR_FORMAT;
        goto cleanup;
    }
    output_count = (size_t)plan->source_page_count;

    for (index = 0; index < split_count; ++index) {
        if (requests[index].struct_size < minimum_size ||
            requests[index].struct_size > element_size ||
            requests[index].page_index < 0 ||
            requests[index].page_index >= plan->source_page_count ||
            requests[index].columns == 0 || requests[index].rows == 0) {
            status = QUANTAPDF_ERROR_ARGUMENT;
            goto cleanup;
        }
        if (requests[index].columns > SIZE_MAX / requests[index].rows) {
            status = QUANTAPDF_ERROR_ARGUMENT;
            goto cleanup;
        }
        sorted[index] = requests[index];
    }

    qsort(sorted, split_count, sizeof(*sorted), poster_split_compare);
    for (index = 1; index < split_count; ++index) {
        if (sorted[index - 1].page_index == sorted[index].page_index) {
            status = QUANTAPDF_ERROR_ARGUMENT;
            goto cleanup;
        }
    }

    for (index = 0; index < split_count; ++index) {
        quantapdf_pdf_poster_split_plan *split = &plan->splits[index];
        fz_matrix public_to_pdf;
        size_t tile_count = sorted[index].columns * sorted[index].rows;
        size_t row;
        size_t column;
        size_t tile_index = 0;

        if (tile_count > (size_t)INT_MAX ||
            output_count > SIZE_MAX - (tile_count - 1) ||
            output_count + (tile_count - 1) > (size_t)INT_MAX) {
            status = QUANTAPDF_ERROR_ARGUMENT;
            goto cleanup;
        }
        output_count += tile_count - 1;

        split->page_index = sorted[index].page_index;
        split->columns = sorted[index].columns;
        split->rows = sorted[index].rows;
        split->tile_count = tile_count;
        split->changed = tile_count > 1;
        if (split->changed)
            plan->any_changed = 1;

        status = quantapdf_pdf_page_box_resolve(
            ctx, document, split->page_index, &split->page);
        if (status != QUANTAPDF_OK)
            goto cleanup;

        status = poster_build_edges(
            split->page.visible_public.x0,
            split->page.visible_public.x1,
            split->columns,
            &split->x_edges);
        if (status != QUANTAPDF_OK)
            goto cleanup;
        status = poster_build_edges(
            split->page.visible_public.y0,
            split->page.visible_public.y1,
            split->rows,
            &split->y_edges);
        if (status != QUANTAPDF_OK)
            goto cleanup;

        if (tile_count > SIZE_MAX / sizeof(*split->tiles)) {
            status = QUANTAPDF_ERROR_NOMEM;
            goto cleanup;
        }
        split->tiles = (quantapdf_pdf_poster_tile_plan *)calloc(
            tile_count, sizeof(*split->tiles));
        if (split->tiles == NULL) {
            status = QUANTAPDF_ERROR_NOMEM;
            goto cleanup;
        }

        public_to_pdf = fz_invert_matrix(split->page.pdf_to_public);
        for (row = 0; row < split->rows; ++row) {
            for (column = 0; column < split->columns; ++column) {
                quantapdf_pdf_poster_tile_plan *tile =
                    &split->tiles[tile_index];
                fz_rect public_rect;

                tile->row = row;
                tile->column = column;
                tile->tile_index = tile_index;
                tile->public_rect.x0 = split->x_edges[column];
                tile->public_rect.y0 = split->y_edges[row];
                tile->public_rect.x1 = split->x_edges[column + 1];
                tile->public_rect.y1 = split->y_edges[row + 1];

                public_rect.x0 = tile->public_rect.x0;
                public_rect.y0 = tile->public_rect.y0;
                public_rect.x1 = tile->public_rect.x1;
                public_rect.y1 = tile->public_rect.y1;
                tile->pdf_rect = poster_normalize_rect(
                    fz_transform_rect(public_rect, public_to_pdf));
                if (!poster_positive_finite_rect(tile->pdf_rect) ||
                    !poster_rect_inside(tile->pdf_rect, split->page.visible_pdf)) {
                    status = QUANTAPDF_ERROR_ARGUMENT;
                    goto cleanup;
                }
                ++tile_index;
            }
        }
    }

    plan->output_page_count = (int)output_count;
    free(sorted);
    *out_plan = plan;
    return QUANTAPDF_OK;

cleanup:
    free(sorted);
    quantapdf_pdf_poster_drop_plan(plan);
    return status;
}

quantapdf_status quantapdf_pdf_poster_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    int expansion_policy,
    quantapdf_pdf_poster_plan **out_plan)
{
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (out_plan != NULL)
        *out_plan = NULL;
    if (ctx == NULL || document == NULL || splits == NULL ||
        split_count == 0 || out_plan == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = poster_build_plan_imp(
            ctx, document, splits, split_count, expansion_policy, out_plan);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_pdf_poster_drop_plan(*out_plan);
        *out_plan = NULL;
        return quantapdf_status_from_mupdf(caught_code);
    }
    return status;
}

static int poster_close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static int poster_rect_equivalent(fz_rect left, fz_rect right)
{
    return poster_close_float(left.x0, right.x0) &&
        poster_close_float(left.y0, right.y0) &&
        poster_close_float(left.x1, right.x1) &&
        poster_close_float(left.y1, right.y1);
}

static int poster_public_rect_equivalent(
    quantapdf_rect left, quantapdf_rect right)
{
    return poster_close_float(left.x0, right.x0) &&
        poster_close_float(left.y0, right.y0) &&
        poster_close_float(left.x1, right.x1) &&
        poster_close_float(left.y1, right.y1);
}

static int poster_matrix_equivalent(fz_matrix left, fz_matrix right)
{
    return poster_close_float(left.a, right.a) &&
        poster_close_float(left.b, right.b) &&
        poster_close_float(left.c, right.c) &&
        poster_close_float(left.d, right.d) &&
        poster_close_float(left.e, right.e) &&
        poster_close_float(left.f, right.f);
}

int quantapdf_pdf_poster_plan_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right)
{
    size_t index;

    if (left == NULL || right == NULL ||
        left->split_count != right->split_count ||
        left->source_page_count != right->source_page_count ||
        left->output_page_count != right->output_page_count ||
        left->any_changed != right->any_changed)
        return 0;

    for (index = 0; index < left->split_count; ++index) {
        const quantapdf_pdf_poster_split_plan *a = &left->splits[index];
        const quantapdf_pdf_poster_split_plan *b = &right->splits[index];
        size_t edge;
        size_t tile;

        if (a->page_index != b->page_index ||
            a->columns != b->columns || a->rows != b->rows ||
            a->tile_count != b->tile_count || a->changed != b->changed ||
            a->page.has_explicit_crop != b->page.has_explicit_crop ||
            a->page.rotate_degrees != b->page.rotate_degrees ||
            !poster_close_float(a->page.user_unit, b->page.user_unit) ||
            !poster_rect_equivalent(a->page.media_pdf, b->page.media_pdf) ||
            !poster_rect_equivalent(a->page.crop_pdf, b->page.crop_pdf) ||
            !poster_rect_equivalent(a->page.visible_pdf, b->page.visible_pdf) ||
            !poster_public_rect_equivalent(
                a->page.visible_public, b->page.visible_public) ||
            !poster_matrix_equivalent(
                a->page.pdf_to_public, b->page.pdf_to_public))
            return 0;

        for (edge = 0; edge <= a->columns; ++edge) {
            if (!poster_close_float(a->x_edges[edge], b->x_edges[edge]))
                return 0;
        }
        for (edge = 0; edge <= a->rows; ++edge) {
            if (!poster_close_float(a->y_edges[edge], b->y_edges[edge]))
                return 0;
        }
        for (tile = 0; tile < a->tile_count; ++tile) {
            if (a->tiles[tile].row != b->tiles[tile].row ||
                a->tiles[tile].column != b->tiles[tile].column ||
                a->tiles[tile].tile_index != b->tiles[tile].tile_index ||
                !poster_public_rect_equivalent(
                    a->tiles[tile].public_rect, b->tiles[tile].public_rect) ||
                !poster_rect_equivalent(
                    a->tiles[tile].pdf_rect, b->tiles[tile].pdf_rect))
                return 0;
        }
    }
    return 1;
}
