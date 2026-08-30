#include "pdf_flatten_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_pdf_flatten_form_entry {
    size_t target_index;
    size_t field_index;
    size_t *locator_steps;
    size_t locator_count;
    int merged;
} extractpdf_pdf_flatten_form_entry;

typedef struct extractpdf_pdf_flatten_form_co_entry {
    size_t *locator_steps;
    size_t locator_count;
    int survive;
} extractpdf_pdf_flatten_form_co_entry;

struct extractpdf_pdf_flatten_form_plan {
    extractpdf_pdf_flatten_form_entry *entries;
    size_t entry_count;
    size_t root_field_count;
    size_t root_field_index;
    size_t root_kid_count;
    size_t remove_root_kid_index;
    size_t intermediate_kid_count;
    size_t remove_intermediate_kid_index;
    extractpdf_pdf_flatten_form_co_entry *co_entries;
    size_t co_count;
    size_t co_survivor_count;
    int co_present;
    int replace_co;
    int remove_acroform;
    int replace_root_fields;
    int replace_root_kids;
    int replace_intermediate_kids;
};

struct extractpdf_pdf_flatten_form_runtime {
    pdf_obj *catalog;
    pdf_obj *acroform;
    pdf_obj *fields;
    pdf_obj *root_field;
    pdf_obj *root_kids;
    pdf_obj *intermediate_field;
    pdf_obj *intermediate_kids;
    pdf_obj *field;
    pdf_obj *co;
};

static int flatten_form_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return extractpdf_pdf_form_same_identity(ctx, left, right);
}

static extractpdf_status flatten_form_get_root_graph(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj **out_root,
    pdf_obj **out_acroform,
    pdf_obj **out_fields)
{
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;

    *out_root = NULL;
    *out_acroform = NULL;
    *out_fields = NULL;
    if (!extractpdf_pdf_dict_find(
            ctx, pdf_trailer(ctx, document), PDF_NAME(Root), &root) ||
        !pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, root, PDF_NAME(AcroForm), &acroform) ||
        !pdf_is_dict(ctx, acroform))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(Fields), &fields) ||
        !pdf_is_array(ctx, fields))
        return EXTRACTPDF_ERROR_FORMAT;
    *out_root = root;
    *out_acroform = acroform;
    *out_fields = fields;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_check_policy(
    fz_context *ctx,
    pdf_document *document)
{
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *value = NULL;

    if (!extractpdf_pdf_dict_find(
            ctx, pdf_trailer(ctx, document), PDF_NAME(Root), &root) ||
        !pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, root, PDF_NAME(AcroForm), &acroform))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!pdf_is_dict(ctx, acroform))
        return EXTRACTPDF_ERROR_FORMAT;

    if (extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(XFA), &value))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    if (extractpdf_pdf_dict_find(
            ctx, acroform, PDF_NAME(NeedAppearances), &value)) {
        if (!pdf_is_bool(ctx, value))
            return EXTRACTPDF_ERROR_FORMAT;
        if (pdf_to_bool(ctx, value))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_find_widget(
    fz_context *ctx,
    const extractpdf_pdf_form_provenance *provenance,
    pdf_obj *widget,
    size_t *out_field_index)
{
    size_t field_index;
    size_t match = SIZE_MAX;

    *out_field_index = SIZE_MAX;
    for (field_index = 0; field_index < provenance->field_count; ++field_index) {
        const extractpdf_pdf_form_live_field *field =
            &provenance->fields[field_index];
        size_t widget_index;
        for (widget_index = 0; widget_index < field->widget_count; ++widget_index) {
            if (!flatten_form_same_identity(
                    ctx, field->widgets[widget_index].object, widget))
                continue;
            if (match != SIZE_MAX)
                return EXTRACTPDF_ERROR_FORMAT;
            match = field_index;
        }
    }
    if (match == SIZE_MAX)
        return EXTRACTPDF_ERROR_FORMAT;
    *out_field_index = match;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_find_field(
    fz_context *ctx,
    const extractpdf_pdf_form_provenance *provenance,
    pdf_obj *field_object,
    size_t *out_field_index)
{
    size_t field_index;
    size_t match = SIZE_MAX;

    *out_field_index = SIZE_MAX;
    for (field_index = 0; field_index < provenance->field_count; ++field_index) {
        if (!flatten_form_same_identity(
                ctx, provenance->fields[field_index].group_head, field_object))
            continue;
        if (match != SIZE_MAX)
            return EXTRACTPDF_ERROR_FORMAT;
        match = field_index;
    }
    if (match == SIZE_MAX)
        return EXTRACTPDF_ERROR_FORMAT;
    *out_field_index = match;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_copy_steps(
    const size_t *steps,
    size_t step_count,
    size_t **out_steps)
{
    size_t *copy;

    *out_steps = NULL;
    if (step_count == 0 || steps == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (step_count > SIZE_MAX / sizeof(*copy))
        return EXTRACTPDF_ERROR_NOMEM;
    copy = (size_t *)malloc(step_count * sizeof(*copy));
    if (copy == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    memcpy(copy, steps, step_count * sizeof(*copy));
    *out_steps = copy;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_copy_locator(
    const extractpdf_pdf_form_locator *locator,
    extractpdf_pdf_flatten_form_entry *entry)
{
    extractpdf_status status;

    status = flatten_form_copy_steps(
        locator->steps, locator->step_count, &entry->locator_steps);
    if (status != EXTRACTPDF_OK)
        return status;
    entry->locator_count = locator->step_count;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_target_object(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_flatten_form_entry *entry,
    pdf_obj **out_widget)
{
    const extractpdf_pdf_flatten_target_plan *target;
    pdf_obj *page;
    pdf_obj *annots;
    pdf_obj *widget;

    *out_widget = NULL;
    if (entry->target_index >= plan->target_count)
        return EXTRACTPDF_ERROR_FORMAT;
    target = &plan->targets[entry->target_index];
    if (target->kind != EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET)
        return EXTRACTPDF_ERROR_FORMAT;
    page = pdf_lookup_page_obj(ctx, document, target->page_index);
    if (!pdf_is_dict(ctx, page))
        return EXTRACTPDF_ERROR_FORMAT;
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    if (!pdf_is_array(ctx, annots) ||
        target->annot_ordinal >= (size_t)pdf_array_len(ctx, annots))
        return EXTRACTPDF_ERROR_FORMAT;
    widget = pdf_array_get(ctx, annots, (int)target->annot_ordinal);
    if (!pdf_is_indirect(ctx, widget) || !pdf_is_dict(ctx, widget))
        return EXTRACTPDF_ERROR_FORMAT;
    *out_widget = widget;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_check_sole_widget_child(
    fz_context *ctx,
    pdf_obj *field,
    pdf_obj *widget)
{
    pdf_obj *kids;

    if (!pdf_is_dict(ctx, field) || widget == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
    if (!pdf_is_array(ctx, kids))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_array_len(ctx, kids) != 1)
        return EXTRACTPDF_ERROR_STATE;
    if (!flatten_form_same_identity(ctx, pdf_array_get(ctx, kids, 0), widget))
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_check_all_widget_children(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_flatten_form_plan *form,
    pdf_obj *field)
{
    pdf_obj *kids;
    int kid_count;
    int kid_index;
    size_t entry_index;

    if (!pdf_is_dict(ctx, field) || form == NULL || form->entry_count < 2)
        return EXTRACTPDF_ERROR_FORMAT;
    kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
    if (!pdf_is_array(ctx, kids))
        return EXTRACTPDF_ERROR_FORMAT;
    kid_count = pdf_array_len(ctx, kids);
    if (kid_count < 0)
        return EXTRACTPDF_ERROR_FORMAT;
    if ((size_t)kid_count != form->entry_count)
        return EXTRACTPDF_ERROR_STATE;

    for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
        const extractpdf_pdf_flatten_form_entry *entry = &form->entries[entry_index];
        if (entry->locator_count != 1 || entry->locator_steps == NULL ||
            entry->locator_steps[0] != form->entries[0].locator_steps[0] ||
            entry->field_index != form->entries[0].field_index || entry->merged)
            return EXTRACTPDF_ERROR_STATE;
    }

    for (kid_index = 0; kid_index < kid_count; ++kid_index) {
        pdf_obj *kid = pdf_array_get(ctx, kids, kid_index);
        size_t matches = 0;

        if (!pdf_is_indirect(ctx, kid) || !pdf_is_dict(ctx, kid))
            return EXTRACTPDF_ERROR_FORMAT;
        for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
            pdf_obj *widget = NULL;
            extractpdf_status status = flatten_form_target_object(
                ctx, document, plan, &form->entries[entry_index], &widget);
            if (status != EXTRACTPDF_OK)
                return status;
            if (flatten_form_same_identity(ctx, kid, widget))
                ++matches;
        }
        if (matches == 0)
            return EXTRACTPDF_ERROR_STATE;
        if (matches != 1)
            return EXTRACTPDF_ERROR_FORMAT;
    }

    for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
        pdf_obj *widget = NULL;
        size_t matches = 0;
        extractpdf_status status = flatten_form_target_object(
            ctx, document, plan, &form->entries[entry_index], &widget);
        if (status != EXTRACTPDF_OK)
            return status;
        for (kid_index = 0; kid_index < kid_count; ++kid_index)
            if (flatten_form_same_identity(
                    ctx, pdf_array_get(ctx, kids, kid_index), widget))
                ++matches;
        if (matches != 1)
            return EXTRACTPDF_ERROR_FORMAT;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_resolve_locator(
    fz_context *ctx,
    pdf_obj *fields,
    const size_t *steps,
    size_t step_count,
    pdf_obj **out_field)
{
    pdf_obj *field;
    size_t depth;

    *out_field = NULL;
    if (!pdf_is_array(ctx, fields) || steps == NULL || step_count == 0 ||
        steps[0] >= (size_t)pdf_array_len(ctx, fields))
        return EXTRACTPDF_ERROR_FORMAT;
    field = pdf_array_get(ctx, fields, (int)steps[0]);
    if (!pdf_is_dict(ctx, field))
        return EXTRACTPDF_ERROR_FORMAT;

    for (depth = 1; depth < step_count; ++depth) {
        pdf_obj *kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, kids) ||
            steps[depth] >= (size_t)pdf_array_len(ctx, kids))
            return EXTRACTPDF_ERROR_FORMAT;
        field = pdf_array_get(ctx, kids, (int)steps[depth]);
        if (!pdf_is_dict(ctx, field))
            return EXTRACTPDF_ERROR_FORMAT;
    }
    *out_field = field;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_preflight_co(
    fz_context *ctx,
    pdf_obj *acroform,
    const extractpdf_pdf_form_provenance *provenance,
    const extractpdf_pdf_form_live_field *removed_field,
    extractpdf_pdf_flatten_form_plan *form)
{
    pdf_obj *co = NULL;
    int count;
    int index;

    if (!extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(CO), &co))
        return EXTRACTPDF_OK;
    form->co_present = 1;
    if (!pdf_is_array(ctx, co))
        return EXTRACTPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, co);
    if (count < 0)
        return EXTRACTPDF_ERROR_FORMAT;
    form->co_count = (size_t)count;
    if (form->co_count != 0) {
        if (form->co_count > SIZE_MAX / sizeof(*form->co_entries))
            return EXTRACTPDF_ERROR_NOMEM;
        form->co_entries = (extractpdf_pdf_flatten_form_co_entry *)calloc(
            form->co_count, sizeof(*form->co_entries));
        if (form->co_entries == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
    }

    for (index = 0; index < count; ++index) {
        pdf_obj *field_object = pdf_array_get(ctx, co, index);
        extractpdf_pdf_flatten_form_co_entry *entry = &form->co_entries[index];
        const extractpdf_pdf_form_live_field *field;
        size_t field_index;
        extractpdf_status status;

        if (!pdf_is_indirect(ctx, field_object) || !pdf_is_dict(ctx, field_object))
            return EXTRACTPDF_ERROR_FORMAT;
        status = flatten_form_find_field(
            ctx, provenance, field_object, &field_index);
        if (status != EXTRACTPDF_OK)
            return status;
        if (field_index >= provenance->field_count)
            return EXTRACTPDF_ERROR_FORMAT;
        field = &provenance->fields[field_index];
        status = flatten_form_copy_steps(
            field->locator.steps,
            field->locator.step_count,
            &entry->locator_steps);
        if (status != EXTRACTPDF_OK)
            return status;
        entry->locator_count = field->locator.step_count;
        entry->survive = !flatten_form_same_identity(
            ctx, field_object, removed_field->group_head);
        if (entry->survive)
            ++form->co_survivor_count;
    }

    if (!form->remove_acroform && form->co_survivor_count != form->co_count)
        form->replace_co = 1;
    return EXTRACTPDF_OK;
}

void extractpdf_pdf_flatten_form_drop_plan(
    extractpdf_pdf_flatten_form_plan *form)
{
    size_t index;
    if (form == NULL)
        return;
    for (index = 0; index < form->entry_count; ++index)
        free(form->entries[index].locator_steps);
    for (index = 0; index < form->co_count; ++index)
        free(form->co_entries[index].locator_steps);
    free(form->co_entries);
    free(form->entries);
    free(form);
}

extractpdf_status extractpdf_pdf_flatten_form_preflight(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_provenance *provenance,
    extractpdf_pdf_flatten_plan *plan)
{
    extractpdf_pdf_flatten_form_plan *form = NULL;
    const extractpdf_pdf_form_live_field *selected_field = NULL;
    pdf_obj *selected_widget = NULL;
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;
    pdf_obj *root_field;
    size_t widget_target_count = 0;
    size_t target_index;
    size_t at = 0;
    int root_field_count;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || model == NULL ||
        provenance == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (plan->form != NULL)
        return EXTRACTPDF_ERROR_STATE;

    for (target_index = 0; target_index < plan->target_count; ++target_index)
        if (plan->targets[target_index].kind == EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET)
            ++widget_target_count;
    if (widget_target_count == 0)
        return EXTRACTPDF_OK;
    if (widget_target_count != model->widget_count)
        return EXTRACTPDF_ERROR_FORMAT;

    status = flatten_form_check_policy(ctx, document);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_form_capture_provenance_widgets(
        ctx, document, model, provenance);
    if (status != EXTRACTPDF_OK)
        return status;

    form = (extractpdf_pdf_flatten_form_plan *)calloc(1, sizeof(*form));
    if (form == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    form->entries = (extractpdf_pdf_flatten_form_entry *)calloc(
        widget_target_count, sizeof(*form->entries));
    if (form->entries == NULL) {
        free(form);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    form->entry_count = widget_target_count;

    for (target_index = 0; target_index < plan->target_count; ++target_index) {
        const extractpdf_pdf_flatten_target_plan *target =
            &plan->targets[target_index];
        extractpdf_pdf_flatten_form_entry *entry;
        const extractpdf_pdf_form_live_field *field;
        pdf_obj *page;
        pdf_obj *annots;
        pdf_obj *widget;
        size_t field_index;

        if (target->kind != EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET)
            continue;
        page = pdf_lookup_page_obj(ctx, document, target->page_index);
        if (!pdf_is_dict(ctx, page)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        if (!pdf_is_array(ctx, annots) ||
            target->annot_ordinal >= (size_t)pdf_array_len(ctx, annots)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        widget = pdf_array_get(ctx, annots, (int)target->annot_ordinal);
        status = flatten_form_find_widget(
            ctx, provenance, widget, &field_index);
        if (status != EXTRACTPDF_OK)
            goto fail;
        if (field_index >= provenance->field_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        field = &provenance->fields[field_index];
        entry = &form->entries[at++];
        entry->target_index = target_index;
        entry->field_index = field_index;
        entry->merged = flatten_form_same_identity(ctx, field->group_head, widget);
        status = flatten_form_copy_locator(&field->locator, entry);
        if (status != EXTRACTPDF_OK)
            goto fail;
        selected_field = field;
        selected_widget = widget;
    }
    if (at != widget_target_count || selected_field == NULL ||
        selected_widget == NULL) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    if (widget_target_count > 1) {
        size_t entry_index;

        if (model->field_count != 1) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
            const extractpdf_pdf_flatten_form_entry *entry =
                &form->entries[entry_index];
            if (entry->field_index != form->entries[0].field_index ||
                entry->locator_count != 1 || entry->locator_steps == NULL ||
                entry->locator_steps[0] != form->entries[0].locator_steps[0] ||
                entry->merged) {
                status = EXTRACTPDF_ERROR_STATE;
                goto fail;
            }
        }
        selected_field = &provenance->fields[form->entries[0].field_index];
        if (selected_field->widget_count != widget_target_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
    } else if (form->entries[0].locator_count == 0 ||
               form->entries[0].locator_count > 3) {
        status = EXTRACTPDF_ERROR_STATE;
        goto fail;
    }

    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        goto fail;
    (void)root;
    root_field_count = pdf_array_len(ctx, fields);
    if (root_field_count <= 0 ||
        form->entries[0].locator_steps[0] >= (size_t)root_field_count) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }
    form->root_field_count = (size_t)root_field_count;
    form->root_field_index = form->entries[0].locator_steps[0];
    root_field = pdf_array_get(ctx, fields, (int)form->root_field_index);
    if (!pdf_is_dict(ctx, root_field)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    if (widget_target_count > 1) {
        if (root_field_count != 1 || form->root_field_index != 0 ||
            !flatten_form_same_identity(
                ctx, root_field, selected_field->group_head)) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        status = flatten_form_check_all_widget_children(
            ctx, document, plan, form, root_field);
        if (status != EXTRACTPDF_OK)
            goto fail;
        form->remove_acroform = 1;
    } else if (form->entries[0].locator_count == 1) {
        if (!flatten_form_same_identity(
                ctx, root_field, selected_field->group_head)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        if (form->entries[0].merged) {
            if (model->field_count != 1 || root_field_count != 1) {
                status = EXTRACTPDF_ERROR_STATE;
                goto fail;
            }
        } else {
            status = flatten_form_check_sole_widget_child(
                ctx, root_field, selected_widget);
            if (status != EXTRACTPDF_OK)
                goto fail;
            if (model->field_count != (size_t)root_field_count) {
                status = EXTRACTPDF_ERROR_STATE;
                goto fail;
            }
        }

        if (root_field_count == 1)
            form->remove_acroform = 1;
        else
            form->replace_root_fields = 1;
    } else if (form->entries[0].locator_count == 2) {
        pdf_obj *root_kids;
        pdf_obj *selected_terminal;
        int root_kid_count;

        if (form->entries[0].merged || root_field_count != 1) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        root_kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, root_kids)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        root_kid_count = pdf_array_len(ctx, root_kids);
        if (root_kid_count <= 1 ||
            form->entries[0].locator_steps[1] >= (size_t)root_kid_count) {
            status = root_kid_count <= 1 ?
                EXTRACTPDF_ERROR_STATE : EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        if (model->field_count != (size_t)root_kid_count) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        form->root_kid_count = (size_t)root_kid_count;
        form->remove_root_kid_index = form->entries[0].locator_steps[1];
        selected_terminal = pdf_array_get(
            ctx, root_kids, (int)form->remove_root_kid_index);
        if (!flatten_form_same_identity(
                ctx, selected_terminal, selected_field->group_head)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        status = flatten_form_check_sole_widget_child(
            ctx, selected_terminal, selected_widget);
        if (status != EXTRACTPDF_OK)
            goto fail;
        form->replace_root_kids = 1;
    } else {
        pdf_obj *root_kids;
        pdf_obj *mid;
        pdf_obj *mid_kids;
        pdf_obj *selected_terminal;
        pdf_obj *parent;
        int root_kid_count;
        int mid_kid_count;
        size_t selected_mid_index;

        if (form->entries[0].merged || root_field_count != 1) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        root_kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, root_kids)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        root_kid_count = pdf_array_len(ctx, root_kids);
        if (root_kid_count <= 0 ||
            form->entries[0].locator_steps[1] >= (size_t)root_kid_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        form->root_kid_count = (size_t)root_kid_count;
        form->remove_root_kid_index = form->entries[0].locator_steps[1];
        mid = pdf_array_get(ctx, root_kids, (int)form->remove_root_kid_index);
        if (!pdf_is_indirect(ctx, mid) || !pdf_is_dict(ctx, mid)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        parent = pdf_dict_get(ctx, mid, PDF_NAME(Parent));
        if (!flatten_form_same_identity(ctx, parent, root_field)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        mid_kids = pdf_dict_get(ctx, mid, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, mid_kids)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        mid_kid_count = pdf_array_len(ctx, mid_kids);
        selected_mid_index = form->entries[0].locator_steps[2];
        if (mid_kid_count <= 0 || selected_mid_index >= (size_t)mid_kid_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        selected_terminal = pdf_array_get(ctx, mid_kids, (int)selected_mid_index);
        if (!flatten_form_same_identity(
                ctx, selected_terminal, selected_field->group_head)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        parent = pdf_dict_get(ctx, selected_terminal, PDF_NAME(Parent));
        if (!flatten_form_same_identity(ctx, parent, mid)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        status = flatten_form_check_sole_widget_child(
            ctx, selected_terminal, selected_widget);
        if (status != EXTRACTPDF_OK)
            goto fail;

        if (mid_kid_count == 1) {
            if (selected_mid_index != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto fail;
            }
            if (root_kid_count <= 1 ||
                model->field_count != (size_t)root_kid_count) {
                status = EXTRACTPDF_ERROR_STATE;
                goto fail;
            }
            form->replace_root_kids = 1;
        } else {
            if (root_kid_count != 1 || form->remove_root_kid_index != 0 ||
                model->field_count != (size_t)mid_kid_count) {
                status = EXTRACTPDF_ERROR_STATE;
                goto fail;
            }
            form->intermediate_kid_count = (size_t)mid_kid_count;
            form->remove_intermediate_kid_index = selected_mid_index;
            form->replace_intermediate_kids = 1;
        }
    }

    status = flatten_form_preflight_co(
        ctx, acroform, provenance, selected_field, form);
    if (status != EXTRACTPDF_OK)
        goto fail;

    plan->form = form;
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_flatten_form_drop_plan(form);
    return status;
}

int extractpdf_pdf_flatten_form_plan_equivalent(
    const extractpdf_pdf_flatten_form_plan *left,
    const extractpdf_pdf_flatten_form_plan *right)
{
    size_t index;
    if (left == NULL || right == NULL)
        return left == right;
    if (left->entry_count != right->entry_count ||
        left->root_field_count != right->root_field_count ||
        left->root_field_index != right->root_field_index ||
        left->root_kid_count != right->root_kid_count ||
        left->remove_root_kid_index != right->remove_root_kid_index ||
        left->intermediate_kid_count != right->intermediate_kid_count ||
        left->remove_intermediate_kid_index !=
            right->remove_intermediate_kid_index ||
        left->co_count != right->co_count ||
        left->co_survivor_count != right->co_survivor_count ||
        left->co_present != right->co_present ||
        left->replace_co != right->replace_co ||
        left->remove_acroform != right->remove_acroform ||
        left->replace_root_fields != right->replace_root_fields ||
        left->replace_root_kids != right->replace_root_kids ||
        left->replace_intermediate_kids != right->replace_intermediate_kids)
        return 0;
    for (index = 0; index < left->entry_count; ++index) {
        const extractpdf_pdf_flatten_form_entry *a = &left->entries[index];
        const extractpdf_pdf_flatten_form_entry *b = &right->entries[index];
        if (a->target_index != b->target_index ||
            a->field_index != b->field_index ||
            a->locator_count != b->locator_count ||
            a->merged != b->merged)
            return 0;
        if (a->locator_count != 0 &&
            memcmp(
                a->locator_steps,
                b->locator_steps,
                a->locator_count * sizeof(*a->locator_steps)) != 0)
            return 0;
    }
    for (index = 0; index < left->co_count; ++index) {
        const extractpdf_pdf_flatten_form_co_entry *a = &left->co_entries[index];
        const extractpdf_pdf_flatten_form_co_entry *b = &right->co_entries[index];
        if (a->locator_count != b->locator_count || a->survive != b->survive)
            return 0;
        if (a->locator_count != 0 &&
            memcmp(
                a->locator_steps,
                b->locator_steps,
                a->locator_count * sizeof(*a->locator_steps)) != 0)
            return 0;
    }
    return 1;
}

void extractpdf_pdf_flatten_form_drop_runtime(
    fz_context *ctx,
    extractpdf_pdf_flatten_form_runtime *runtime)
{
    if (runtime == NULL)
        return;
    pdf_drop_obj(ctx, runtime->co);
    pdf_drop_obj(ctx, runtime->field);
    pdf_drop_obj(ctx, runtime->intermediate_kids);
    pdf_drop_obj(ctx, runtime->intermediate_field);
    pdf_drop_obj(ctx, runtime->root_kids);
    pdf_drop_obj(ctx, runtime->root_field);
    pdf_drop_obj(ctx, runtime->fields);
    pdf_drop_obj(ctx, runtime->acroform);
    pdf_drop_obj(ctx, runtime->catalog);
    free(runtime);
}

extractpdf_status extractpdf_pdf_flatten_form_resolve_runtime(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime)
{
    const extractpdf_pdf_flatten_form_plan *form;
    extractpdf_pdf_flatten_form_runtime *resolved = NULL;
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;
    pdf_obj *root_field;
    pdf_obj *root_kids = NULL;
    pdf_obj *intermediate_field = NULL;
    pdf_obj *intermediate_kids = NULL;
    pdf_obj *field = NULL;
    pdf_obj *widget = NULL;
    pdf_obj *co = NULL;
    extractpdf_status status;
    int mode_count;
    size_t co_index;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    form = plan->form;
    if (form == NULL)
        return EXTRACTPDF_OK;
    mode_count = form->remove_acroform + form->replace_root_fields +
        form->replace_root_kids + form->replace_intermediate_kids;
    if (form->entry_count == 0 || form->root_field_count == 0 || mode_count != 1)
        return EXTRACTPDF_ERROR_STATE;
    if (form->entry_count == 1) {
        if (form->entries[0].locator_count != 1 &&
            form->entries[0].locator_count != 2 &&
            form->entries[0].locator_count != 3)
            return EXTRACTPDF_ERROR_STATE;
    } else {
        size_t entry_index;
        if (!form->remove_acroform || form->replace_root_fields ||
            form->replace_root_kids || form->replace_intermediate_kids ||
            form->root_field_count != 1 || form->root_field_index != 0)
            return EXTRACTPDF_ERROR_STATE;
        for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
            const extractpdf_pdf_flatten_form_entry *entry =
                &form->entries[entry_index];
            if (entry->locator_count != 1 || entry->locator_steps == NULL ||
                entry->locator_steps[0] != form->entries[0].locator_steps[0] ||
                entry->field_index != form->entries[0].field_index || entry->merged)
                return EXTRACTPDF_ERROR_STATE;
        }
    }

    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        return status;
    if ((size_t)pdf_array_len(ctx, fields) != form->root_field_count ||
        form->root_field_index >= form->root_field_count)
        return EXTRACTPDF_ERROR_FORMAT;
    root_field = pdf_array_get(ctx, fields, (int)form->root_field_index);
    if (!pdf_is_dict(ctx, root_field))
        return EXTRACTPDF_ERROR_FORMAT;

    if (form->entry_count > 1) {
        status = flatten_form_check_all_widget_children(
            ctx, document, plan, form, root_field);
        if (status != EXTRACTPDF_OK)
            return status;
        root_kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, root_kids))
            return EXTRACTPDF_ERROR_FORMAT;
        field = root_field;
    } else {
        status = flatten_form_target_object(
            ctx, document, plan, &form->entries[0], &widget);
        if (status != EXTRACTPDF_OK)
            return status;
        status = flatten_form_resolve_locator(
            ctx,
            fields,
            form->entries[0].locator_steps,
            form->entries[0].locator_count,
            &field);
        if (status != EXTRACTPDF_OK)
            return status;

        if (form->entries[0].locator_count == 1) {
            if (!flatten_form_same_identity(ctx, field, root_field) ||
                form->replace_root_kids || form->replace_intermediate_kids)
                return EXTRACTPDF_ERROR_FORMAT;
            if (form->entries[0].merged) {
                if (!flatten_form_same_identity(ctx, field, widget))
                    return EXTRACTPDF_ERROR_FORMAT;
            } else {
                status = flatten_form_check_sole_widget_child(ctx, field, widget);
                if (status != EXTRACTPDF_OK)
                    return status;
            }
        } else if (form->entries[0].locator_count == 2) {
            pdf_obj *parent;

            if (!form->replace_root_kids || form->entries[0].merged ||
                form->root_kid_count <= 1 ||
                form->remove_root_kid_index >= form->root_kid_count)
                return EXTRACTPDF_ERROR_FORMAT;
            root_kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
            if (!pdf_is_array(ctx, root_kids) ||
                (size_t)pdf_array_len(ctx, root_kids) != form->root_kid_count ||
                !flatten_form_same_identity(
                    ctx,
                    pdf_array_get(ctx, root_kids, (int)form->remove_root_kid_index),
                    field))
                return EXTRACTPDF_ERROR_FORMAT;
            parent = pdf_dict_get(ctx, field, PDF_NAME(Parent));
            if (!flatten_form_same_identity(ctx, parent, root_field))
                return EXTRACTPDF_ERROR_FORMAT;
            status = flatten_form_check_sole_widget_child(ctx, field, widget);
            if (status != EXTRACTPDF_OK)
                return status;
        } else {
            pdf_obj *mid;
            pdf_obj *mid_kids;
            pdf_obj *parent;
            size_t selected_mid_index = form->entries[0].locator_steps[2];

            if (form->entries[0].merged ||
                (!form->replace_root_kids && !form->replace_intermediate_kids))
                return EXTRACTPDF_ERROR_FORMAT;
            root_kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
            if (!pdf_is_array(ctx, root_kids) ||
                (size_t)pdf_array_len(ctx, root_kids) != form->root_kid_count ||
                form->remove_root_kid_index >= form->root_kid_count)
                return EXTRACTPDF_ERROR_FORMAT;
            mid = pdf_array_get(
                ctx, root_kids, (int)form->remove_root_kid_index);
            if (!pdf_is_indirect(ctx, mid) || !pdf_is_dict(ctx, mid))
                return EXTRACTPDF_ERROR_FORMAT;
            parent = pdf_dict_get(ctx, mid, PDF_NAME(Parent));
            if (!flatten_form_same_identity(ctx, parent, root_field))
                return EXTRACTPDF_ERROR_FORMAT;
            mid_kids = pdf_dict_get(ctx, mid, PDF_NAME(Kids));
            if (!pdf_is_array(ctx, mid_kids))
                return EXTRACTPDF_ERROR_FORMAT;

            if (form->replace_root_kids) {
                if (form->root_kid_count <= 1 || pdf_array_len(ctx, mid_kids) != 1 ||
                    selected_mid_index != 0 ||
                    !flatten_form_same_identity(ctx, pdf_array_get(ctx, mid_kids, 0), field))
                    return EXTRACTPDF_ERROR_FORMAT;
            } else {
                if (form->root_kid_count != 1 || form->remove_root_kid_index != 0 ||
                    form->intermediate_kid_count <= 1 ||
                    (size_t)pdf_array_len(ctx, mid_kids) !=
                        form->intermediate_kid_count ||
                    form->remove_intermediate_kid_index != selected_mid_index ||
                    form->remove_intermediate_kid_index >=
                        form->intermediate_kid_count ||
                    !flatten_form_same_identity(
                        ctx,
                        pdf_array_get(
                            ctx, mid_kids,
                            (int)form->remove_intermediate_kid_index),
                        field))
                    return EXTRACTPDF_ERROR_FORMAT;
                intermediate_field = mid;
                intermediate_kids = mid_kids;
            }
            parent = pdf_dict_get(ctx, field, PDF_NAME(Parent));
            if (!flatten_form_same_identity(ctx, parent, mid))
                return EXTRACTPDF_ERROR_FORMAT;
            status = flatten_form_check_sole_widget_child(ctx, field, widget);
            if (status != EXTRACTPDF_OK)
                return status;
        }
    }

    if (form->co_present) {
        if (!extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(CO), &co) ||
            !pdf_is_array(ctx, co) ||
            (size_t)pdf_array_len(ctx, co) != form->co_count)
            return EXTRACTPDF_ERROR_FORMAT;
        for (co_index = 0; co_index < form->co_count; ++co_index) {
            const extractpdf_pdf_flatten_form_co_entry *co_entry =
                &form->co_entries[co_index];
            pdf_obj *co_field = pdf_array_get(ctx, co, (int)co_index);
            pdf_obj *expected_field = NULL;

            if (!pdf_is_indirect(ctx, co_field) || !pdf_is_dict(ctx, co_field))
                return EXTRACTPDF_ERROR_FORMAT;
            status = flatten_form_resolve_locator(
                ctx,
                fields,
                co_entry->locator_steps,
                co_entry->locator_count,
                &expected_field);
            if (status != EXTRACTPDF_OK)
                return status;
            if (!flatten_form_same_identity(ctx, co_field, expected_field))
                return EXTRACTPDF_ERROR_FORMAT;
        }
    } else if (extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(CO), &co)) {
        return EXTRACTPDF_ERROR_FORMAT;
    }

    resolved = (extractpdf_pdf_flatten_form_runtime *)calloc(1, sizeof(*resolved));
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    resolved->catalog = pdf_keep_obj(ctx, root);
    resolved->acroform = pdf_keep_obj(ctx, acroform);
    resolved->fields = pdf_keep_obj(ctx, fields);
    resolved->root_field = pdf_keep_obj(ctx, root_field);
    resolved->root_kids = pdf_keep_obj(ctx, root_kids);
    resolved->intermediate_field = pdf_keep_obj(ctx, intermediate_field);
    resolved->intermediate_kids = pdf_keep_obj(ctx, intermediate_kids);
    resolved->field = pdf_keep_obj(ctx, field);
    resolved->co = pdf_keep_obj(ctx, co);
    runtime->form = resolved;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_flatten_form_apply(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime)
{
    extractpdf_pdf_flatten_form_runtime *resolved;
    pdf_obj *replacement_members = NULL;
    pdf_obj *replacement_co = NULL;
    int caught_code = FZ_ERROR_NONE;
    int count;
    int index;
    size_t co_index;
    size_t co_kept = 0;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (plan->form == NULL)
        return EXTRACTPDF_OK;
    resolved = runtime->form;
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    if (plan->form->remove_acroform) {
        if (plan->form->replace_root_fields || plan->form->replace_root_kids ||
            plan->form->replace_intermediate_kids || plan->form->replace_co)
            return EXTRACTPDF_ERROR_FORMAT;
        pdf_dict_del(ctx, resolved->catalog, PDF_NAME(AcroForm));
        return EXTRACTPDF_OK;
    }

    fz_var(replacement_members);
    fz_var(replacement_co);
    fz_var(caught_code);
    fz_try(ctx)
    {
        if (plan->form->replace_root_fields) {
            count = pdf_array_len(ctx, resolved->fields);
            if (count <= 1 ||
                (size_t)count != plan->form->root_field_count ||
                plan->form->root_field_index >= (size_t)count)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Fields changed after preflight");
            replacement_members = pdf_new_array(ctx, document, count - 1);
            for (index = 0; index < count; ++index) {
                if ((size_t)index == plan->form->root_field_index)
                    continue;
                pdf_array_push(
                    ctx,
                    replacement_members,
                    pdf_array_get(ctx, resolved->fields, index));
            }
        } else if (plan->form->replace_root_kids) {
            count = pdf_array_len(ctx, resolved->root_kids);
            if (count <= 1 ||
                (size_t)count != plan->form->root_kid_count ||
                plan->form->remove_root_kid_index >= (size_t)count)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Kids changed after preflight");
            replacement_members = pdf_new_array(ctx, document, count - 1);
            for (index = 0; index < count; ++index) {
                if ((size_t)index == plan->form->remove_root_kid_index)
                    continue;
                pdf_array_push(
                    ctx,
                    replacement_members,
                    pdf_array_get(ctx, resolved->root_kids, index));
            }
        } else if (plan->form->replace_intermediate_kids) {
            count = pdf_array_len(ctx, resolved->intermediate_kids);
            if (resolved->intermediate_field == NULL ||
                !pdf_is_dict(ctx, resolved->intermediate_field) ||
                count <= 1 ||
                (size_t)count != plan->form->intermediate_kid_count ||
                plan->form->remove_intermediate_kid_index >= (size_t)count)
                fz_throw(
                    ctx, FZ_ERROR_FORMAT,
                    "flatten intermediate Kids changed after preflight");
            replacement_members = pdf_new_array(ctx, document, count - 1);
            for (index = 0; index < count; ++index) {
                if ((size_t)index == plan->form->remove_intermediate_kid_index)
                    continue;
                pdf_array_push(
                    ctx,
                    replacement_members,
                    pdf_array_get(ctx, resolved->intermediate_kids, index));
            }
        } else {
            fz_throw(ctx, FZ_ERROR_FORMAT, "flatten form mutation mode missing");
        }

        if (plan->form->replace_co) {
            if (!plan->form->co_present || resolved->co == NULL ||
                !pdf_is_array(ctx, resolved->co) ||
                (size_t)pdf_array_len(ctx, resolved->co) != plan->form->co_count)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten CO changed after preflight");
            replacement_co = pdf_new_array(
                ctx, document, (int)plan->form->co_survivor_count);
            for (co_index = 0; co_index < plan->form->co_count; ++co_index) {
                if (!plan->form->co_entries[co_index].survive)
                    continue;
                pdf_array_push(
                    ctx,
                    replacement_co,
                    pdf_array_get(ctx, resolved->co, (int)co_index));
                ++co_kept;
            }
            if (co_kept != plan->form->co_survivor_count)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten CO survivor count changed");
        }

        if (plan->form->replace_root_fields)
            pdf_dict_put(
                ctx, resolved->acroform, PDF_NAME(Fields), replacement_members);
        else if (plan->form->replace_root_kids)
            pdf_dict_put(
                ctx, resolved->root_field, PDF_NAME(Kids), replacement_members);
        else
            pdf_dict_put(
                ctx,
                resolved->intermediate_field,
                PDF_NAME(Kids),
                replacement_members);
        if (plan->form->replace_co)
            pdf_dict_put(ctx, resolved->acroform, PDF_NAME(CO), replacement_co);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, replacement_co);
        pdf_drop_obj(ctx, replacement_members);
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
