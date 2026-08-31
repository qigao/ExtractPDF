#include "pdf_flatten_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_pdf_flatten_form_entry {
    size_t target_index;
    size_t field_index;
    size_t *locator_steps;
    size_t locator_count;
    size_t widget_kid_index;
    int merged;
} extractpdf_pdf_flatten_form_entry;

typedef struct extractpdf_pdf_flatten_form_node {
    size_t *locator_steps;
    size_t locator_count;
    size_t original_kid_count;
    size_t *survivor_kid_indices;
    size_t survivor_kid_count;
    int kids_present;
    int selected_merged;
    int remove;
    int replace_kids;
} extractpdf_pdf_flatten_form_node;

typedef struct extractpdf_pdf_flatten_form_co_entry {
    size_t *locator_steps;
    size_t locator_count;
    int survive;
} extractpdf_pdf_flatten_form_co_entry;

struct extractpdf_pdf_flatten_form_plan {
    extractpdf_pdf_flatten_form_entry *entries;
    size_t entry_count;
    extractpdf_pdf_flatten_form_node *nodes;
    size_t node_count;
    size_t root_field_count;
    size_t *root_survivor_indices;
    size_t root_survivor_count;
    extractpdf_pdf_flatten_form_co_entry *co_entries;
    size_t co_count;
    size_t co_survivor_count;
    int co_present;
    int replace_co;
    int remove_acroform;
    int replace_root_fields;
};

typedef struct extractpdf_pdf_flatten_form_runtime_node {
    pdf_obj *field;
    pdf_obj *kids;
} extractpdf_pdf_flatten_form_runtime_node;

struct extractpdf_pdf_flatten_form_runtime {
    pdf_obj *catalog;
    pdf_obj *acroform;
    pdf_obj *fields;
    pdf_obj *co;
    extractpdf_pdf_flatten_form_runtime_node *nodes;
    size_t node_count;
};

static int flatten_form_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return extractpdf_pdf_form_same_identity(ctx, left, right);
}

static int flatten_form_locator_equal(
    const size_t *left,
    size_t left_count,
    const size_t *right,
    size_t right_count)
{
    return left_count == right_count &&
        left_count != 0 &&
        left != NULL &&
        right != NULL &&
        memcmp(left, right, left_count * sizeof(*left)) == 0;
}

static int flatten_form_locator_prefix(
    const size_t *prefix,
    size_t prefix_count,
    const size_t *steps,
    size_t step_count)
{
    return prefix_count != 0 &&
        prefix_count <= step_count &&
        prefix != NULL &&
        steps != NULL &&
        memcmp(prefix, steps, prefix_count * sizeof(*prefix)) == 0;
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
    if (!pdf_is_indirect(ctx, field) || !pdf_is_dict(ctx, field))
        return EXTRACTPDF_ERROR_FORMAT;

    for (depth = 1; depth < step_count; ++depth) {
        pdf_obj *kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, kids) ||
            steps[depth] >= (size_t)pdf_array_len(ctx, kids))
            return EXTRACTPDF_ERROR_FORMAT;
        field = pdf_array_get(ctx, kids, (int)steps[depth]);
        if (!pdf_is_indirect(ctx, field) || !pdf_is_dict(ctx, field))
            return EXTRACTPDF_ERROR_FORMAT;
    }
    *out_field = field;
    return EXTRACTPDF_OK;
}

static size_t flatten_form_find_node(
    const extractpdf_pdf_flatten_form_plan *form,
    const size_t *steps,
    size_t step_count)
{
    size_t index;
    for (index = 0; index < form->node_count; ++index) {
        const extractpdf_pdf_flatten_form_node *node = &form->nodes[index];
        if (flatten_form_locator_equal(
                node->locator_steps, node->locator_count, steps, step_count))
            return index;
    }
    return SIZE_MAX;
}

static extractpdf_status flatten_form_add_node(
    extractpdf_pdf_flatten_form_plan *form,
    const size_t *steps,
    size_t step_count)
{
    extractpdf_pdf_flatten_form_node *grown;
    extractpdf_pdf_flatten_form_node *node;
    size_t next;
    extractpdf_status status;

    if (flatten_form_find_node(form, steps, step_count) != SIZE_MAX)
        return EXTRACTPDF_OK;
    if (form->node_count == SIZE_MAX ||
        form->node_count + 1 > SIZE_MAX / sizeof(*form->nodes))
        return EXTRACTPDF_ERROR_NOMEM;
    next = form->node_count + 1;
    grown = (extractpdf_pdf_flatten_form_node *)realloc(
        form->nodes, next * sizeof(*form->nodes));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    form->nodes = grown;
    node = &form->nodes[form->node_count];
    memset(node, 0, sizeof(*node));
    status = flatten_form_copy_steps(steps, step_count, &node->locator_steps);
    if (status != EXTRACTPDF_OK)
        return status;
    node->locator_count = step_count;
    form->node_count = next;
    return EXTRACTPDF_OK;
}

static int flatten_form_entry_belongs_to_node(
    const extractpdf_pdf_flatten_form_entry *entry,
    const extractpdf_pdf_flatten_form_node *node)
{
    return flatten_form_locator_equal(
        entry->locator_steps,
        entry->locator_count,
        node->locator_steps,
        node->locator_count);
}

static extractpdf_status flatten_form_prepare_entry(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_form_provenance *provenance,
    const extractpdf_pdf_flatten_plan *plan,
    size_t target_index,
    extractpdf_pdf_flatten_form_entry *entry)
{
    const extractpdf_pdf_form_live_field *field;
    pdf_obj *widget = NULL;
    pdf_obj *fields = NULL;
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *field_object = NULL;
    pdf_obj *kids;
    size_t field_index;
    extractpdf_status status;

    entry->target_index = target_index;
    status = flatten_form_target_object(
        ctx, document, plan, entry, &widget);
    if (status != EXTRACTPDF_OK)
        return status;
    status = flatten_form_find_widget(ctx, provenance, widget, &field_index);
    if (status != EXTRACTPDF_OK)
        return status;
    if (field_index >= provenance->field_count)
        return EXTRACTPDF_ERROR_FORMAT;
    field = &provenance->fields[field_index];

    entry->field_index = field_index;
    entry->widget_kid_index = SIZE_MAX;
    status = flatten_form_copy_locator(&field->locator, entry);
    if (status != EXTRACTPDF_OK)
        return status;

    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        return status;
    (void)root;
    (void)acroform;
    status = flatten_form_resolve_locator(
        ctx, fields, entry->locator_steps, entry->locator_count, &field_object);
    if (status != EXTRACTPDF_OK)
        return status;
    if (!flatten_form_same_identity(ctx, field_object, field->group_head))
        return EXTRACTPDF_ERROR_FORMAT;

    entry->merged = flatten_form_same_identity(ctx, field_object, widget);
    if (!entry->merged) {
        int count;
        int index;
        size_t matches = 0;

        kids = pdf_dict_get(ctx, field_object, PDF_NAME(Kids));
        if (!pdf_is_array(ctx, kids))
            return EXTRACTPDF_ERROR_FORMAT;
        count = pdf_array_len(ctx, kids);
        if (count < 0)
            return EXTRACTPDF_ERROR_FORMAT;
        for (index = 0; index < count; ++index) {
            if (!flatten_form_same_identity(
                    ctx, pdf_array_get(ctx, kids, index), widget))
                continue;
            entry->widget_kid_index = (size_t)index;
            ++matches;
        }
        if (matches != 1)
            return EXTRACTPDF_ERROR_FORMAT;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_describe_nodes(
    fz_context *ctx,
    pdf_obj *fields,
    extractpdf_pdf_flatten_form_plan *form)
{
    size_t node_index;

    for (node_index = 0; node_index < form->node_count; ++node_index) {
        extractpdf_pdf_flatten_form_node *node = &form->nodes[node_index];
        pdf_obj *field = NULL;
        pdf_obj *kids = NULL;
        size_t entry_index;
        extractpdf_status status = flatten_form_resolve_locator(
            ctx, fields, node->locator_steps, node->locator_count, &field);
        if (status != EXTRACTPDF_OK)
            return status;

        if (node->locator_count > 1) {
            pdf_obj *parent = NULL;
            pdf_obj *parent_ref;
            status = flatten_form_resolve_locator(
                ctx, fields, node->locator_steps, node->locator_count - 1, &parent);
            if (status != EXTRACTPDF_OK)
                return status;
            parent_ref = pdf_dict_get(ctx, field, PDF_NAME(Parent));
            if (!flatten_form_same_identity(ctx, parent_ref, parent))
                return EXTRACTPDF_ERROR_FORMAT;
        }

        if (extractpdf_pdf_dict_find(ctx, field, PDF_NAME(Kids), &kids)) {
            int count;
            if (!pdf_is_array(ctx, kids))
                return EXTRACTPDF_ERROR_FORMAT;
            count = pdf_array_len(ctx, kids);
            if (count < 0)
                return EXTRACTPDF_ERROR_FORMAT;
            node->kids_present = 1;
            node->original_kid_count = (size_t)count;
        }

        for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
            const extractpdf_pdf_flatten_form_entry *entry =
                &form->entries[entry_index];
            if (entry->merged && flatten_form_entry_belongs_to_node(entry, node))
                node->selected_merged = 1;
        }
    }
    return EXTRACTPDF_OK;
}

static int flatten_form_omit_selected_widget(
    const extractpdf_pdf_flatten_form_plan *form,
    const extractpdf_pdf_flatten_form_node *node,
    size_t kid_index)
{
    size_t entry_index;
    for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
        const extractpdf_pdf_flatten_form_entry *entry = &form->entries[entry_index];
        if (!entry->merged &&
            entry->widget_kid_index == kid_index &&
            flatten_form_entry_belongs_to_node(entry, node))
            return 1;
    }
    return 0;
}

static int flatten_form_omit_removed_child(
    const extractpdf_pdf_flatten_form_plan *form,
    const extractpdf_pdf_flatten_form_node *node,
    size_t kid_index)
{
    size_t child_index;
    for (child_index = 0; child_index < form->node_count; ++child_index) {
        const extractpdf_pdf_flatten_form_node *child = &form->nodes[child_index];
        if (!child->remove ||
            child->locator_count != node->locator_count + 1 ||
            child->locator_steps[child->locator_count - 1] != kid_index ||
            !flatten_form_locator_prefix(
                node->locator_steps,
                node->locator_count,
                child->locator_steps,
                child->locator_count))
            continue;
        return 1;
    }
    return 0;
}

static extractpdf_status flatten_form_compute_node_survivors(
    extractpdf_pdf_flatten_form_plan *form)
{
    size_t max_depth = 0;
    size_t node_index;
    size_t depth;

    for (node_index = 0; node_index < form->node_count; ++node_index)
        if (form->nodes[node_index].locator_count > max_depth)
            max_depth = form->nodes[node_index].locator_count;

    for (depth = max_depth; depth != 0; --depth) {
        for (node_index = 0; node_index < form->node_count; ++node_index) {
            extractpdf_pdf_flatten_form_node *node = &form->nodes[node_index];
            size_t kid_index;
            int changed = 0;

            if (node->locator_count != depth)
                continue;
            if (node->selected_merged) {
                node->remove = 1;
                continue;
            }
            if (!node->kids_present)
                return EXTRACTPDF_ERROR_FORMAT;

            if (node->original_kid_count != 0) {
                if (node->original_kid_count >
                    SIZE_MAX / sizeof(*node->survivor_kid_indices))
                    return EXTRACTPDF_ERROR_NOMEM;
                node->survivor_kid_indices = (size_t *)calloc(
                    node->original_kid_count,
                    sizeof(*node->survivor_kid_indices));
                if (node->survivor_kid_indices == NULL)
                    return EXTRACTPDF_ERROR_NOMEM;
            }

            for (kid_index = 0; kid_index < node->original_kid_count; ++kid_index) {
                if (flatten_form_omit_selected_widget(form, node, kid_index) ||
                    flatten_form_omit_removed_child(form, node, kid_index)) {
                    changed = 1;
                    continue;
                }
                node->survivor_kid_indices[node->survivor_kid_count++] = kid_index;
            }

            if (node->survivor_kid_count == 0)
                node->remove = 1;
            else if (changed)
                node->replace_kids = 1;
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_form_compute_root_survivors(
    fz_context *ctx,
    pdf_obj *fields,
    extractpdf_pdf_flatten_form_plan *form)
{
    int count = pdf_array_len(ctx, fields);
    int index;

    if (count <= 0)
        return EXTRACTPDF_ERROR_FORMAT;
    form->root_field_count = (size_t)count;
    if (form->root_field_count >
        SIZE_MAX / sizeof(*form->root_survivor_indices))
        return EXTRACTPDF_ERROR_NOMEM;
    form->root_survivor_indices = (size_t *)calloc(
        form->root_field_count, sizeof(*form->root_survivor_indices));
    if (form->root_survivor_indices == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    for (index = 0; index < count; ++index) {
        size_t node_index;
        int remove = 0;
        pdf_obj *field = pdf_array_get(ctx, fields, index);

        if (!pdf_is_indirect(ctx, field) || !pdf_is_dict(ctx, field))
            return EXTRACTPDF_ERROR_FORMAT;
        for (node_index = 0; node_index < form->node_count; ++node_index) {
            const extractpdf_pdf_flatten_form_node *node = &form->nodes[node_index];
            if (node->locator_count == 1 &&
                node->locator_steps[0] == (size_t)index &&
                node->remove) {
                remove = 1;
                break;
            }
        }
        if (!remove)
            form->root_survivor_indices[form->root_survivor_count++] =
                (size_t)index;
    }

    if (form->root_survivor_count == 0)
        form->remove_acroform = 1;
    else if (form->root_survivor_count != form->root_field_count)
        form->replace_root_fields = 1;
    return EXTRACTPDF_OK;
}

static int flatten_form_locator_removed(
    const extractpdf_pdf_flatten_form_plan *form,
    const size_t *steps,
    size_t step_count)
{
    size_t node_index;
    for (node_index = 0; node_index < form->node_count; ++node_index) {
        const extractpdf_pdf_flatten_form_node *node = &form->nodes[node_index];
        if (node->remove &&
            flatten_form_locator_prefix(
                node->locator_steps,
                node->locator_count,
                steps,
                step_count))
            return 1;
    }
    return 0;
}

static extractpdf_status flatten_form_preflight_co(
    fz_context *ctx,
    pdf_obj *acroform,
    const extractpdf_pdf_form_provenance *provenance,
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
        entry->survive = !flatten_form_locator_removed(
            form, entry->locator_steps, entry->locator_count);
        if (entry->survive)
            ++form->co_survivor_count;
    }

    if (!form->remove_acroform &&
        form->co_survivor_count != form->co_count)
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
    for (index = 0; index < form->node_count; ++index) {
        free(form->nodes[index].locator_steps);
        free(form->nodes[index].survivor_kid_indices);
    }
    for (index = 0; index < form->co_count; ++index)
        free(form->co_entries[index].locator_steps);
    free(form->co_entries);
    free(form->root_survivor_indices);
    free(form->nodes);
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
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;
    size_t widget_target_count = 0;
    size_t target_index;
    size_t at = 0;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || model == NULL ||
        provenance == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (plan->form != NULL)
        return EXTRACTPDF_ERROR_STATE;

    for (target_index = 0; target_index < plan->target_count; ++target_index)
        if (plan->targets[target_index].kind ==
            EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET)
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
    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        return status;
    (void)root;

    form = (extractpdf_pdf_flatten_form_plan *)calloc(1, sizeof(*form));
    if (form == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    if (widget_target_count > SIZE_MAX / sizeof(*form->entries)) {
        status = EXTRACTPDF_ERROR_NOMEM;
        goto fail;
    }
    form->entries = (extractpdf_pdf_flatten_form_entry *)calloc(
        widget_target_count, sizeof(*form->entries));
    if (form->entries == NULL) {
        status = EXTRACTPDF_ERROR_NOMEM;
        goto fail;
    }
    form->entry_count = widget_target_count;

    for (target_index = 0; target_index < plan->target_count; ++target_index) {
        extractpdf_pdf_flatten_form_entry *entry;
        size_t depth;

        if (plan->targets[target_index].kind !=
            EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET)
            continue;
        entry = &form->entries[at++];
        status = flatten_form_prepare_entry(
            ctx, document, provenance, plan, target_index, entry);
        if (status != EXTRACTPDF_OK)
            goto fail;
        for (depth = 1; depth <= entry->locator_count; ++depth) {
            status = flatten_form_add_node(
                form, entry->locator_steps, depth);
            if (status != EXTRACTPDF_OK)
                goto fail;
        }
    }
    if (at != widget_target_count) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    status = flatten_form_describe_nodes(ctx, fields, form);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = flatten_form_compute_node_survivors(form);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = flatten_form_compute_root_survivors(ctx, fields, form);
    if (status != EXTRACTPDF_OK)
        goto fail;
    status = flatten_form_preflight_co(ctx, acroform, provenance, form);
    if (status != EXTRACTPDF_OK)
        goto fail;

    plan->form = form;
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_flatten_form_drop_plan(form);
    return status;
}

static int flatten_form_steps_equal(
    const size_t *left,
    const size_t *right,
    size_t count)
{
    return count == 0 ||
        (left != NULL && right != NULL &&
         memcmp(left, right, count * sizeof(*left)) == 0);
}

int extractpdf_pdf_flatten_form_plan_equivalent(
    const extractpdf_pdf_flatten_form_plan *left,
    const extractpdf_pdf_flatten_form_plan *right)
{
    size_t index;

    if (left == NULL || right == NULL)
        return left == right;
    if (left->entry_count != right->entry_count ||
        left->node_count != right->node_count ||
        left->root_field_count != right->root_field_count ||
        left->root_survivor_count != right->root_survivor_count ||
        left->co_count != right->co_count ||
        left->co_survivor_count != right->co_survivor_count ||
        left->co_present != right->co_present ||
        left->replace_co != right->replace_co ||
        left->remove_acroform != right->remove_acroform ||
        left->replace_root_fields != right->replace_root_fields)
        return 0;

    if (!flatten_form_steps_equal(
            left->root_survivor_indices,
            right->root_survivor_indices,
            left->root_survivor_count))
        return 0;

    for (index = 0; index < left->entry_count; ++index) {
        const extractpdf_pdf_flatten_form_entry *a = &left->entries[index];
        const extractpdf_pdf_flatten_form_entry *b = &right->entries[index];
        if (a->target_index != b->target_index ||
            a->field_index != b->field_index ||
            a->locator_count != b->locator_count ||
            a->widget_kid_index != b->widget_kid_index ||
            a->merged != b->merged ||
            !flatten_form_steps_equal(
                a->locator_steps, b->locator_steps, a->locator_count))
            return 0;
    }

    for (index = 0; index < left->node_count; ++index) {
        const extractpdf_pdf_flatten_form_node *a = &left->nodes[index];
        const extractpdf_pdf_flatten_form_node *b = &right->nodes[index];
        if (a->locator_count != b->locator_count ||
            a->original_kid_count != b->original_kid_count ||
            a->survivor_kid_count != b->survivor_kid_count ||
            a->kids_present != b->kids_present ||
            a->selected_merged != b->selected_merged ||
            a->remove != b->remove ||
            a->replace_kids != b->replace_kids ||
            !flatten_form_steps_equal(
                a->locator_steps, b->locator_steps, a->locator_count) ||
            !flatten_form_steps_equal(
                a->survivor_kid_indices,
                b->survivor_kid_indices,
                a->survivor_kid_count))
            return 0;
    }

    for (index = 0; index < left->co_count; ++index) {
        const extractpdf_pdf_flatten_form_co_entry *a = &left->co_entries[index];
        const extractpdf_pdf_flatten_form_co_entry *b = &right->co_entries[index];
        if (a->locator_count != b->locator_count ||
            a->survive != b->survive ||
            !flatten_form_steps_equal(
                a->locator_steps, b->locator_steps, a->locator_count))
            return 0;
    }
    return 1;
}

void extractpdf_pdf_flatten_form_drop_runtime(
    fz_context *ctx,
    extractpdf_pdf_flatten_form_runtime *runtime)
{
    size_t index;

    if (runtime == NULL)
        return;
    for (index = 0; index < runtime->node_count; ++index) {
        pdf_drop_obj(ctx, runtime->nodes[index].kids);
        pdf_drop_obj(ctx, runtime->nodes[index].field);
    }
    free(runtime->nodes);
    pdf_drop_obj(ctx, runtime->co);
    pdf_drop_obj(ctx, runtime->fields);
    pdf_drop_obj(ctx, runtime->acroform);
    pdf_drop_obj(ctx, runtime->catalog);
    free(runtime);
}

static extractpdf_status flatten_form_validate_runtime_node(
    fz_context *ctx,
    pdf_obj *fields,
    const extractpdf_pdf_flatten_form_node *node,
    extractpdf_pdf_flatten_form_runtime_node *runtime_node)
{
    pdf_obj *field = NULL;
    pdf_obj *kids = NULL;
    extractpdf_status status = flatten_form_resolve_locator(
        ctx, fields, node->locator_steps, node->locator_count, &field);

    if (status != EXTRACTPDF_OK)
        return status;
    if (node->locator_count > 1) {
        pdf_obj *parent = NULL;
        pdf_obj *parent_ref;
        status = flatten_form_resolve_locator(
            ctx, fields, node->locator_steps, node->locator_count - 1, &parent);
        if (status != EXTRACTPDF_OK)
            return status;
        parent_ref = pdf_dict_get(ctx, field, PDF_NAME(Parent));
        if (!flatten_form_same_identity(ctx, parent_ref, parent))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    if (node->kids_present) {
        if (!extractpdf_pdf_dict_find(ctx, field, PDF_NAME(Kids), &kids) ||
            !pdf_is_array(ctx, kids) ||
            (size_t)pdf_array_len(ctx, kids) != node->original_kid_count)
            return EXTRACTPDF_ERROR_FORMAT;
    } else if (extractpdf_pdf_dict_find(ctx, field, PDF_NAME(Kids), &kids)) {
        return EXTRACTPDF_ERROR_FORMAT;
    }

    runtime_node->field = pdf_keep_obj(ctx, field);
    runtime_node->kids = pdf_keep_obj(ctx, kids);
    return EXTRACTPDF_OK;
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
    pdf_obj *co = NULL;
    size_t node_index;
    size_t entry_index;
    size_t co_index;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    form = plan->form;
    if (form == NULL)
        return EXTRACTPDF_OK;
    if (form->entry_count == 0 || form->node_count == 0)
        return EXTRACTPDF_ERROR_STATE;

    status = flatten_form_get_root_graph(
        ctx, document, &root, &acroform, &fields);
    if (status != EXTRACTPDF_OK)
        return status;
    if ((size_t)pdf_array_len(ctx, fields) != form->root_field_count)
        return EXTRACTPDF_ERROR_FORMAT;

    resolved = (extractpdf_pdf_flatten_form_runtime *)calloc(
        1, sizeof(*resolved));
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    resolved->catalog = pdf_keep_obj(ctx, root);
    resolved->acroform = pdf_keep_obj(ctx, acroform);
    resolved->fields = pdf_keep_obj(ctx, fields);
    resolved->node_count = form->node_count;
    if (resolved->node_count != 0) {
        if (resolved->node_count > SIZE_MAX / sizeof(*resolved->nodes)) {
            status = EXTRACTPDF_ERROR_NOMEM;
            goto fail;
        }
        resolved->nodes = (extractpdf_pdf_flatten_form_runtime_node *)calloc(
            resolved->node_count, sizeof(*resolved->nodes));
        if (resolved->nodes == NULL) {
            status = EXTRACTPDF_ERROR_NOMEM;
            goto fail;
        }
    }

    for (node_index = 0; node_index < form->node_count; ++node_index) {
        status = flatten_form_validate_runtime_node(
            ctx, fields, &form->nodes[node_index], &resolved->nodes[node_index]);
        if (status != EXTRACTPDF_OK)
            goto fail;
    }

    for (entry_index = 0; entry_index < form->entry_count; ++entry_index) {
        const extractpdf_pdf_flatten_form_entry *entry =
            &form->entries[entry_index];
        size_t owner_index = flatten_form_find_node(
            form, entry->locator_steps, entry->locator_count);
        pdf_obj *widget = NULL;
        pdf_obj *field;

        if (owner_index == SIZE_MAX || owner_index >= resolved->node_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        status = flatten_form_target_object(
            ctx, document, plan, entry, &widget);
        if (status != EXTRACTPDF_OK)
            goto fail;
        field = resolved->nodes[owner_index].field;
        if (entry->merged) {
            if (!flatten_form_same_identity(ctx, field, widget)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto fail;
            }
        } else {
            pdf_obj *kids = resolved->nodes[owner_index].kids;
            if (!pdf_is_array(ctx, kids) ||
                entry->widget_kid_index >= (size_t)pdf_array_len(ctx, kids) ||
                !flatten_form_same_identity(
                    ctx,
                    pdf_array_get(ctx, kids, (int)entry->widget_kid_index),
                    widget)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto fail;
            }
        }
    }

    if (form->co_present) {
        if (!extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(CO), &co) ||
            !pdf_is_array(ctx, co) ||
            (size_t)pdf_array_len(ctx, co) != form->co_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            goto fail;
        }
        for (co_index = 0; co_index < form->co_count; ++co_index) {
            const extractpdf_pdf_flatten_form_co_entry *entry =
                &form->co_entries[co_index];
            pdf_obj *co_field = pdf_array_get(ctx, co, (int)co_index);
            pdf_obj *expected = NULL;
            if (!pdf_is_indirect(ctx, co_field) || !pdf_is_dict(ctx, co_field)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto fail;
            }
            status = flatten_form_resolve_locator(
                ctx, fields, entry->locator_steps, entry->locator_count, &expected);
            if (status != EXTRACTPDF_OK)
                goto fail;
            if (!flatten_form_same_identity(ctx, co_field, expected)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                goto fail;
            }
        }
        resolved->co = pdf_keep_obj(ctx, co);
    } else if (extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(CO), &co)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto fail;
    }

    runtime->form = resolved;
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_flatten_form_drop_runtime(ctx, resolved);
    return status;
}

static extractpdf_status flatten_form_replace_node_kids(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_form_node *node,
    const extractpdf_pdf_flatten_form_runtime_node *runtime_node)
{
    pdf_obj *replacement = NULL;
    int caught_code = FZ_ERROR_NONE;
    size_t index;

    if (!node->replace_kids)
        return EXTRACTPDF_OK;
    if (node->remove || !node->kids_present ||
        !pdf_is_dict(ctx, runtime_node->field) ||
        !pdf_is_array(ctx, runtime_node->kids) ||
        (size_t)pdf_array_len(ctx, runtime_node->kids) !=
            node->original_kid_count)
        return EXTRACTPDF_ERROR_FORMAT;

    fz_var(replacement);
    fz_var(caught_code);
    fz_try(ctx)
    {
        replacement = pdf_new_array(
            ctx, document, (int)node->survivor_kid_count);
        for (index = 0; index < node->survivor_kid_count; ++index) {
            size_t original = node->survivor_kid_indices[index];
            if (original >= node->original_kid_count)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Kids survivor invalid");
            pdf_array_push(
                ctx, replacement,
                pdf_array_get(ctx, runtime_node->kids, (int)original));
        }
        pdf_dict_put(ctx, runtime_node->field, PDF_NAME(Kids), replacement);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, replacement);
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

static extractpdf_status flatten_form_remove_pruned_node_kids(
    fz_context *ctx,
    const extractpdf_pdf_flatten_form_node *node,
    const extractpdf_pdf_flatten_form_runtime_node *runtime_node)
{
    int caught_code = FZ_ERROR_NONE;

    if (!node->remove || !node->kids_present || node->survivor_kid_count != 0)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, runtime_node->field) ||
        !pdf_is_array(ctx, runtime_node->kids) ||
        (size_t)pdf_array_len(ctx, runtime_node->kids) !=
            node->original_kid_count)
        return EXTRACTPDF_ERROR_FORMAT;

    fz_var(caught_code);
    fz_try(ctx)
    {
        pdf_dict_del(ctx, runtime_node->field, PDF_NAME(Kids));
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

extractpdf_status extractpdf_pdf_flatten_form_apply(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime)
{
    const extractpdf_pdf_flatten_form_plan *form;
    extractpdf_pdf_flatten_form_runtime *resolved;
    size_t max_depth = 0;
    size_t depth;
    size_t node_index;
    extractpdf_status status;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    form = plan->form;
    if (form == NULL)
        return EXTRACTPDF_OK;
    resolved = runtime->form;
    if (resolved == NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    for (node_index = 0; node_index < form->node_count; ++node_index)
        if (form->nodes[node_index].locator_count > max_depth)
            max_depth = form->nodes[node_index].locator_count;

    for (depth = max_depth; depth != 0; --depth) {
        for (node_index = 0; node_index < form->node_count; ++node_index) {
            if (form->nodes[node_index].locator_count != depth)
                continue;
            status = flatten_form_remove_pruned_node_kids(
                ctx,
                &form->nodes[node_index],
                &resolved->nodes[node_index]);
            if (status != EXTRACTPDF_OK)
                return status;
        }
    }

    if (form->remove_acroform) {
        int caught_code = FZ_ERROR_NONE;
        fz_var(caught_code);
        fz_try(ctx)
        {
            pdf_dict_del(ctx, resolved->catalog, PDF_NAME(AcroForm));
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

    for (depth = max_depth; depth != 0; --depth) {
        for (node_index = 0; node_index < form->node_count; ++node_index) {
            if (form->nodes[node_index].locator_count != depth ||
                !form->nodes[node_index].replace_kids)
                continue;
            status = flatten_form_replace_node_kids(
                ctx,
                document,
                &form->nodes[node_index],
                &resolved->nodes[node_index]);
            if (status != EXTRACTPDF_OK)
                return status;
        }
    }

    if (form->replace_root_fields) {
        pdf_obj *replacement = NULL;
        int caught_code = FZ_ERROR_NONE;
        size_t index;

        fz_var(replacement);
        fz_var(caught_code);
        fz_try(ctx)
        {
            if (!pdf_is_array(ctx, resolved->fields) ||
                (size_t)pdf_array_len(ctx, resolved->fields) !=
                    form->root_field_count)
                fz_throw(
                    ctx, FZ_ERROR_FORMAT,
                    "flatten Fields changed after preflight");
            replacement = pdf_new_array(
                ctx, document, (int)form->root_survivor_count);
            for (index = 0; index < form->root_survivor_count; ++index) {
                size_t original = form->root_survivor_indices[index];
                if (original >= form->root_field_count)
                    fz_throw(
                        ctx, FZ_ERROR_FORMAT,
                        "flatten Fields survivor invalid");
                pdf_array_push(
                    ctx, replacement,
                    pdf_array_get(ctx, resolved->fields, (int)original));
            }
            pdf_dict_put(ctx, resolved->acroform, PDF_NAME(Fields), replacement);
        }
        fz_always(ctx)
        {
            pdf_drop_obj(ctx, replacement);
        }
        fz_catch(ctx)
        {
            caught_code = fz_caught(ctx);
            fz_report_error(ctx);
        }
        if (caught_code != FZ_ERROR_NONE)
            return extractpdf_status_from_mupdf(caught_code);
    }

    if (form->replace_co) {
        pdf_obj *replacement = NULL;
        int caught_code = FZ_ERROR_NONE;
        size_t index;
        size_t kept = 0;

        fz_var(replacement);
        fz_var(caught_code);
        fz_try(ctx)
        {
            if (!form->co_present ||
                !pdf_is_array(ctx, resolved->co) ||
                (size_t)pdf_array_len(ctx, resolved->co) != form->co_count)
                fz_throw(
                    ctx, FZ_ERROR_FORMAT,
                    "flatten CO changed after preflight");
            replacement = pdf_new_array(
                ctx, document, (int)form->co_survivor_count);
            for (index = 0; index < form->co_count; ++index) {
                if (!form->co_entries[index].survive)
                    continue;
                pdf_array_push(
                    ctx, replacement,
                    pdf_array_get(ctx, resolved->co, (int)index));
                ++kept;
            }
            if (kept != form->co_survivor_count)
                fz_throw(
                    ctx, FZ_ERROR_FORMAT,
                    "flatten CO survivor count changed");
            pdf_dict_put(ctx, resolved->acroform, PDF_NAME(CO), replacement);
        }
        fz_always(ctx)
        {
            pdf_drop_obj(ctx, replacement);
        }
        fz_catch(ctx)
        {
            caught_code = fz_caught(ctx);
            fz_report_error(ctx);
        }
        if (caught_code != FZ_ERROR_NONE)
            return extractpdf_status_from_mupdf(caught_code);
    }

    return EXTRACTPDF_OK;
}
