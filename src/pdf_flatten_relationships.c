#include "pdf_flatten_internal.h"

static int flatten_relationship_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return left != NULL && right != NULL &&
        pdf_is_indirect(ctx, left) && pdf_is_indirect(ctx, right) &&
        pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
        pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
}

static int flatten_relationship_has_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key)
{
    int count = pdf_dict_len(ctx, dictionary);
    int index;

    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key))
            return 1;
    }
    return 0;
}

static int flatten_relationship_has_annotation_targets(
    const extractpdf_pdf_flatten_plan *plan)
{
    size_t index;

    for (index = 0; index < plan->target_count; ++index) {
        if (plan->targets[index].kind ==
            EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION)
            return 1;
    }
    return 0;
}

static int flatten_relationship_owner_selected(
    const extractpdf_pdf_flatten_plan *plan,
    int page_index,
    size_t annot_ordinal)
{
    size_t index;

    for (index = 0; index < plan->target_count; ++index) {
        const extractpdf_pdf_flatten_target_plan *target = &plan->targets[index];
        if (target->kind == EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION &&
            target->page_index == page_index &&
            target->annot_ordinal == annot_ordinal)
            return 1;
    }
    return 0;
}

static extractpdf_status flatten_relationship_object_selected(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    pdf_obj *object,
    int *out_selected)
{
    pdf_obj *subtype;
    size_t index;

    *out_selected = 0;
    if (!pdf_is_indirect(ctx, object) || !pdf_is_dict(ctx, object))
        return EXTRACTPDF_ERROR_FORMAT;
    subtype = pdf_dict_get(ctx, object, PDF_NAME(Subtype));
    if (!pdf_is_name(ctx, subtype))
        return EXTRACTPDF_ERROR_FORMAT;

    for (index = 0; index < plan->target_count; ++index) {
        const extractpdf_pdf_flatten_target_plan *target = &plan->targets[index];
        pdf_obj *page;
        pdf_obj *annots;
        pdf_obj *candidate;
        int count;

        if (target->kind != EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION)
            continue;
        page = pdf_lookup_page_obj(ctx, document, target->page_index);
        if (!pdf_is_dict(ctx, page))
            return EXTRACTPDF_ERROR_FORMAT;
        annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        count = pdf_array_len(ctx, annots);
        if (count < 0 || target->annot_ordinal >= (size_t)count)
            return EXTRACTPDF_ERROR_FORMAT;
        candidate = pdf_array_get(ctx, annots, (int)target->annot_ordinal);
        if (!pdf_is_indirect(ctx, candidate) || !pdf_is_dict(ctx, candidate))
            return EXTRACTPDF_ERROR_FORMAT;
        if (flatten_relationship_same_identity(ctx, candidate, object)) {
            *out_selected = 1;
            return EXTRACTPDF_OK;
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_relationship_check_edge(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    int owner_selected,
    pdf_obj *related)
{
    int related_selected = 0;
    extractpdf_status status;

    status = flatten_relationship_object_selected(
        ctx, document, plan, related, &related_selected);
    if (status != EXTRACTPDF_OK)
        return status;
    if (owner_selected || related_selected)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_flatten_validate_relationships(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan)
{
    int page_index;

    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (!flatten_relationship_has_annotation_targets(plan))
        return EXTRACTPDF_OK;

    for (page_index = 0; page_index < plan->source_page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots;
        int annot_count;
        int annot_index;

        if (!pdf_is_dict(ctx, page))
            return EXTRACTPDF_ERROR_FORMAT;
        annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        if (annots == NULL)
            continue;
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        annot_count = pdf_array_len(ctx, annots);
        if (annot_count < 0)
            return EXTRACTPDF_ERROR_FORMAT;

        for (annot_index = 0; annot_index < annot_count; ++annot_index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
            pdf_obj *subtype;
            pdf_obj *related;
            int owner_selected;
            extractpdf_status status;

            if (!pdf_is_indirect(ctx, annotation) ||
                !pdf_is_dict(ctx, annotation))
                return EXTRACTPDF_ERROR_FORMAT;
            subtype = pdf_dict_get(ctx, annotation, PDF_NAME(Subtype));
            if (!pdf_is_name(ctx, subtype))
                return EXTRACTPDF_ERROR_FORMAT;
            owner_selected = flatten_relationship_owner_selected(
                plan, page_index, (size_t)annot_index);

            if (flatten_relationship_has_key(
                    ctx, annotation, PDF_NAME(Popup))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(Popup));
                status = flatten_relationship_check_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }

            if (pdf_name_eq(ctx, subtype, PDF_NAME(Popup)) &&
                flatten_relationship_has_key(
                    ctx, annotation, PDF_NAME(Parent))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(Parent));
                status = flatten_relationship_check_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }

            if (flatten_relationship_has_key(
                    ctx, annotation, PDF_NAME(IRT))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(IRT));
                status = flatten_relationship_check_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }
        }
    }
    return EXTRACTPDF_OK;
}
