#include "pdf_poster_internal.h"

#include "pdf_annotation_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int has_key(fz_context *ctx, pdf_obj *dict, pdf_obj *key)
{
    return pdf_dict_get(ctx, dict, key) != NULL;
}

static int name_is(fz_context *ctx, pdf_obj *obj, const char *name)
{
    const char *value;
    if (!pdf_is_name(ctx, obj))
        return 0;
    value = pdf_to_name(ctx, obj);
    return value != NULL && strcmp(value, name) == 0;
}

static int public_inside(extractpdf_rect inner, extractpdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static extractpdf_status read_public_rect(
    fz_context *ctx,
    pdf_obj *annotation,
    fz_matrix pdf_to_public,
    extractpdf_rect *out_rect)
{
    pdf_obj *rect = pdf_dict_get(ctx, annotation, PDF_NAME(Rect));
    fz_rect raw;
    fz_rect mapped;
    float v[4];
    int i;

    if (!pdf_is_array(ctx, rect) || pdf_array_len(ctx, rect) != 4)
        return EXTRACTPDF_ERROR_FORMAT;
    for (i = 0; i < 4; ++i) {
        pdf_obj *item = pdf_array_get(ctx, rect, i);
        if (!pdf_is_number(ctx, item))
            return EXTRACTPDF_ERROR_FORMAT;
        v[i] = pdf_to_real(ctx, item);
        if (!isfinite(v[i]))
            return EXTRACTPDF_ERROR_FORMAT;
    }
    raw.x0 = fminf(v[0], v[2]);
    raw.y0 = fminf(v[1], v[3]);
    raw.x1 = fmaxf(v[0], v[2]);
    raw.y1 = fmaxf(v[1], v[3]);
    if (!(raw.x0 < raw.x1) || !(raw.y0 < raw.y1))
        return EXTRACTPDF_ERROR_FORMAT;
    mapped = fz_transform_rect(raw, pdf_to_public);
    out_rect->x0 = fminf(mapped.x0, mapped.x1);
    out_rect->y0 = fminf(mapped.y0, mapped.y1);
    out_rect->x1 = fmaxf(mapped.x0, mapped.x1);
    out_rect->y1 = fmaxf(mapped.y0, mapped.y1);
    if (!isfinite(out_rect->x0) || !isfinite(out_rect->y0) ||
        !isfinite(out_rect->x1) || !isfinite(out_rect->y1) ||
        !(out_rect->x0 < out_rect->x1) || !(out_rect->y0 < out_rect->y1))
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_poster_collect_rect_tiles(
    const extractpdf_pdf_poster_split_plan *split,
    extractpdf_rect rect,
    size_t **out_tile_indices,
    size_t *out_tile_count,
    int *out_crosses)
{
    size_t count = 0;
    size_t index;
    size_t *indices;

    if (out_tile_indices != NULL)
        *out_tile_indices = NULL;
    if (out_tile_count != NULL)
        *out_tile_count = 0;
    if (out_crosses != NULL)
        *out_crosses = 0;
    if (split == NULL || out_tile_indices == NULL || out_tile_count == NULL ||
        out_crosses == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    for (index = 0; index < split->tile_count; ++index) {
        extractpdf_rect tile = split->tiles[index].public_rect;
        if (fmaxf(rect.x0, tile.x0) < fminf(rect.x1, tile.x1) &&
            fmaxf(rect.y0, tile.y0) < fminf(rect.y1, tile.y1))
            ++count;
    }
    if (count == 0)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (count > SIZE_MAX / sizeof(*indices))
        return EXTRACTPDF_ERROR_NOMEM;
    indices = (size_t *)malloc(count * sizeof(*indices));
    if (indices == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    count = 0;
    for (index = 0; index < split->tile_count; ++index) {
        extractpdf_rect tile = split->tiles[index].public_rect;
        if (fmaxf(rect.x0, tile.x0) < fminf(rect.x1, tile.x1) &&
            fmaxf(rect.y0, tile.y0) < fminf(rect.y1, tile.y1))
            indices[count++] = index;
    }
    *out_tile_indices = indices;
    *out_tile_count = count;
    *out_crosses = count > 1;
    return EXTRACTPDF_OK;
}

static extractpdf_pdf_poster_split_plan *find_split(
    extractpdf_pdf_poster_plan *plan,
    int page_index)
{
    size_t i;
    for (i = 0; i < plan->split_count; ++i) {
        if (plan->splits[i].page_index == page_index)
            return &plan->splits[i];
    }
    return NULL;
}

static extractpdf_status check_link_action(fz_context *ctx, pdf_obj *annotation)
{
    pdf_obj *dest = pdf_dict_get(ctx, annotation, PDF_NAME(Dest));
    pdf_obj *action = pdf_dict_get(ctx, annotation, PDF_NAME(A));
    pdf_obj *kind;

    if (has_key(ctx, annotation, PDF_NAME(AA)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (dest != NULL && action != NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (action == NULL)
        return dest != NULL ? EXTRACTPDF_OK : EXTRACTPDF_ERROR_UNSUPPORTED;
    if (!pdf_is_dict(ctx, action))
        return EXTRACTPDF_ERROR_FORMAT;
    if (has_key(ctx, action, PDF_NAME(Next)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    kind = pdf_dict_get(ctx, action, PDF_NAME(S));
    if (!pdf_is_name(ctx, kind))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_name_eq(ctx, kind, PDF_NAME(URI)))
        return EXTRACTPDF_OK;
    if (pdf_name_eq(ctx, kind, PDF_NAME(GoTo)))
        return pdf_dict_get(ctx, action, PDF_NAME(D)) != NULL ?
            EXTRACTPDF_OK : EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

static int page_key_allowed(fz_context *ctx, pdf_obj *key)
{
    return pdf_name_eq(ctx, key, PDF_NAME(Type)) ||
        pdf_name_eq(ctx, key, PDF_NAME(Parent)) ||
        pdf_name_eq(ctx, key, PDF_NAME(MediaBox)) ||
        pdf_name_eq(ctx, key, PDF_NAME(CropBox)) ||
        pdf_name_eq(ctx, key, PDF_NAME(Resources)) ||
        pdf_name_eq(ctx, key, PDF_NAME(Contents)) ||
        pdf_name_eq(ctx, key, PDF_NAME(Rotate)) ||
        pdf_name_eq(ctx, key, PDF_NAME(UserUnit)) ||
        pdf_name_eq(ctx, key, PDF_NAME(Group)) ||
        name_is(ctx, key, "Tabs") ||
        pdf_name_eq(ctx, key, PDF_NAME(Annots));
}

static extractpdf_status check_selected_page(
    fz_context *ctx,
    extractpdf_pdf_poster_split_plan *split)
{
    pdf_obj *page = split->page.page_obj;
    int count;
    int i;

    if (pdf_dict_get_inheritable(ctx, page, PDF_NAME(BleedBox)) != NULL ||
        pdf_dict_get_inheritable(ctx, page, PDF_NAME(TrimBox)) != NULL ||
        pdf_dict_get_inheritable(ctx, page, PDF_NAME(ArtBox)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    count = pdf_dict_len(ctx, page);
    for (i = 0; i < count; ++i) {
        if (!page_key_allowed(ctx, pdf_dict_get_key(ctx, page, i)))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status check_form_actions(
    fz_context *ctx,
    pdf_document *document)
{
    pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
    pdf_obj *form;
    pdf_obj *fields;
    pdf_obj **stack = NULL;
    size_t count = 0;
    size_t capacity;
    pdf_mark_bits *marks = NULL;
    extractpdf_status status = EXTRACTPDF_OK;
    int i;

    if (!pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    form = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
    if (form == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, form))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_dict_get(ctx, form, PDF_NAME(XFA)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    fields = pdf_dict_get(ctx, form, PDF_NAME(Fields));
    if (fields == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_array(ctx, fields))
        return EXTRACTPDF_ERROR_FORMAT;

    capacity = (size_t)pdf_array_len(ctx, fields) + 16;
    if (capacity < 16)
        capacity = 16;
    if (capacity > SIZE_MAX / sizeof(*stack))
        return EXTRACTPDF_ERROR_NOMEM;
    stack = (pdf_obj **)malloc(capacity * sizeof(*stack));
    if (stack == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    marks = pdf_new_mark_bits(ctx, document);
    for (i = 0; i < pdf_array_len(ctx, fields); ++i)
        stack[count++] = pdf_array_get(ctx, fields, i);

    while (status == EXTRACTPDF_OK && count != 0) {
        pdf_obj *node = stack[--count];
        pdf_obj *kids;
        int kid_count;
        int kid;

        if (!pdf_is_indirect(ctx, node) || !pdf_is_dict(ctx, node)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        if (pdf_mark_bits_set(ctx, marks, node)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        if (has_key(ctx, node, PDF_NAME(A)) || has_key(ctx, node, PDF_NAME(AA))) {
            status = EXTRACTPDF_ERROR_UNSUPPORTED;
            break;
        }
        kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
        if (kids == NULL)
            continue;
        if (!pdf_is_array(ctx, kids)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        kid_count = pdf_array_len(ctx, kids);
        if (count + (size_t)kid_count > capacity) {
            size_t needed = count + (size_t)kid_count;
            size_t grown_capacity = capacity;
            pdf_obj **grown;
            while (grown_capacity < needed) {
                if (grown_capacity > SIZE_MAX / 2) {
                    grown_capacity = needed;
                    break;
                }
                grown_capacity *= 2;
            }
            if (grown_capacity > SIZE_MAX / sizeof(*stack)) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }
            grown = (pdf_obj **)realloc(stack, grown_capacity * sizeof(*stack));
            if (grown == NULL) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }
            stack = grown;
            capacity = grown_capacity;
        }
        for (kid = 0; kid < kid_count; ++kid)
            stack[count++] = pdf_array_get(ctx, kids, kid);
    }

    pdf_drop_mark_bits(ctx, marks);
    free(stack);
    return status;
}

extractpdf_status extractpdf_pdf_poster_annotations_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan)
{
    int page_count;
    int page_index;
    size_t split_index;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    for (split_index = 0; split_index < plan->split_count; ++split_index) {
        if (plan->splits[split_index].changed) {
            status = check_selected_page(ctx, &plan->splits[split_index]);
            if (status != EXTRACTPDF_OK)
                return status;
        }
    }
    status = check_form_actions(ctx, document);
    if (status != EXTRACTPDF_OK)
        return status;

    page_count = pdf_count_pages(ctx, document);
    for (page_index = 0; page_index < page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots;
        extractpdf_pdf_poster_split_plan *split = find_split(plan, page_index);
        int annot_count;
        int annot_index;

        if (!pdf_is_dict(ctx, page))
            return EXTRACTPDF_ERROR_FORMAT;
        if (has_key(ctx, page, PDF_NAME(AA)))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        if (annots == NULL)
            continue;
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        annot_count = pdf_array_len(ctx, annots);
        if (split != NULL && split->changed && annot_count != 0) {
            if ((size_t)annot_count > SIZE_MAX / sizeof(*split->annots))
                return EXTRACTPDF_ERROR_NOMEM;
            split->annots = (extractpdf_pdf_poster_annot_plan *)calloc(
                (size_t)annot_count, sizeof(*split->annots));
            if (split->annots == NULL)
                return EXTRACTPDF_ERROR_NOMEM;
            split->annot_count = (size_t)annot_count;
        }

        for (annot_index = 0; annot_index < annot_count; ++annot_index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
            pdf_obj *subtype;
            int is_link;
            int is_widget;

            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation))
                return EXTRACTPDF_ERROR_FORMAT;
            subtype = pdf_dict_get(ctx, annotation, PDF_NAME(Subtype));
            if (!pdf_is_name(ctx, subtype))
                return EXTRACTPDF_ERROR_FORMAT;
            is_link = pdf_name_eq(ctx, subtype, PDF_NAME(Link));
            is_widget = pdf_name_eq(ctx, subtype, PDF_NAME(Widget));
            if (is_link) {
                status = check_link_action(ctx, annotation);
                if (status != EXTRACTPDF_OK)
                    return status;
            } else if (has_key(ctx, annotation, PDF_NAME(A)) ||
                       has_key(ctx, annotation, PDF_NAME(AA))) {
                return EXTRACTPDF_ERROR_UNSUPPORTED;
            }
            if (split == NULL || !split->changed)
                continue;

            {
                extractpdf_pdf_poster_annot_plan *annot_plan =
                    &split->annots[annot_index];
                int crosses = 0;
                annot_plan->source_annot_index = (size_t)annot_index;
                annot_plan->form_field_index = SIZE_MAX;
                annot_plan->form_widget_index = SIZE_MAX;
                status = read_public_rect(
                    ctx, annotation, split->page.pdf_to_public,
                    &annot_plan->source_public_rect);
                if (status != EXTRACTPDF_OK)
                    return status;
                if (is_link) {
                    annot_plan->kind = EXTRACTPDF_PDF_POSTER_ANNOT_LINK;
                } else if (is_widget) {
                    annot_plan->kind = EXTRACTPDF_PDF_POSTER_ANNOT_WIDGET;
                } else {
                    extractpdf_annotation_type type;
                    if (pdf_name_eq(ctx, subtype, PDF_NAME(Popup)) ||
                        has_key(ctx, annotation, PDF_NAME(Popup)) ||
                        has_key(ctx, annotation, PDF_NAME(IRT)) ||
                        has_key(ctx, annotation, PDF_NAME(RT)))
                        return EXTRACTPDF_ERROR_UNSUPPORTED;
                    if (!extractpdf_pdf_annotation_classify(ctx, annotation, &type) ||
                        type == EXTRACTPDF_ANNOTATION_UNKNOWN)
                        return EXTRACTPDF_ERROR_UNSUPPORTED;
                    annot_plan->kind = EXTRACTPDF_PDF_POSTER_ANNOT_ORDINARY;
                }
                status = extractpdf_pdf_poster_collect_rect_tiles(
                    split, annot_plan->source_public_rect,
                    &annot_plan->tile_indices, &annot_plan->tile_count, &crosses);
                if (status != EXTRACTPDF_OK)
                    return status;
                if (annot_plan->kind != EXTRACTPDF_PDF_POSTER_ANNOT_LINK) {
                    size_t tile_index;
                    if (crosses || annot_plan->tile_count != 1)
                        return EXTRACTPDF_ERROR_UNSUPPORTED;
                    tile_index = annot_plan->tile_indices[0];
                    if (!public_inside(
                            annot_plan->source_public_rect,
                            split->tiles[tile_index].public_rect))
                        return EXTRACTPDF_ERROR_UNSUPPORTED;
                }
            }
        }
    }
    return EXTRACTPDF_OK;
}
