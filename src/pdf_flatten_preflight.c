#include "pdf_flatten_internal.h"
#include "pdf_annotation_common.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct flatten_signature_scan {
    pdf_document *document;
    int has_signed_field;
} flatten_signature_scan;

static int flatten_dict_has_key(
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

static void flatten_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    flatten_signature_scan *scan = (flatten_signature_scan *)data;

    if (scan->has_signed_field || !pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static int flatten_has_signed_field(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    flatten_signature_scan scan;
    pdf_obj *field_type = NULL;
    pdf_obj *fields;

    scan.document = document;
    scan.has_signed_field = 0;
    fields = pdf_dict_getp(
        ctx, pdf_trailer(ctx, document), "Root/AcroForm/Fields");
    pdf_walk_tree(
        ctx,
        fields,
        PDF_NAME(Kids),
        flatten_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field;
}

extractpdf_status extractpdf_pdf_flatten_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    int encrypted = 0;
    int signed_field = 0;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(encrypted);
    fz_var(signed_field);
    fz_var(caught_code);
    fz_try(ctx)
    {
        encrypted = flatten_dict_has_key(
            ctx, pdf_trailer(ctx, document), PDF_NAME(Encrypt));
        if (!encrypted)
            signed_field = flatten_has_signed_field(ctx, document);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (encrypted || signed_field)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static int flatten_same_indirect(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return left != NULL && right != NULL &&
        pdf_is_indirect(ctx, left) && pdf_is_indirect(ctx, right) &&
        pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
        pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
}

static int flatten_supported_annotation(extractpdf_annotation_type type)
{
    switch (type) {
    case EXTRACTPDF_ANNOTATION_TEXT:
    case EXTRACTPDF_ANNOTATION_FREE_TEXT:
    case EXTRACTPDF_ANNOTATION_LINE:
    case EXTRACTPDF_ANNOTATION_SQUARE:
    case EXTRACTPDF_ANNOTATION_CIRCLE:
    case EXTRACTPDF_ANNOTATION_POLYGON:
    case EXTRACTPDF_ANNOTATION_POLY_LINE:
    case EXTRACTPDF_ANNOTATION_HIGHLIGHT:
    case EXTRACTPDF_ANNOTATION_UNDERLINE:
    case EXTRACTPDF_ANNOTATION_SQUIGGLY:
    case EXTRACTPDF_ANNOTATION_STRIKE_OUT:
    case EXTRACTPDF_ANNOTATION_STAMP:
    case EXTRACTPDF_ANNOTATION_CARET:
    case EXTRACTPDF_ANNOTATION_INK:
        return 1;
    default:
        return 0;
    }
}

static extractpdf_status flatten_check_neutral_link(
    fz_context *ctx,
    pdf_obj *link)
{
    pdf_obj *ap;
    pdf_obj *bs;
    pdf_obj *width_object;
    pdf_obj *border;
    float width;
    int index;

    if (!pdf_is_dict(ctx, link))
        return EXTRACTPDF_ERROR_FORMAT;

    if (flatten_dict_has_key(ctx, link, PDF_NAME(AP))) {
        ap = pdf_dict_get(ctx, link, PDF_NAME(AP));
        if (!pdf_is_dict(ctx, ap))
            return EXTRACTPDF_ERROR_FORMAT;
        if (flatten_dict_has_key(ctx, ap, PDF_NAME(N)))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    if (flatten_dict_has_key(ctx, link, PDF_NAME(BS))) {
        bs = pdf_dict_get(ctx, link, PDF_NAME(BS));
        if (!pdf_is_dict(ctx, bs))
            return EXTRACTPDF_ERROR_FORMAT;
        if (!flatten_dict_has_key(ctx, bs, PDF_NAME(W)))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        width_object = pdf_dict_get(ctx, bs, PDF_NAME(W));
        if (!pdf_is_number(ctx, width_object))
            return EXTRACTPDF_ERROR_FORMAT;
        width = pdf_to_real(ctx, width_object);
        if (!isfinite(width) || width < 0.0f)
            return EXTRACTPDF_ERROR_FORMAT;
        return width == 0.0f ?
            EXTRACTPDF_OK : EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    if (flatten_dict_has_key(ctx, link, PDF_NAME(Border))) {
        border = pdf_dict_get(ctx, link, PDF_NAME(Border));
        if (!pdf_is_array(ctx, border) || pdf_array_len(ctx, border) < 3)
            return EXTRACTPDF_ERROR_FORMAT;
        width = 0.0f;
        for (index = 0; index < 3; ++index) {
            pdf_obj *item = pdf_array_get(ctx, border, index);
            float value;
            if (!pdf_is_number(ctx, item))
                return EXTRACTPDF_ERROR_FORMAT;
            value = pdf_to_real(ctx, item);
            if (!isfinite(value))
                return EXTRACTPDF_ERROR_FORMAT;
            if (index == 2)
                width = value;
        }
        return width == 0.0f ?
            EXTRACTPDF_OK : EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

static extractpdf_status flatten_validate_changed_page_links(
    fz_context *ctx,
    pdf_obj *page)
{
    pdf_obj *annots;
    int count;
    int index;

    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    if (!pdf_is_array(ctx, annots))
        return EXTRACTPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, annots);
    if (count < 0)
        return EXTRACTPDF_ERROR_FORMAT;

    for (index = 0; index < count; ++index) {
        pdf_obj *annotation = pdf_array_get(ctx, annots, index);
        pdf_obj *subtype;
        extractpdf_status status;

        if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation))
            return EXTRACTPDF_ERROR_FORMAT;
        subtype = pdf_dict_get(ctx, annotation, PDF_NAME(Subtype));
        if (!pdf_is_name(ctx, subtype))
            return EXTRACTPDF_ERROR_FORMAT;
        if (!pdf_name_eq(ctx, subtype, PDF_NAME(Link)))
            continue;
        status = flatten_check_neutral_link(ctx, annotation);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static int flatten_has_annotation_targets(
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

static int flatten_annotation_target_selected_at(
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

static extractpdf_status flatten_related_annotation_selected(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    pdf_obj *related,
    int *out_selected)
{
    pdf_obj *subtype;
    size_t index;

    *out_selected = 0;
    if (!pdf_is_indirect(ctx, related) || !pdf_is_dict(ctx, related))
        return EXTRACTPDF_ERROR_FORMAT;
    subtype = pdf_dict_get(ctx, related, PDF_NAME(Subtype));
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
        if (flatten_same_indirect(ctx, candidate, related)) {
            *out_selected = 1;
            return EXTRACTPDF_OK;
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_check_relationship_edge(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    int owner_selected,
    pdf_obj *related)
{
    int related_selected = 0;
    extractpdf_status status;

    status = flatten_related_annotation_selected(
        ctx, document, plan, related, &related_selected);
    if (status != EXTRACTPDF_OK)
        return status;
    if (owner_selected || related_selected)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_validate_annotation_relationships(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan)
{
    int page_index;

    if (!flatten_has_annotation_targets(plan))
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
            owner_selected = flatten_annotation_target_selected_at(
                plan, page_index, (size_t)annot_index);

            if (flatten_dict_has_key(ctx, annotation, PDF_NAME(Popup))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(Popup));
                status = flatten_check_relationship_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }

            if (pdf_name_eq(ctx, subtype, PDF_NAME(Popup)) &&
                flatten_dict_has_key(ctx, annotation, PDF_NAME(Parent))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(Parent));
                status = flatten_check_relationship_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }

            if (flatten_dict_has_key(ctx, annotation, PDF_NAME(IRT))) {
                related = pdf_dict_get(ctx, annotation, PDF_NAME(IRT));
                status = flatten_check_relationship_edge(
                    ctx, document, plan, owner_selected, related);
                if (status != EXTRACTPDF_OK)
                    return status;
            }
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_append_target(
    extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_appearance_view *view,
    int page_index,
    size_t annot_ordinal,
    extractpdf_pdf_flatten_target_kind kind,
    extractpdf_annotation_type type,
    uint32_t flags,
    size_t appearance_slot)
{
    extractpdf_pdf_flatten_target_plan *grown;
    extractpdf_pdf_flatten_target_plan *target;
    size_t next;

    if (plan->target_count == SIZE_MAX ||
        plan->target_count + 1 > SIZE_MAX / sizeof(*plan->targets))
        return EXTRACTPDF_ERROR_NOMEM;
    next = plan->target_count + 1;
    grown = (extractpdf_pdf_flatten_target_plan *)realloc(
        plan->targets, next * sizeof(*plan->targets));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    plan->targets = grown;
    target = &plan->targets[plan->target_count];
    memset(target, 0, sizeof(*target));
    target->page_index = page_index;
    target->annot_ordinal = annot_ordinal;
    target->kind = kind;
    target->annotation_type = type;
    target->flags = flags;
    target->rect = view->rect;
    target->appearance_stateful = view->stateful;
    target->appearance_state_size = view->state_name_size;
    target->bbox = view->bbox;
    target->appearance_matrix = view->matrix;
    target->placement = view->placement;
    target->appearance_slot = appearance_slot;
    if (view->stateful) {
        target->appearance_state = (char *)malloc(view->state_name_size + 1);
        if (target->appearance_state == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        memcpy(
            target->appearance_state,
            view->state_name,
            view->state_name_size + 1);
    }
    plan->target_count = next;
    return EXTRACTPDF_OK;
}

static int flatten_dict_has_alias(
    fz_context *ctx,
    pdf_obj *dictionary,
    size_t alias_number)
{
    char wanted[48];
    int count;
    int index;

    if (dictionary == NULL)
        return 0;
    if (alias_number > (size_t)INT_MAX)
        return 1;
    if (fz_snprintf(wanted, sizeof(wanted), "EPB%d", (int)alias_number) >=
        sizeof(wanted))
        return 1;
    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        pdf_obj *key = pdf_dict_get_key(ctx, dictionary, index);
        const char *name;
        if (!pdf_is_name(ctx, key))
            continue;
        name = pdf_to_name(ctx, key);
        if (name != NULL && strcmp(name, wanted) == 0)
            return 1;
    }
    return 0;
}

static extractpdf_status flatten_validate_changed_page(
    fz_context *ctx,
    pdf_obj *page,
    size_t appearance_slot_count,
    size_t **out_alias_numbers)
{
    pdf_obj *resources;
    pdf_obj *xobjects = NULL;
    pdf_obj *contents;
    size_t *aliases = NULL;
    size_t slot;

    *out_alias_numbers = NULL;
    resources = pdf_dict_get_inheritable(ctx, page, PDF_NAME(Resources));
    if (resources != NULL && !pdf_is_null(ctx, resources)) {
        if (!pdf_is_dict(ctx, resources))
            return EXTRACTPDF_ERROR_FORMAT;
        xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
        if (xobjects != NULL && !pdf_is_null(ctx, xobjects) &&
            !pdf_is_dict(ctx, xobjects))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    if (contents != NULL && !pdf_is_null(ctx, contents)) {
        if (pdf_is_stream(ctx, contents)) {
            if (!pdf_is_indirect(ctx, contents))
                return EXTRACTPDF_ERROR_FORMAT;
        } else if (pdf_is_array(ctx, contents)) {
            int count = pdf_array_len(ctx, contents);
            int index;
            if (count < 0)
                return EXTRACTPDF_ERROR_FORMAT;
            for (index = 0; index < count; ++index) {
                pdf_obj *entry = pdf_array_get(ctx, contents, index);
                if (!pdf_is_indirect(ctx, entry) || !pdf_is_stream(ctx, entry))
                    return EXTRACTPDF_ERROR_FORMAT;
            }
        } else {
            return EXTRACTPDF_ERROR_FORMAT;
        }
    }

    if (appearance_slot_count == 0)
        return EXTRACTPDF_OK;
    if (appearance_slot_count > SIZE_MAX / sizeof(*aliases))
        return EXTRACTPDF_ERROR_NOMEM;
    aliases = (size_t *)calloc(appearance_slot_count, sizeof(*aliases));
    if (aliases == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    for (slot = 0; slot < appearance_slot_count; ++slot) {
        size_t candidate = 0;
        int collision;
        do {
            size_t prior;
            if (candidate > (size_t)INT_MAX) {
                free(aliases);
                return EXTRACTPDF_ERROR_UNSUPPORTED;
            }
            collision = flatten_dict_has_alias(ctx, xobjects, candidate);
            for (prior = 0; !collision && prior < slot; ++prior)
                if (aliases[prior] == candidate)
                    collision = 1;
            if (collision)
                ++candidate;
        } while (collision);
        aliases[slot] = candidate;
    }

    *out_alias_numbers = aliases;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_append_page(
    extractpdf_pdf_flatten_plan *plan,
    int page_index,
    size_t first_target,
    size_t target_count,
    size_t appearance_slot_count,
    size_t *alias_numbers)
{
    extractpdf_pdf_flatten_page_plan *grown;
    size_t next;

    if (plan->page_count == SIZE_MAX ||
        plan->page_count + 1 > SIZE_MAX / sizeof(*plan->pages))
        return EXTRACTPDF_ERROR_NOMEM;
    next = plan->page_count + 1;
    grown = (extractpdf_pdf_flatten_page_plan *)realloc(
        plan->pages, next * sizeof(*plan->pages));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    plan->pages = grown;
    plan->pages[plan->page_count].page_index = page_index;
    plan->pages[plan->page_count].first_target = first_target;
    plan->pages[plan->page_count].target_count = target_count;
    plan->pages[plan->page_count].appearance_slot_count = appearance_slot_count;
    plan->pages[plan->page_count].alias_numbers = alias_numbers;
    plan->page_count = next;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_discover_annotations(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_flatten_plan *plan)
{
    int page_index;

    for (page_index = 0; page_index < plan->source_page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = NULL;
        pdf_obj **page_forms = NULL;
        size_t *alias_numbers = NULL;
        size_t first_target = plan->target_count;
        size_t page_selected = 0;
        size_t appearance_slots = 0;
        int annot_count = 0;
        int annot_index;
        int page_has_widget = 0;
        extractpdf_status status = EXTRACTPDF_OK;

        if (!pdf_is_dict(ctx, page))
            return EXTRACTPDF_ERROR_FORMAT;
        if (!extractpdf_pdf_dict_find(ctx, page, PDF_NAME(Annots), &annots))
            continue;
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        annot_count = pdf_array_len(ctx, annots);
        if (annot_count < 0)
            return EXTRACTPDF_ERROR_FORMAT;
        if (annot_count != 0) {
            if ((size_t)annot_count > SIZE_MAX / sizeof(*page_forms))
                return EXTRACTPDF_ERROR_NOMEM;
            page_forms = (pdf_obj **)calloc((size_t)annot_count, sizeof(*page_forms));
            if (page_forms == NULL)
                return EXTRACTPDF_ERROR_NOMEM;
        }

        for (annot_index = 0; annot_index < annot_count; ++annot_index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
            pdf_obj *subtype = NULL;
            const char *subtype_name;
            extractpdf_annotation_type type = EXTRACTPDF_ANNOTATION_UNKNOWN;
            extractpdf_pdf_appearance_view view;
            pdf_obj *form = NULL;
            uint32_t flags = 0;
            size_t slot;
            size_t prior;

            memset(&view, 0, sizeof(view));
            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto page_cleanup;
            }
            if (!extractpdf_pdf_dict_find(
                    ctx, annotation, PDF_NAME(Subtype), &subtype) ||
                !pdf_is_name(ctx, subtype)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto page_cleanup;
            }
            subtype_name = pdf_to_name(ctx, subtype);
            if (subtype_name == NULL) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto page_cleanup;
            }
            if (strcmp(subtype_name, "Widget") == 0) {
                page_has_widget = 1;
                continue;
            }
            if (strcmp(subtype_name, "Link") == 0 ||
                strcmp(subtype_name, "Popup") == 0)
                continue;
            if (!extractpdf_pdf_annotation_classify(ctx, annotation, &type) ||
                type == EXTRACTPDF_ANNOTATION_UNKNOWN ||
                !flatten_supported_annotation(type)) {
                status = EXTRACTPDF_ERROR_UNSUPPORTED;
                goto page_cleanup;
            }

            status = extractpdf_pdf_appearance_resolve(
                ctx, document, annotation, &view, &form);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto page_cleanup;
            }
            status = extractpdf_pdf_read_optional_uint32(
                ctx, annotation, PDF_NAME(F), 0, &flags);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto page_cleanup;
            }

            slot = appearance_slots;
            for (prior = 0; prior < page_selected; ++prior) {
                if (flatten_same_indirect(ctx, page_forms[prior], form)) {
                    slot = plan->targets[first_target + prior].appearance_slot;
                    break;
                }
            }
            if (prior == page_selected)
                ++appearance_slots;
            page_forms[page_selected] = form;

            status = flatten_append_target(
                plan, &view, page_index, (size_t)annot_index,
                EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION,
                type, flags, slot);
            extractpdf_pdf_appearance_drop_view(&view);
            if (status != EXTRACTPDF_OK)
                goto page_cleanup;
            ++page_selected;
        }

        if (page_selected != 0) {
            if (page_has_widget) {
                status = EXTRACTPDF_ERROR_UNSUPPORTED;
                goto page_cleanup;
            }
            status = flatten_validate_changed_page_links(ctx, page);
            if (status == EXTRACTPDF_OK)
                status = flatten_validate_changed_page(
                    ctx, page, appearance_slots, &alias_numbers);
            if (status == EXTRACTPDF_OK) {
                status = flatten_append_page(
                    plan, page_index, first_target, page_selected,
                    appearance_slots, alias_numbers);
                if (status == EXTRACTPDF_OK)
                    alias_numbers = NULL;
            }
        }

page_cleanup:
        free(alias_numbers);
        free(page_forms);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return flatten_validate_annotation_relationships(ctx, document, plan);
}

static extractpdf_status flatten_discover_widgets(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_flatten_plan *plan)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status;
    size_t selected_total = 0;
    int page_index;

    status = extractpdf_pdf_form_build(
        ctx, document, 1, &model, &provenance);
    if (status != EXTRACTPDF_OK)
        return status;
    if (model->widget_count == 0) {
        extractpdf_pdf_form_drop_provenance(ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_OK;
    }

    for (page_index = 0; page_index < plan->source_page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = NULL;
        pdf_obj **page_forms = NULL;
        size_t *alias_numbers = NULL;
        size_t first_target = plan->target_count;
        size_t page_selected = 0;
        size_t appearance_slots = 0;
        int annot_count;
        int annot_index;
        int page_has_ordinary_annotation = 0;

        if (!pdf_is_dict(ctx, page)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        if (!extractpdf_pdf_dict_find(ctx, page, PDF_NAME(Annots), &annots))
            continue;
        if (!pdf_is_array(ctx, annots)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        annot_count = pdf_array_len(ctx, annots);
        if (annot_count < 0) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        if (annot_count != 0) {
            page_forms = (pdf_obj **)calloc((size_t)annot_count, sizeof(*page_forms));
            if (page_forms == NULL) {
                status = EXTRACTPDF_ERROR_NOMEM;
                goto cleanup;
            }
        }

        for (annot_index = 0; annot_index < annot_count; ++annot_index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
            pdf_obj *subtype = NULL;
            extractpdf_pdf_appearance_view view;
            pdf_obj *form = NULL;
            uint32_t flags = 0;
            size_t slot;
            size_t prior;

            memset(&view, 0, sizeof(view));
            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto widget_page_cleanup;
            }
            if (!extractpdf_pdf_dict_find(
                    ctx, annotation, PDF_NAME(Subtype), &subtype) ||
                !pdf_is_name(ctx, subtype)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto widget_page_cleanup;
            }
            if (!pdf_name_eq(ctx, subtype, PDF_NAME(Widget))) {
                if (!pdf_name_eq(ctx, subtype, PDF_NAME(Link)) &&
                    !pdf_name_eq(ctx, subtype, PDF_NAME(Popup)))
                    page_has_ordinary_annotation = 1;
                continue;
            }

            status = extractpdf_pdf_appearance_resolve(
                ctx, document, annotation, &view, &form);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto widget_page_cleanup;
            }
            status = extractpdf_pdf_read_optional_uint32(
                ctx, annotation, PDF_NAME(F), 0, &flags);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto widget_page_cleanup;
            }

            slot = appearance_slots;
            for (prior = 0; prior < page_selected; ++prior) {
                if (flatten_same_indirect(ctx, page_forms[prior], form)) {
                    slot = plan->targets[first_target + prior].appearance_slot;
                    break;
                }
            }
            if (prior == page_selected)
                ++appearance_slots;
            page_forms[page_selected] = form;

            status = flatten_append_target(
                plan, &view, page_index, (size_t)annot_index,
                EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET,
                EXTRACTPDF_ANNOTATION_UNKNOWN, flags, slot);
            extractpdf_pdf_appearance_drop_view(&view);
            if (status != EXTRACTPDF_OK)
                goto widget_page_cleanup;
            ++page_selected;
            ++selected_total;
        }

        if (page_selected != 0) {
            if (page_has_ordinary_annotation) {
                status = EXTRACTPDF_ERROR_UNSUPPORTED;
                goto widget_page_cleanup;
            }
            status = flatten_validate_changed_page_links(ctx, page);
            if (status == EXTRACTPDF_OK)
                status = flatten_validate_changed_page(
                    ctx, page, appearance_slots, &alias_numbers);
            if (status == EXTRACTPDF_OK) {
                status = flatten_append_page(
                    plan, page_index, first_target, page_selected,
                    appearance_slots, alias_numbers);
                if (status == EXTRACTPDF_OK)
                    alias_numbers = NULL;
            }
        }

widget_page_cleanup:
        free(alias_numbers);
        free(page_forms);
        if (status != EXTRACTPDF_OK)
            goto cleanup;
    }

    if (selected_total != model->widget_count) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto cleanup;
    }
    status = extractpdf_pdf_flatten_form_preflight(
        ctx, document, model, provenance, plan);

cleanup:
    extractpdf_pdf_form_drop_provenance(ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return status;
}

static extractpdf_status flatten_discover_combined(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_flatten_plan *plan)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status;
    size_t selected_widget_total = 0;
    int page_index;

    status = extractpdf_pdf_form_build(
        ctx, document, 1, &model, &provenance);
    if (status != EXTRACTPDF_OK)
        return status;

    for (page_index = 0; page_index < plan->source_page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = NULL;
        pdf_obj **page_forms = NULL;
        size_t *alias_numbers = NULL;
        size_t first_target = plan->target_count;
        size_t page_selected = 0;
        size_t appearance_slots = 0;
        int annot_count;
        int annot_index;

        if (!pdf_is_dict(ctx, page)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        if (!extractpdf_pdf_dict_find(ctx, page, PDF_NAME(Annots), &annots))
            continue;
        if (!pdf_is_array(ctx, annots)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        annot_count = pdf_array_len(ctx, annots);
        if (annot_count < 0) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto cleanup;
        }
        if (annot_count != 0) {
            if ((size_t)annot_count > SIZE_MAX / sizeof(*page_forms)) {
                status = EXTRACTPDF_ERROR_NOMEM;
                goto cleanup;
            }
            page_forms = (pdf_obj **)calloc(
                (size_t)annot_count, sizeof(*page_forms));
            if (page_forms == NULL) {
                status = EXTRACTPDF_ERROR_NOMEM;
                goto cleanup;
            }
        }

        for (annot_index = 0; annot_index < annot_count; ++annot_index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
            pdf_obj *subtype = NULL;
            extractpdf_annotation_type type = EXTRACTPDF_ANNOTATION_UNKNOWN;
            extractpdf_pdf_flatten_target_kind kind;
            extractpdf_pdf_appearance_view view;
            pdf_obj *form = NULL;
            uint32_t annotation_flags = 0;
            size_t slot;
            size_t prior;

            memset(&view, 0, sizeof(view));
            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto combined_page_cleanup;
            }
            if (!extractpdf_pdf_dict_find(
                    ctx, annotation, PDF_NAME(Subtype), &subtype) ||
                !pdf_is_name(ctx, subtype)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto combined_page_cleanup;
            }

            if (pdf_name_eq(ctx, subtype, PDF_NAME(Link)) ||
                pdf_name_eq(ctx, subtype, PDF_NAME(Popup)))
                continue;

            if (pdf_name_eq(ctx, subtype, PDF_NAME(Widget))) {
                kind = EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET;
                ++selected_widget_total;
            } else {
                if (!extractpdf_pdf_annotation_classify(ctx, annotation, &type) ||
                    type == EXTRACTPDF_ANNOTATION_UNKNOWN ||
                    !flatten_supported_annotation(type)) {
                    status = EXTRACTPDF_ERROR_UNSUPPORTED;
                    goto combined_page_cleanup;
                }
                kind = EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION;
            }

            status = extractpdf_pdf_appearance_resolve(
                ctx, document, annotation, &view, &form);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto combined_page_cleanup;
            }
            status = extractpdf_pdf_read_optional_uint32(
                ctx, annotation, PDF_NAME(F), 0, &annotation_flags);
            if (status != EXTRACTPDF_OK) {
                extractpdf_pdf_appearance_drop_view(&view);
                goto combined_page_cleanup;
            }

            slot = appearance_slots;
            for (prior = 0; prior < page_selected; ++prior) {
                if (flatten_same_indirect(ctx, page_forms[prior], form)) {
                    slot = plan->targets[first_target + prior].appearance_slot;
                    break;
                }
            }
            if (prior == page_selected)
                ++appearance_slots;
            page_forms[page_selected] = form;

            status = flatten_append_target(
                plan,
                &view,
                page_index,
                (size_t)annot_index,
                kind,
                type,
                annotation_flags,
                slot);
            extractpdf_pdf_appearance_drop_view(&view);
            if (status != EXTRACTPDF_OK)
                goto combined_page_cleanup;
            ++page_selected;
        }

        if (page_selected != 0) {
            status = flatten_validate_changed_page_links(ctx, page);
            if (status == EXTRACTPDF_OK)
                status = flatten_validate_changed_page(
                    ctx, page, appearance_slots, &alias_numbers);
            if (status == EXTRACTPDF_OK) {
                status = flatten_append_page(
                    plan,
                    page_index,
                    first_target,
                    page_selected,
                    appearance_slots,
                    alias_numbers);
                if (status == EXTRACTPDF_OK)
                    alias_numbers = NULL;
            }
        }

combined_page_cleanup:
        free(alias_numbers);
        free(page_forms);
        if (status != EXTRACTPDF_OK)
            goto cleanup;
    }

    if (selected_widget_total != model->widget_count) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto cleanup;
    }
    status = flatten_validate_annotation_relationships(ctx, document, plan);
    if (status != EXTRACTPDF_OK)
        goto cleanup;
    status = extractpdf_pdf_flatten_form_preflight(
        ctx, document, model, provenance, plan);

cleanup:
    extractpdf_pdf_form_drop_provenance(ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return status;
}

static extractpdf_status flatten_validate_real_bake_catalog(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan)
{
    pdf_obj *root;

    if (!plan->any_changed)
        return EXTRACTPDF_OK;
    root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
    if (!pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    if (flatten_dict_has_key(ctx, root, PDF_NAME(StructTreeRoot)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

void extractpdf_pdf_flatten_drop_plan(
    extractpdf_pdf_flatten_plan *plan)
{
    size_t index;

    if (plan == NULL)
        return;
    for (index = 0; index < plan->target_count; ++index)
        free(plan->targets[index].appearance_state);
    for (index = 0; index < plan->page_count; ++index)
        free(plan->pages[index].alias_numbers);
    extractpdf_pdf_flatten_form_drop_plan(plan->form);
    free(plan->targets);
    free(plan->pages);
    free(plan);
}

extractpdf_status extractpdf_pdf_flatten_build_plan(
    fz_context *ctx,
    pdf_document *document,
    uint32_t flags,
    extractpdf_pdf_flatten_plan **out_plan)
{
    extractpdf_pdf_flatten_plan *plan;
    extractpdf_status status = EXTRACTPDF_OK;
    int annotations_requested;
    int widgets_requested;

    if (out_plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_plan = NULL;
    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    plan = (extractpdf_pdf_flatten_plan *)calloc(1, sizeof(*plan));
    if (plan == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    plan->flags = flags;
    plan->source_page_count = pdf_count_pages(ctx, document);
    if (plan->source_page_count < 0) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    annotations_requested = (flags & EXTRACTPDF_FLATTEN_ANNOTATIONS) != 0;
    widgets_requested = (flags & EXTRACTPDF_FLATTEN_WIDGETS) != 0;
    if (annotations_requested && widgets_requested) {
        status = flatten_discover_combined(ctx, document, plan);
    } else if (annotations_requested) {
        status = flatten_discover_annotations(ctx, document, plan);
    } else if (widgets_requested) {
        status = flatten_discover_widgets(ctx, document, plan);
    }
    if (status != EXTRACTPDF_OK)
        goto fail;

    plan->any_changed = plan->target_count != 0;
    status = flatten_validate_real_bake_catalog(ctx, document, plan);
    if (status != EXTRACTPDF_OK)
        goto fail;
    *out_plan = plan;
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_flatten_drop_plan(plan);
    return status;
}

static int flatten_rect_equal(fz_rect left, fz_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int flatten_matrix_equal(fz_matrix left, fz_matrix right)
{
    return left.a == right.a && left.b == right.b &&
        left.c == right.c && left.d == right.d &&
        left.e == right.e && left.f == right.f;
}

int extractpdf_pdf_flatten_plan_equivalent(
    const extractpdf_pdf_flatten_plan *left,
    const extractpdf_pdf_flatten_plan *right)
{
    size_t index;

    if (left == NULL || right == NULL)
        return 0;
    if (left->flags != right->flags ||
        left->source_page_count != right->source_page_count ||
        left->target_count != right->target_count ||
        left->page_count != right->page_count ||
        left->any_changed != right->any_changed ||
        left->policy_complete != right->policy_complete ||
        !extractpdf_pdf_flatten_form_plan_equivalent(left->form, right->form))
        return 0;

    for (index = 0; index < left->page_count; ++index) {
        const extractpdf_pdf_flatten_page_plan *a = &left->pages[index];
        const extractpdf_pdf_flatten_page_plan *b = &right->pages[index];
        size_t alias;
        if (a->page_index != b->page_index ||
            a->first_target != b->first_target ||
            a->target_count != b->target_count ||
            a->appearance_slot_count != b->appearance_slot_count)
            return 0;
        for (alias = 0; alias < a->appearance_slot_count; ++alias)
            if (a->alias_numbers[alias] != b->alias_numbers[alias])
                return 0;
    }
    for (index = 0; index < left->target_count; ++index) {
        const extractpdf_pdf_flatten_target_plan *a = &left->targets[index];
        const extractpdf_pdf_flatten_target_plan *b = &right->targets[index];
        if (a->page_index != b->page_index ||
            a->annot_ordinal != b->annot_ordinal ||
            a->kind != b->kind ||
            a->annotation_type != b->annotation_type ||
            a->flags != b->flags ||
            !flatten_rect_equal(a->rect, b->rect) ||
            a->appearance_stateful != b->appearance_stateful ||
            a->appearance_state_size != b->appearance_state_size ||
            !flatten_rect_equal(a->bbox, b->bbox) ||
            !flatten_matrix_equal(a->appearance_matrix, b->appearance_matrix) ||
            !flatten_matrix_equal(a->placement, b->placement) ||
            a->appearance_slot != b->appearance_slot)
            return 0;
        if (a->appearance_state_size != 0 &&
            memcmp(
                a->appearance_state,
                b->appearance_state,
                a->appearance_state_size) != 0)
            return 0;
    }
    return 1;
}
