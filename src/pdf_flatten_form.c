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

struct extractpdf_pdf_flatten_form_plan {
    extractpdf_pdf_flatten_form_entry *entries;
    size_t entry_count;
    size_t root_field_count;
    size_t remove_root_index;
    int remove_acroform;
    int replace_root_fields;
};

struct extractpdf_pdf_flatten_form_runtime {
    pdf_obj *catalog;
    pdf_obj *acroform;
    pdf_obj *fields;
    pdf_obj *field;
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

static extractpdf_status flatten_form_copy_locator(
    const extractpdf_pdf_form_locator *locator,
    extractpdf_pdf_flatten_form_entry *entry)
{
    if (locator->step_count == 0 || locator->steps == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (locator->step_count > SIZE_MAX / sizeof(*entry->locator_steps))
        return EXTRACTPDF_ERROR_NOMEM;
    entry->locator_steps = (size_t *)malloc(
        locator->step_count * sizeof(*entry->locator_steps));
    if (entry->locator_steps == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    memcpy(
        entry->locator_steps,
        locator->steps,
        locator->step_count * sizeof(*entry->locator_steps));
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

void extractpdf_pdf_flatten_form_drop_plan(
    extractpdf_pdf_flatten_form_plan *form)
{
    size_t index;
    if (form == NULL)
        return;
    for (index = 0; index < form->entry_count; ++index)
        free(form->entries[index].locator_steps);
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
    if (widget_target_count != 1)
        return EXTRACTPDF_ERROR_STATE;

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
    if (form->entries[0].locator_count != 1) {
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
    form->remove_root_index = form->entries[0].locator_steps[0];
    root_field = pdf_array_get(ctx, fields, (int)form->remove_root_index);
    if (!flatten_form_same_identity(ctx, root_field, selected_field->group_head)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    if (form->entries[0].merged) {
        if (model->field_count != 1 || root_field_count != 1) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
    } else {
        pdf_obj *kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, kids)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        if (pdf_array_len(ctx, kids) != 1) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        if (!flatten_form_same_identity(
                ctx, pdf_array_get(ctx, kids, 0), selected_widget)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        if (model->field_count != (size_t)root_field_count) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
    }

    if (root_field_count == 1) {
        form->remove_acroform = 1;
    } else {
        if (pdf_dict_gets(ctx, acroform, "CO") != NULL) {
            status = EXTRACTPDF_ERROR_STATE;
            goto fail;
        }
        form->replace_root_fields = 1;
    }
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
        left->remove_root_index != right->remove_root_index ||
        left->remove_acroform != right->remove_acroform ||
        left->replace_root_fields != right->replace_root_fields)
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
    return 1;
}

void extractpdf_pdf_flatten_form_drop_runtime(
    fz_context *ctx,
    extractpdf_pdf_flatten_form_runtime *runtime)
{
    if (runtime == NULL)
        return;
    pdf_drop_obj(ctx, runtime->field);
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
    pdf_obj *field;
    pdf_obj *widget = NULL;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    form = plan->form;
    if (form == NULL)
        return EXTRACTPDF_OK;
    if (form->entry_count != 1 || form->entries[0].locator_count != 1 ||
        form->root_field_count == 0 ||
        form->remove_acroform == form->replace_root_fields)
        return EXTRACTPDF_ERROR_STATE;

    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        return status;
    if ((size_t)pdf_array_len(ctx, fields) != form->root_field_count ||
        form->remove_root_index >= form->root_field_count)
        return EXTRACTPDF_ERROR_FORMAT;
    field = pdf_array_get(ctx, fields, (int)form->remove_root_index);
    if (!pdf_is_dict(ctx, field))
        return EXTRACTPDF_ERROR_FORMAT;

    status = flatten_form_target_object(
        ctx, document, plan, &form->entries[0], &widget);
    if (status != EXTRACTPDF_OK)
        return status;
    if (form->entries[0].merged) {
        if (!flatten_form_same_identity(ctx, field, widget))
            return EXTRACTPDF_ERROR_FORMAT;
    } else {
        pdf_obj *kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, kids) || pdf_array_len(ctx, kids) != 1 ||
            !flatten_form_same_identity(
                ctx, pdf_array_get(ctx, kids, 0), widget))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    resolved = (extractpdf_pdf_flatten_form_runtime *)calloc(1, sizeof(*resolved));
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    resolved->catalog = pdf_keep_obj(ctx, root);
    resolved->acroform = pdf_keep_obj(ctx, acroform);
    resolved->fields = pdf_keep_obj(ctx, fields);
    resolved->field = pdf_keep_obj(ctx, field);
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
    pdf_obj *new_fields = NULL;
    int caught_code = FZ_ERROR_NONE;
    int count;
    int index;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (plan->form == NULL)
        return EXTRACTPDF_OK;
    resolved = runtime->form;
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    if (plan->form->remove_acroform) {
        if (plan->form->replace_root_fields)
            return EXTRACTPDF_ERROR_FORMAT;
        pdf_dict_del(ctx, resolved->catalog, PDF_NAME(AcroForm));
        return EXTRACTPDF_OK;
    }
    if (!plan->form->replace_root_fields)
        return EXTRACTPDF_ERROR_FORMAT;

    count = pdf_array_len(ctx, resolved->fields);
    if (count <= 1 || (size_t)count != plan->form->root_field_count ||
        plan->form->remove_root_index >= (size_t)count)
        return EXTRACTPDF_ERROR_FORMAT;

    fz_var(new_fields);
    fz_var(caught_code);
    fz_try(ctx)
    {
        new_fields = pdf_new_array(ctx, document, count - 1);
        for (index = 0; index < count; ++index) {
            if ((size_t)index == plan->form->remove_root_index)
                continue;
            pdf_array_push(ctx, new_fields, pdf_array_get(ctx, resolved->fields, index));
        }
        pdf_dict_put(ctx, resolved->acroform, PDF_NAME(Fields), new_fields);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, new_fields);
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
