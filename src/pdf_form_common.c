#include "pdf_form_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_pdf_form_node {
    pdf_obj *object;
    size_t parent_node;
    size_t group_index;
    size_t depth;
    int has_local_t;
    int is_widget;
} extractpdf_pdf_form_node;

typedef struct extractpdf_pdf_form_group {
    size_t head_node;
    size_t parent_group;
    size_t public_index;
    int has_named_child;
    int has_widget;
    int name_present;
    char *full_name;
    size_t full_name_size;
} extractpdf_pdf_form_group;

typedef struct extractpdf_pdf_form_effective {
    pdf_obj *value;
    size_t owner_node;
    int present;
} extractpdf_pdf_form_effective;

typedef struct extractpdf_pdf_form_parse_state {
    fz_context *ctx;
    pdf_document *document;
    extractpdf_pdf_form_model *model;
    extractpdf_pdf_form_provenance *provenance;
    extractpdf_pdf_form_node *nodes;
    size_t node_count;
    size_t node_capacity;
    extractpdf_pdf_form_group *groups;
    size_t group_count;
    size_t group_capacity;
} extractpdf_pdf_form_parse_state;

static void extractpdf_pdf_form_free_transient(
    extractpdf_pdf_form_parse_state *state)
{
    size_t index;

    if (state == NULL)
        return;
    for (index = 0; index < state->group_count; ++index)
        free(state->groups[index].full_name);
    free(state->groups);
    free(state->nodes);
    state->groups = NULL;
    state->nodes = NULL;
    state->group_count = 0;
    state->node_count = 0;
}

void extractpdf_pdf_form_drop_model(
    extractpdf_pdf_form_model *model)
{
    size_t index;

    if (model == NULL)
        return;
    for (index = 0; index < model->option_count; ++index)
        free(model->options[index].button_state);
    free(model->fields);
    free(model->values);
    free(model->options);
    free(model->widgets);
    free(model->strings);
    free(model);
}

void extractpdf_pdf_form_drop_provenance(
    fz_context *ctx,
    extractpdf_pdf_form_provenance *provenance)
{
    size_t field_index;

    if (provenance == NULL)
        return;
    for (field_index = 0; field_index < provenance->field_count; ++field_index) {
        extractpdf_pdf_form_live_field *field =
            &provenance->fields[field_index];
        size_t index;

        if (field->group_head != NULL)
            pdf_drop_obj(ctx, field->group_head);
        for (index = 0; index < field->group_node_count; ++index)
            pdf_drop_obj(ctx, field->group_nodes[index]);
        if (field->effective_v_owner != NULL)
            pdf_drop_obj(ctx, field->effective_v_owner);
        for (index = 0; index < field->widget_count; ++index)
            pdf_drop_obj(ctx, field->widgets[index].object);
        free(field->group_nodes);
        free(field->widgets);
    }
    free(provenance->fields);
    free(provenance);
}

static extractpdf_pdf_form_model *extractpdf_pdf_form_new_model(void)
{
    return (extractpdf_pdf_form_model *)calloc(
        1, sizeof(extractpdf_pdf_form_model));
}

static extractpdf_status extractpdf_pdf_form_reserve_nodes(
    extractpdf_pdf_form_parse_state *state,
    size_t required)
{
    size_t capacity;
    extractpdf_pdf_form_node *grown;

    if (required <= state->node_capacity)
        return EXTRACTPDF_OK;
    capacity = state->node_capacity != 0 ? state->node_capacity : 16;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*state->nodes))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_form_node *)realloc(
        state->nodes, capacity * sizeof(*state->nodes));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    state->nodes = grown;
    state->node_capacity = capacity;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_reserve_groups(
    extractpdf_pdf_form_parse_state *state,
    size_t required)
{
    size_t capacity;
    extractpdf_pdf_form_group *grown;

    if (required <= state->group_capacity)
        return EXTRACTPDF_OK;
    capacity = state->group_capacity != 0 ? state->group_capacity : 8;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if (capacity < required ||
        capacity > SIZE_MAX / sizeof(*state->groups))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_form_group *)realloc(
        state->groups, capacity * sizeof(*state->groups));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    state->groups = grown;
    state->group_capacity = capacity;
    return EXTRACTPDF_OK;
}

int extractpdf_pdf_form_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    int left_indirect = pdf_is_indirect(ctx, left);
    int right_indirect = pdf_is_indirect(ctx, right);

    if (left_indirect || right_indirect) {
        if (!left_indirect || !right_indirect)
            return 0;
        return pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
            pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
    }
    return left == right;
}

static int extractpdf_pdf_form_seen_identity(
    extractpdf_pdf_form_parse_state *state,
    pdf_obj *object)
{
    size_t index;

    for (index = 0; index < state->node_count; ++index) {
        if (extractpdf_pdf_form_same_identity(
                state->ctx, state->nodes[index].object, object))
            return 1;
    }
    return 0;
}

static extractpdf_status extractpdf_pdf_form_copy_text(
    fz_context *ctx,
    pdf_obj *string,
    char **out_text,
    size_t *out_size)
{
    const char *text;
    size_t size;
    char *copy;

    *out_text = NULL;
    *out_size = 0;
    if (!pdf_is_string(ctx, string))
        return EXTRACTPDF_ERROR_FORMAT;
    text = pdf_to_text_string(ctx, string);
    if (text == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    size = strlen(text);
    copy = (char *)malloc(size + 1);
    if (copy == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    memcpy(copy, text, size + 1);
    *out_text = copy;
    *out_size = size;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_make_group_name(
    extractpdf_pdf_form_parse_state *state,
    size_t parent_group,
    int local_t_present,
    const char *partial,
    size_t partial_size,
    char **out_name,
    size_t *out_size,
    int *out_present)
{
    const extractpdf_pdf_form_group *parent = NULL;
    size_t size;
    char *name;

    *out_name = NULL;
    *out_size = 0;
    *out_present = 0;

    if (!local_t_present) {
        if (parent_group != SIZE_MAX) {
            parent = &state->groups[parent_group];
            if (parent->name_present) {
                name = (char *)malloc(parent->full_name_size + 1);
                if (name == NULL)
                    return EXTRACTPDF_ERROR_NOMEM;
                memcpy(name, parent->full_name, parent->full_name_size + 1);
                *out_name = name;
                *out_size = parent->full_name_size;
                *out_present = 1;
            }
        }
        return EXTRACTPDF_OK;
    }

    if (parent_group != SIZE_MAX)
        parent = &state->groups[parent_group];

    if (parent != NULL && parent->name_present) {
        if (parent->full_name_size > SIZE_MAX - partial_size - 2)
            return EXTRACTPDF_ERROR_NOMEM;
        size = parent->full_name_size + 1 + partial_size;
        name = (char *)malloc(size + 1);
        if (name == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        memcpy(name, parent->full_name, parent->full_name_size);
        name[parent->full_name_size] = '.';
        memcpy(name + parent->full_name_size + 1, partial, partial_size);
        name[size] = '\0';
    } else {
        size = partial_size;
        name = (char *)malloc(size + 1);
        if (name == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        memcpy(name, partial, partial_size);
        name[size] = '\0';
    }

    *out_name = name;
    *out_size = size;
    *out_present = 1;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_add_group(
    extractpdf_pdf_form_parse_state *state,
    size_t head_node,
    size_t parent_group,
    int local_t_present,
    const char *partial,
    size_t partial_size,
    size_t *out_group)
{
    extractpdf_pdf_form_group *group;
    extractpdf_status status;

    status = extractpdf_pdf_form_reserve_groups(state, state->group_count + 1);
    if (status != EXTRACTPDF_OK)
        return status;

    group = &state->groups[state->group_count];
    memset(group, 0, sizeof(*group));
    group->head_node = head_node;
    group->parent_group = parent_group;
    group->public_index = SIZE_MAX;

    status = extractpdf_pdf_form_make_group_name(
        state, parent_group, local_t_present, partial, partial_size,
        &group->full_name, &group->full_name_size, &group->name_present);
    if (status != EXTRACTPDF_OK)
        return status;

    *out_group = state->group_count;
    ++state->group_count;
    if (parent_group != SIZE_MAX && local_t_present)
        state->groups[parent_group].has_named_child = 1;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_form_is_widget(
    extractpdf_pdf_form_parse_state *state,
    pdf_obj *object)
{
    pdf_obj *subtype = NULL;

    if (!extractpdf_pdf_dict_find(state->ctx, object, PDF_NAME(Subtype), &subtype))
        return 0;
    return pdf_is_name(state->ctx, subtype) &&
        pdf_name_eq(state->ctx, subtype, PDF_NAME(Widget));
}

static extractpdf_status extractpdf_pdf_form_traverse(
    extractpdf_pdf_form_parse_state *state,
    pdf_obj *object,
    size_t parent_node,
    size_t parent_group,
    size_t depth,
    int top_level)
{
    pdf_obj *parent_obj = NULL;
    pdf_obj *t_obj = NULL;
    pdf_obj *kids = NULL;
    char *partial = NULL;
    size_t partial_size = 0;
    size_t node_index;
    size_t group_index;
    int has_parent;
    int has_local_t;
    int is_widget;
    extractpdf_status status;
    int index;
    int count;

    if (!pdf_is_dict(state->ctx, object))
        return EXTRACTPDF_ERROR_FORMAT;
    if (extractpdf_pdf_form_seen_identity(state, object))
        return EXTRACTPDF_ERROR_FORMAT;
    if (depth > 256)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    has_parent = extractpdf_pdf_dict_find(state->ctx, object, PDF_NAME(Parent), &parent_obj);
    if (top_level) {
        if (has_parent && !pdf_is_null(state->ctx, parent_obj))
            return EXTRACTPDF_ERROR_FORMAT;
    } else {
        if (!has_parent || parent_node == SIZE_MAX ||
            !extractpdf_pdf_form_same_identity(
                state->ctx, parent_obj, state->nodes[parent_node].object))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    has_local_t = extractpdf_pdf_dict_find(state->ctx, object, PDF_NAME(T), &t_obj);
    if (has_local_t) {
        status = extractpdf_pdf_form_copy_text(state->ctx, t_obj, &partial, &partial_size);
        if (status != EXTRACTPDF_OK)
            return status;
        if (strchr(partial, '.') != NULL) {
            free(partial);
            return EXTRACTPDF_ERROR_FORMAT;
        }
    }

    is_widget = extractpdf_pdf_form_is_widget(state, object);
    node_index = state->node_count;
    if (top_level || has_local_t) {
        status = extractpdf_pdf_form_add_group(
            state, node_index, top_level ? SIZE_MAX : parent_group,
            has_local_t, partial != NULL ? partial : "", partial_size,
            &group_index);
        if (status != EXTRACTPDF_OK) {
            free(partial);
            return status;
        }
    } else {
        group_index = parent_group;
    }
    free(partial);

    status = extractpdf_pdf_form_reserve_nodes(state, state->node_count + 1);
    if (status != EXTRACTPDF_OK)
        return status;
    state->nodes[node_index].object = object;
    state->nodes[node_index].parent_node = parent_node;
    state->nodes[node_index].group_index = group_index;
    state->nodes[node_index].depth = depth;
    state->nodes[node_index].has_local_t = has_local_t;
    state->nodes[node_index].is_widget = is_widget;
    ++state->node_count;

    if (is_widget) {
        state->groups[group_index].has_widget = 1;
        return EXTRACTPDF_OK;
    }

    if (!extractpdf_pdf_dict_find(state->ctx, object, PDF_NAME(Kids), &kids))
        return EXTRACTPDF_OK;
    if (!pdf_is_array(state->ctx, kids))
        return EXTRACTPDF_ERROR_FORMAT;

    count = pdf_array_len(state->ctx, kids);
    for (index = 0; index < count; ++index) {
        pdf_obj *child = pdf_array_get(state->ctx, kids, index);
        status = extractpdf_pdf_form_traverse(
            state, child, node_index, group_index, depth + 1, 0);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_pdf_form_effective extractpdf_pdf_form_effective_value(
    extractpdf_pdf_form_parse_state *state,
    size_t node_index,
    pdf_obj *key)
{
    extractpdf_pdf_form_effective result;

    result.value = NULL;
    result.owner_node = SIZE_MAX;
    result.present = 0;
    while (node_index != SIZE_MAX) {
        pdf_obj *value = NULL;
        if (extractpdf_pdf_dict_find(
                state->ctx, state->nodes[node_index].object, key, &value)) {
            result.value = value;
            result.owner_node = node_index;
            result.present = 1;
            break;
        }
        node_index = state->nodes[node_index].parent_node;
    }
    return result;
}

static int extractpdf_pdf_form_effective_equal(
    extractpdf_pdf_form_parse_state *state,
    extractpdf_pdf_form_effective left,
    extractpdf_pdf_form_effective right)
{
    if (left.present != right.present)
        return 0;
    if (!left.present)
        return 1;
    if (left.value == right.value)
        return 1;
    return pdf_objcmp_resolve(state->ctx, left.value, right.value) == 0;
}

static extractpdf_status extractpdf_pdf_form_effective_flags(
    extractpdf_pdf_form_parse_state *state,
    size_t node_index,
    uint32_t *out_flags)
{
    extractpdf_pdf_form_effective effective;
    int64_t value;

    *out_flags = 0;
    effective = extractpdf_pdf_form_effective_value(state, node_index, PDF_NAME(Ff));
    if (!effective.present)
        return EXTRACTPDF_OK;
    if (!pdf_is_int(state->ctx, effective.value))
        return EXTRACTPDF_ERROR_FORMAT;
    value = pdf_to_int64(state->ctx, effective.value);
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return EXTRACTPDF_ERROR_FORMAT;
    *out_flags = (uint32_t)value;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_validate_group(
    extractpdf_pdf_form_parse_state *state,
    size_t group_index)
{
    const extractpdf_pdf_form_group *group = &state->groups[group_index];
    extractpdf_pdf_form_effective ft_reference;
    extractpdf_pdf_form_effective v_reference;
    extractpdf_pdf_form_effective opt_reference;
    extractpdf_pdf_form_effective i_reference;
    uint32_t flags_reference;
    extractpdf_status status;
    size_t index;

    ft_reference = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(FT));
    v_reference = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(V));
    opt_reference = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(Opt));
    i_reference = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(I));
    status = extractpdf_pdf_form_effective_flags(state, group->head_node, &flags_reference);
    if (status != EXTRACTPDF_OK)
        return status;

    for (index = 0; index < state->node_count; ++index) {
        extractpdf_pdf_form_effective current;
        uint32_t flags;

        if (state->nodes[index].group_index != group_index)
            continue;
        current = extractpdf_pdf_form_effective_value(state, index, PDF_NAME(FT));
        if (!extractpdf_pdf_form_effective_equal(state, ft_reference, current))
            return EXTRACTPDF_ERROR_FORMAT;
        status = extractpdf_pdf_form_effective_flags(state, index, &flags);
        if (status != EXTRACTPDF_OK)
            return status;
        if (flags != flags_reference)
            return EXTRACTPDF_ERROR_FORMAT;
        current = extractpdf_pdf_form_effective_value(state, index, PDF_NAME(V));
        if (!extractpdf_pdf_form_effective_equal(state, v_reference, current))
            return EXTRACTPDF_ERROR_FORMAT;
        current = extractpdf_pdf_form_effective_value(state, index, PDF_NAME(Opt));
        if (!extractpdf_pdf_form_effective_equal(state, opt_reference, current))
            return EXTRACTPDF_ERROR_FORMAT;
        current = extractpdf_pdf_form_effective_value(state, index, PDF_NAME(I));
        if (!extractpdf_pdf_form_effective_equal(state, i_reference, current))
            return EXTRACTPDF_ERROR_FORMAT;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_classify_field(
    extractpdf_pdf_form_parse_state *state,
    pdf_obj *ft,
    uint32_t flags,
    extractpdf_form_field_type *out_type,
    int *out_multiselect)
{
    *out_type = EXTRACTPDF_FORM_FIELD_UNKNOWN;
    *out_multiselect = 0;
    if (!pdf_is_name(state->ctx, ft))
        return EXTRACTPDF_ERROR_FORMAT;

    if (pdf_name_eq(state->ctx, ft, PDF_NAME(Btn))) {
        int push = (flags & PDF_BTN_FIELD_IS_PUSHBUTTON) != 0;
        int radio = (flags & PDF_BTN_FIELD_IS_RADIO) != 0;
        if (push && radio)
            return EXTRACTPDF_ERROR_FORMAT;
        *out_type = push ? EXTRACTPDF_FORM_FIELD_PUSH_BUTTON :
            (radio ? EXTRACTPDF_FORM_FIELD_RADIO_BUTTON : EXTRACTPDF_FORM_FIELD_CHECKBOX);
        return EXTRACTPDF_OK;
    }
    if (pdf_name_eq(state->ctx, ft, PDF_NAME(Tx))) {
        *out_type = EXTRACTPDF_FORM_FIELD_TEXT;
        return EXTRACTPDF_OK;
    }
    if (pdf_name_eq(state->ctx, ft, PDF_NAME(Ch))) {
        int combo = (flags & PDF_CH_FIELD_IS_COMBO) != 0;
        int multiselect = (flags & PDF_CH_FIELD_IS_MULTI_SELECT) != 0;
        if (combo && multiselect)
            return EXTRACTPDF_ERROR_FORMAT;
        if (combo)
            *out_type = EXTRACTPDF_FORM_FIELD_COMBO_BOX;
        else {
            *out_type = EXTRACTPDF_FORM_FIELD_LIST_BOX;
            *out_multiselect = multiselect;
        }
        return EXTRACTPDF_OK;
    }
    if (pdf_name_eq(state->ctx, ft, PDF_NAME(Sig))) {
        *out_type = EXTRACTPDF_FORM_FIELD_SIGNATURE;
        return EXTRACTPDF_OK;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_append_string(
    extractpdf_pdf_form_model *model,
    const char *text,
    size_t size,
    int present,
    extractpdf_pdf_form_string *out_string)
{
    size_t required;
    size_t capacity;
    char *grown;

    out_string->offset = 0;
    out_string->size = 0;
    out_string->present = 0;
    if (!present)
        return EXTRACTPDF_OK;
    if (model->string_size > SIZE_MAX - size - 1)
        return EXTRACTPDF_ERROR_NOMEM;
    required = model->string_size + size + 1;
    if (required > model->string_capacity) {
        capacity = model->string_capacity != 0 ? model->string_capacity : 64;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        if (capacity < required)
            return EXTRACTPDF_ERROR_NOMEM;
        grown = (char *)realloc(model->strings, capacity);
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        model->strings = grown;
        model->string_capacity = capacity;
    }
    out_string->offset = model->string_size;
    out_string->size = size;
    out_string->present = 1;
    memcpy(model->strings + model->string_size, text, size);
    model->strings[model->string_size + size] = '\0';
    model->string_size = required;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_materialize_label(
    extractpdf_pdf_form_parse_state *state,
    size_t node_index,
    extractpdf_pdf_form_string *out_label)
{
    extractpdf_pdf_form_effective effective;
    const char *text;
    size_t size;

    effective = extractpdf_pdf_form_effective_value(state, node_index, PDF_NAME(TU));
    if (!effective.present)
        return extractpdf_pdf_form_append_string(state->model, "", 0, 0, out_label);
    if (!pdf_is_string(state->ctx, effective.value))
        return EXTRACTPDF_ERROR_FORMAT;
    text = pdf_to_text_string(state->ctx, effective.value);
    if (text == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    size = strlen(text);
    return extractpdf_pdf_form_append_string(state->model, text, size, 1, out_label);
}

static int extractpdf_pdf_form_duplicate_name(
    extractpdf_pdf_form_parse_state *state,
    size_t group_index)
{
    const extractpdf_pdf_form_group *group = &state->groups[group_index];
    size_t index;

    if (!group->name_present || group->full_name_size == 0)
        return 0;
    for (index = 0; index < group_index; ++index) {
        const extractpdf_pdf_form_group *other = &state->groups[index];
        if (other->public_index == SIZE_MAX || !other->name_present ||
            other->full_name_size != group->full_name_size)
            continue;
        if (memcmp(other->full_name, group->full_name, group->full_name_size) == 0)
            return 1;
    }
    return 0;
}

static extractpdf_status extractpdf_pdf_form_materialize_fields(
    extractpdf_pdf_form_parse_state *state)
{
    size_t terminal_count = 0;
    size_t group_index;
    size_t field_index = 0;

    for (group_index = 0; group_index < state->group_count; ++group_index) {
        extractpdf_pdf_form_group *group = &state->groups[group_index];
        if (group->has_widget && group->has_named_child)
            return EXTRACTPDF_ERROR_FORMAT;
        if (!group->has_named_child)
            ++terminal_count;
    }
    if (terminal_count > SIZE_MAX / sizeof(*state->model->fields))
        return EXTRACTPDF_ERROR_NOMEM;
    if (terminal_count != 0) {
        state->model->fields = (extractpdf_pdf_form_field_internal *)calloc(
            terminal_count, sizeof(*state->model->fields));
        if (state->model->fields == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
    }
    state->model->field_count = terminal_count;

    for (group_index = 0; group_index < state->group_count; ++group_index) {
        extractpdf_pdf_form_group *group = &state->groups[group_index];
        extractpdf_pdf_form_field_internal *field;
        extractpdf_pdf_form_effective ft;
        extractpdf_pdf_form_effective v;
        extractpdf_status status;
        uint32_t flags;
        int multiselect = 0;

        status = extractpdf_pdf_form_validate_group(state, group_index);
        if (status != EXTRACTPDF_OK)
            return status;
        if (group->has_named_child)
            continue;
        if (extractpdf_pdf_form_duplicate_name(state, group_index))
            return EXTRACTPDF_ERROR_FORMAT;

        ft = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(FT));
        if (!ft.present)
            return EXTRACTPDF_ERROR_FORMAT;
        status = extractpdf_pdf_form_effective_flags(state, group->head_node, &flags);
        if (status != EXTRACTPDF_OK)
            return status;

        field = &state->model->fields[field_index];
        status = extractpdf_pdf_form_classify_field(
            state, ft.value, flags, &field->type, &multiselect);
        if (status != EXTRACTPDF_OK)
            return status;
        field->flags = flags;
        field->is_multiselect = multiselect;
        field->is_signed = 0;
        v = extractpdf_pdf_form_effective_value(state, group->head_node, PDF_NAME(V));
        switch (field->type) {
        case EXTRACTPDF_FORM_FIELD_PUSH_BUTTON:
        case EXTRACTPDF_FORM_FIELD_SIGNATURE:
        case EXTRACTPDF_FORM_FIELD_UNKNOWN:
            field->value_presence = EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;
            break;
        default:
            field->value_presence = v.present ?
                EXTRACTPDF_FORM_VALUE_PRESENT : EXTRACTPDF_FORM_VALUE_MISSING;
            break;
        }
        status = extractpdf_pdf_form_append_string(
            state->model,
            group->full_name != NULL ? group->full_name : "",
            group->full_name_size,
            group->name_present,
            &field->name);
        if (status != EXTRACTPDF_OK)
            return status;
        status = extractpdf_pdf_form_materialize_label(
            state, group->head_node, &field->label);
        if (status != EXTRACTPDF_OK)
            return status;
        group->public_index = field_index;
        ++field_index;
    }
    return field_index == terminal_count ? EXTRACTPDF_OK : EXTRACTPDF_ERROR_FORMAT;
}

static extractpdf_status extractpdf_pdf_form_materialize_provenance(
    extractpdf_pdf_form_parse_state *state)
{
    extractpdf_pdf_form_provenance *provenance;
    size_t group_index;

    provenance = (extractpdf_pdf_form_provenance *)calloc(1, sizeof(*provenance));
    if (provenance == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    state->provenance = provenance;
    provenance->field_count = state->model->field_count;
    if (provenance->field_count != 0) {
        if (provenance->field_count > SIZE_MAX / sizeof(*provenance->fields))
            return EXTRACTPDF_ERROR_NOMEM;
        provenance->fields = (extractpdf_pdf_form_live_field *)calloc(
            provenance->field_count, sizeof(*provenance->fields));
        if (provenance->fields == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
    }

    for (group_index = 0; group_index < state->group_count; ++group_index) {
        const extractpdf_pdf_form_group *group = &state->groups[group_index];
        extractpdf_pdf_form_live_field *live;
        extractpdf_pdf_form_effective effective;
        size_t node_index;
        size_t node_count = 0;
        size_t at = 0;

        if (group->public_index == SIZE_MAX)
            continue;
        if (group->public_index >= provenance->field_count)
            return EXTRACTPDF_ERROR_FORMAT;
        live = &provenance->fields[group->public_index];
        live->group_head = pdf_keep_obj(
            state->ctx, state->nodes[group->head_node].object);

        for (node_index = 0; node_index < state->node_count; ++node_index)
            if (state->nodes[node_index].group_index == group_index)
                ++node_count;
        if (node_count != 0) {
            if (node_count > SIZE_MAX / sizeof(*live->group_nodes))
                return EXTRACTPDF_ERROR_NOMEM;
            live->group_nodes = (pdf_obj **)calloc(
                node_count, sizeof(*live->group_nodes));
            if (live->group_nodes == NULL)
                return EXTRACTPDF_ERROR_NOMEM;
        }
        live->group_node_count = node_count;
        for (node_index = 0; node_index < state->node_count; ++node_index) {
            if (state->nodes[node_index].group_index != group_index)
                continue;
            live->group_nodes[at++] = pdf_keep_obj(
                state->ctx, state->nodes[node_index].object);
        }
        if (at != node_count)
            return EXTRACTPDF_ERROR_FORMAT;

        effective = extractpdf_pdf_form_effective_value(
            state, group->head_node, PDF_NAME(V));
        if (effective.present) {
            if (effective.owner_node >= state->node_count)
                return EXTRACTPDF_ERROR_FORMAT;
            live->effective_v_present = 1;
            live->effective_v_owner = pdf_keep_obj(
                state->ctx, state->nodes[effective.owner_node].object);
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_form_parse_impl(
    extractpdf_pdf_form_parse_state *state)
{
    pdf_obj *trailer;
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;
    extractpdf_status status;
    int index;
    int count;

    state->model = extractpdf_pdf_form_new_model();
    if (state->model == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    trailer = pdf_trailer(state->ctx, state->document);
    if (!extractpdf_pdf_dict_find(state->ctx, trailer, PDF_NAME(Root), &root) ||
        !pdf_is_dict(state->ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(state->ctx, root, PDF_NAME(AcroForm), &acroform))
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(state->ctx, acroform))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(state->ctx, acroform, PDF_NAME(Fields), &fields))
        return EXTRACTPDF_OK;
    if (!pdf_is_array(state->ctx, fields))
        return EXTRACTPDF_ERROR_FORMAT;
    count = pdf_array_len(state->ctx, fields);
    if (count == 0)
        return EXTRACTPDF_OK;

    for (index = 0; index < count; ++index) {
        pdf_obj *field = pdf_array_get(state->ctx, fields, index);
        status = extractpdf_pdf_form_traverse(state, field, SIZE_MAX, SIZE_MAX, 1, 1);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return extractpdf_pdf_form_materialize_fields(state);
}

static extractpdf_status extractpdf_pdf_form_parse_internal(
    fz_context *ctx,
    pdf_document *document,
    int want_provenance,
    extractpdf_pdf_form_model **out_model,
    extractpdf_pdf_form_provenance **out_provenance)
{
    extractpdf_pdf_form_parse_state *state;
    extractpdf_status status = EXTRACTPDF_OK;

    if (out_model == NULL || (want_provenance && out_provenance == NULL))
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_model = NULL;
    if (out_provenance != NULL)
        *out_provenance = NULL;
    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    state = (extractpdf_pdf_form_parse_state *)calloc(1, sizeof(*state));
    if (state == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    state->ctx = ctx;
    state->document = document;

    fz_try(ctx)
    {
        status = extractpdf_pdf_form_parse_impl(state);
        if (status == EXTRACTPDF_OK && want_provenance)
            status = extractpdf_pdf_form_materialize_provenance(state);
    }
    fz_always(ctx)
    {
        extractpdf_pdf_form_free_transient(state);
    }
    fz_catch(ctx)
    {
        extractpdf_pdf_form_drop_model(state->model);
        extractpdf_pdf_form_drop_provenance(ctx, state->provenance);
        free(state);
        fz_rethrow(ctx);
    }

    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_model(state->model);
        extractpdf_pdf_form_drop_provenance(ctx, state->provenance);
        free(state);
        return status;
    }
    *out_model = state->model;
    if (out_provenance != NULL)
        *out_provenance = state->provenance;
    free(state);
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_form_parse(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model **out_model)
{
    return extractpdf_pdf_form_parse_internal(
        ctx, document, 0, out_model, NULL);
}

extractpdf_status extractpdf_pdf_form_build(
    fz_context *ctx,
    pdf_document *document,
    int want_provenance,
    extractpdf_pdf_form_model **out_model,
    extractpdf_pdf_form_provenance **out_provenance)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (out_model == NULL || (want_provenance && out_provenance == NULL))
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_model = NULL;
    if (out_provenance != NULL)
        *out_provenance = NULL;
    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(model);
    fz_var(provenance);
    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = extractpdf_pdf_form_parse_internal(
            ctx, document, want_provenance, &model,
            want_provenance ? &provenance : NULL);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_form_reconcile_widgets(ctx, document, model);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_form_materialize_scalar_values(
                ctx, document, model);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_pdf_form_drop_model(model);
        extractpdf_pdf_form_drop_provenance(ctx, provenance);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_model(model);
        extractpdf_pdf_form_drop_provenance(ctx, provenance);
        return status;
    }
    *out_model = model;
    if (out_provenance != NULL)
        *out_provenance = provenance;
    return EXTRACTPDF_OK;
}
