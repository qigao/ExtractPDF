#include "pdf_poster_internal.h"

#include "pdf_outline_common.h"

#include <math.h>

static extractpdf_pdf_poster_split_plan *poster_nav_find_split(
    extractpdf_pdf_poster_plan *plan,
    int page_index)
{
    size_t index;
    for (index = 0; index < plan->split_count; ++index) {
        if (plan->splits[index].page_index == page_index)
            return &plan->splits[index];
    }
    return NULL;
}

static extractpdf_status poster_validate_local_destination(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan,
    pdf_obj *destination)
{
    pdf_obj *page_operand;
    pdf_obj *kind;
    int page_index;
    extractpdf_pdf_poster_split_plan *split;

    if (pdf_is_name(ctx, destination) || pdf_is_string(ctx, destination))
        return EXTRACTPDF_OK;
    if (!pdf_is_array(ctx, destination) || pdf_array_len(ctx, destination) < 2)
        return EXTRACTPDF_ERROR_FORMAT;

    page_operand = pdf_array_get(ctx, destination, 0);
    if (!pdf_is_indirect(ctx, page_operand) || !pdf_is_dict(ctx, page_operand) ||
        !pdf_name_eq(
            ctx, pdf_dict_get(ctx, page_operand, PDF_NAME(Type)), PDF_NAME(Page)))
        return EXTRACTPDF_ERROR_FORMAT;
    page_index = pdf_lookup_page_number(ctx, document, page_operand);
    if (page_index < 0 || page_index >= plan->source_page_count)
        return EXTRACTPDF_ERROR_FORMAT;

    kind = pdf_array_get(ctx, destination, 1);
    if (!pdf_is_name(ctx, kind))
        return EXTRACTPDF_ERROR_FORMAT;

    split = poster_nav_find_split(plan, page_index);
    if (split == NULL || !split->changed)
        return EXTRACTPDF_OK;

    if (!pdf_name_eq(ctx, kind, PDF_NAME(XYZ)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (pdf_array_len(ctx, destination) < 5 ||
        !pdf_is_number(ctx, pdf_array_get(ctx, destination, 2)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, destination, 3)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    {
        float x = pdf_to_real(ctx, pdf_array_get(ctx, destination, 2));
        float y = pdf_to_real(ctx, pdf_array_get(ctx, destination, 3));
        float px;
        float py;
        size_t row;
        size_t column;
        int found_row = 0;
        int found_column = 0;

        if (!isfinite(x) || !isfinite(y))
            return EXTRACTPDF_ERROR_FORMAT;
        px = split->page.pdf_to_public.a * x +
            split->page.pdf_to_public.c * y + split->page.pdf_to_public.e;
        py = split->page.pdf_to_public.b * x +
            split->page.pdf_to_public.d * y + split->page.pdf_to_public.f;
        if (!isfinite(px) || !isfinite(py) ||
            px < split->page.visible_public.x0 ||
            px > split->page.visible_public.x1 ||
            py < split->page.visible_public.y0 ||
            py > split->page.visible_public.y1)
            return EXTRACTPDF_ERROR_UNSUPPORTED;

        for (column = 0; column < split->columns; ++column) {
            if (px >= split->x_edges[column] &&
                (px < split->x_edges[column + 1] ||
                 (column + 1 == split->columns &&
                  px <= split->x_edges[column + 1]))) {
                found_column = 1;
                break;
            }
        }
        for (row = 0; row < split->rows; ++row) {
            if (py >= split->y_edges[row] &&
                (py < split->y_edges[row + 1] ||
                 (row + 1 == split->rows &&
                  py <= split->y_edges[row + 1]))) {
                found_row = 1;
                break;
            }
        }
        if (!found_column || !found_row)
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    return EXTRACTPDF_OK;
}

static extractpdf_status poster_outline_visit(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *item,
    size_t preorder_index,
    void *user)
{
    extractpdf_pdf_poster_plan *plan = (extractpdf_pdf_poster_plan *)user;
    pdf_obj *dest = pdf_dict_get(ctx, item, PDF_NAME(Dest));
    pdf_obj *action = pdf_dict_get(ctx, item, PDF_NAME(A));

    (void)preorder_index;
    if (dest != NULL && action != NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (dest != NULL)
        return poster_validate_local_destination(ctx, document, plan, dest);
    if (action == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, action))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_dict_get(ctx, action, PDF_NAME(Next)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    {
        pdf_obj *kind = pdf_dict_get(ctx, action, PDF_NAME(S));
        if (!pdf_is_name(ctx, kind))
            return EXTRACTPDF_ERROR_FORMAT;
        if (pdf_name_eq(ctx, kind, PDF_NAME(URI)))
            return EXTRACTPDF_OK;
        if (pdf_name_eq(ctx, kind, PDF_NAME(GoTo))) {
            pdf_obj *target = pdf_dict_get(ctx, action, PDF_NAME(D));
            if (target == NULL)
                return EXTRACTPDF_ERROR_FORMAT;
            return poster_validate_local_destination(
                ctx, document, plan, target);
        }
    }
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

static extractpdf_status poster_scan_link_destinations(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan)
{
    int page_count = pdf_count_pages(ctx, document);
    int page_index;

    for (page_index = 0; page_index < page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        int count;
        int index;

        if (annots == NULL)
            continue;
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        count = pdf_array_len(ctx, annots);
        for (index = 0; index < count; ++index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, index);
            pdf_obj *subtype;
            pdf_obj *dest;
            pdf_obj *action;

            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation))
                return EXTRACTPDF_ERROR_FORMAT;
            subtype = pdf_dict_get(ctx, annotation, PDF_NAME(Subtype));
            if (!pdf_is_name(ctx, subtype))
                return EXTRACTPDF_ERROR_FORMAT;
            if (!pdf_name_eq(ctx, subtype, PDF_NAME(Link)))
                continue;

            dest = pdf_dict_get(ctx, annotation, PDF_NAME(Dest));
            action = pdf_dict_get(ctx, annotation, PDF_NAME(A));
            if (dest != NULL)
                return poster_validate_local_destination(
                    ctx, document, plan, dest);
            if (action != NULL && pdf_is_dict(ctx, action) &&
                pdf_name_eq(
                    ctx, pdf_dict_get(ctx, action, PDF_NAME(S)), PDF_NAME(GoTo))) {
                pdf_obj *target = pdf_dict_get(ctx, action, PDF_NAME(D));
                if (target == NULL)
                    return EXTRACTPDF_ERROR_FORMAT;
                return poster_validate_local_destination(
                    ctx, document, plan, target);
            }
        }
    }
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_poster_navigation_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan)
{
    pdf_obj *root;
    size_t outline_count = 0;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
    if (!pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;

    if (pdf_dict_get(ctx, root, PDF_NAME(OpenAction)) != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(AA)) != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(PageLabels)) != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(Threads)) != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(StructTreeRoot)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = poster_scan_link_destinations(ctx, document, plan);
    if (status != EXTRACTPDF_OK)
        return status;

    return extractpdf_pdf_outline_walk_strict(
        ctx, document, poster_outline_visit, plan, &outline_count);
}
