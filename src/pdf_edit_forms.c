#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXTRACTPDF_FORM_REF_DOMAIN UINT64_C(0x464f524d5f524546)

struct extractpdf_form {
    extractpdf_pdf_form_model *model;
};

typedef struct extractpdf_pdf_edit_form_group_scan {
    pdf_obj *head;
    size_t parent_group;
    int has_named_child;
} extractpdf_pdf_edit_form_group_scan;

typedef struct extractpdf_pdf_edit_form_scan {
    fz_context *ctx;
    extractpdf_pdf_edit_form_group_scan *groups;
    size_t group_count;
    size_t group_capacity;
} extractpdf_pdf_edit_form_scan;

static uint64_t extractpdf_pdf_edit_form_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static int extractpdf_pdf_edit_form_same_identity(
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

static extractpdf_status extractpdf_pdf_edit_form_build_model(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_form_model **out_model)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    *out_model = NULL;
    fz_var(model);
    fz_var(status);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        status = extractpdf_pdf_form_parse(edit->ctx, edit->document, &model);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_form_reconcile_widgets(
                edit->ctx, edit->document, model);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_form_materialize_scalar_values(
                edit->ctx, edit->document, model);
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_pdf_form_drop_model(model);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_model(model);
        return status;
    }
    *out_model = model;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_form_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_form **out_form)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_form *form;
    extractpdf_status status;

    if (out_form == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_form = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_form_build_model(edit, &model);
    if (status != EXTRACTPDF_OK)
        return status;
    form = (extractpdf_form *)calloc(1, sizeof(*form));
    if (form == NULL) {
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    form->model = model;
    *out_form = form;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_form_scan_reserve(
    extractpdf_pdf_edit_form_scan *scan,
    size_t needed)
{
    extractpdf_pdf_edit_form_group_scan *grown;
    size_t capacity;

    if (needed <= scan->group_capacity)
        return EXTRACTPDF_OK;
    capacity = scan->group_capacity != 0 ? scan->group_capacity : 8;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*scan->groups))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_edit_form_group_scan *)realloc(
        scan->groups, capacity * sizeof(*scan->groups));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    scan->groups = grown;
    scan->group_capacity = capacity;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_form_scan_is_widget(
    extractpdf_pdf_edit_form_scan *scan,
    pdf_obj *object)
{
    pdf_obj *subtype = NULL;

    return extractpdf_pdf_dict_find(
            scan->ctx, object, PDF_NAME(Subtype), &subtype) &&
        pdf_is_name(scan->ctx, subtype) &&
        pdf_name_eq(scan->ctx, subtype, PDF_NAME(Widget));
}

static extractpdf_status extractpdf_pdf_edit_form_scan_node(
    extractpdf_pdf_edit_form_scan *scan,
    pdf_obj *object,
    size_t parent_group,
    int top_level)
{
    pdf_obj *t = NULL;
    pdf_obj *kids = NULL;
    size_t group_index = parent_group;
    int has_local_t;
    int index;
    int count;
    extractpdf_status status;

    has_local_t = extractpdf_pdf_dict_find(scan->ctx, object, PDF_NAME(T), &t);
    if (top_level || has_local_t) {
        status = extractpdf_pdf_edit_form_scan_reserve(
            scan, scan->group_count + 1);
        if (status != EXTRACTPDF_OK)
            return status;
        group_index = scan->group_count;
        scan->groups[group_index].head = object;
        scan->groups[group_index].parent_group = top_level ? SIZE_MAX : parent_group;
        scan->groups[group_index].has_named_child = 0;
        ++scan->group_count;
        if (!top_level && has_local_t && parent_group != SIZE_MAX)
            scan->groups[parent_group].has_named_child = 1;
    }

    if (extractpdf_pdf_edit_form_scan_is_widget(scan, object))
        return EXTRACTPDF_OK;
    if (!extractpdf_pdf_dict_find(scan->ctx, object, PDF_NAME(Kids), &kids))
        return EXTRACTPDF_OK;

    count = pdf_array_len(scan->ctx, kids);
    for (index = 0; index < count; ++index) {
        status = extractpdf_pdf_edit_form_scan_node(
            scan, pdf_array_get(scan->ctx, kids, index), group_index, 0);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_form_field_head(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    pdf_obj **out_head)
{
    extractpdf_pdf_edit_form_scan scan;
    pdf_obj *fields;
    extractpdf_status status = EXTRACTPDF_OK;
    int index;
    int count;
    size_t group_index;
    size_t public_index = 0;

    *out_head = NULL;
    memset(&scan, 0, sizeof(scan));
    scan.ctx = edit->ctx;
    fields = pdf_dict_getp(
        edit->ctx, pdf_trailer(edit->ctx, edit->document),
        "Root/AcroForm/Fields");
    if (!pdf_is_array(edit->ctx, fields))
        return model->field_count == 0 ? EXTRACTPDF_ERROR_ARGUMENT :
            EXTRACTPDF_ERROR_FORMAT;

    count = pdf_array_len(edit->ctx, fields);
    for (index = 0; index < count; ++index) {
        status = extractpdf_pdf_edit_form_scan_node(
            &scan, pdf_array_get(edit->ctx, fields, index), SIZE_MAX, 1);
        if (status != EXTRACTPDF_OK)
            break;
    }
    if (status == EXTRACTPDF_OK) {
        for (group_index = 0; group_index < scan.group_count; ++group_index) {
            if (scan.groups[group_index].has_named_child)
                continue;
            if (public_index == field_index) {
                *out_head = scan.groups[group_index].head;
                break;
            }
            ++public_index;
        }
        if (public_index >= model->field_count || *out_head == NULL)
            status = EXTRACTPDF_ERROR_FORMAT;
    }
    free(scan.groups);
    return status;
}

static extractpdf_status extractpdf_pdf_edit_form_reserve_entries(
    extractpdf_pdf_edit *edit,
    size_t needed)
{
    extractpdf_pdf_edit_form_entry *grown;
    size_t capacity;
    size_t maximum = (size_t)UINT32_MAX - 1;

    if (needed <= edit->form_entry_capacity)
        return EXTRACTPDF_OK;
    if (needed > maximum || needed > SIZE_MAX / sizeof(*edit->form_entries))
        return EXTRACTPDF_ERROR_NOMEM;
    capacity = edit->form_entry_capacity != 0 ? edit->form_entry_capacity : 8;
    while (capacity < needed) {
        size_t next = capacity > maximum / 2 ? maximum : capacity * 2;
        if (next <= capacity) {
            capacity = maximum;
            break;
        }
        capacity = next;
    }
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*edit->form_entries))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_edit_form_entry *)realloc(
        edit->form_entries, capacity * sizeof(*edit->form_entries));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    memset(grown + edit->form_entry_capacity, 0,
        (capacity - edit->form_entry_capacity) * sizeof(*grown));
    edit->form_entries = grown;
    edit->form_entry_capacity = capacity;
    return EXTRACTPDF_OK;
}

static uint32_t extractpdf_pdf_edit_form_tag_for_slot(
    extractpdf_pdf_edit *edit,
    size_t slot)
{
    uint64_t mixed = extractpdf_pdf_edit_form_mix64(
        edit->session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN ^
        (uint64_t)(slot + 1));
    uint32_t tag = (uint32_t)(mixed ^ (mixed >> 32));

    return tag != 0 ? tag : 1;
}

static void extractpdf_pdf_edit_form_make_token(
    extractpdf_pdf_edit *edit,
    size_t slot,
    extractpdf_form_field_ref *out_ref)
{
    const extractpdf_pdf_edit_form_entry *entry = &edit->form_entries[slot];

    out_ref->opaque[0] = edit->session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN;
    out_ref->opaque[1] =
        ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
}

static extractpdf_status extractpdf_pdf_edit_form_register(
    extractpdf_pdf_edit *edit,
    pdf_obj *group_head,
    extractpdf_form_field_ref *out_ref)
{
    size_t slot;
    extractpdf_status status;

    for (slot = 0; slot < edit->form_entry_count; ++slot) {
        if (extractpdf_pdf_edit_form_same_identity(
                edit->ctx, edit->form_entries[slot].group_head, group_head)) {
            extractpdf_pdf_edit_form_make_token(edit, slot, out_ref);
            return EXTRACTPDF_OK;
        }
    }
    if (edit->form_entry_count >= (size_t)UINT32_MAX - 1)
        return EXTRACTPDF_ERROR_NOMEM;
    status = extractpdf_pdf_edit_form_reserve_entries(
        edit, edit->form_entry_count + 1);
    if (status != EXTRACTPDF_OK)
        return status;

    slot = edit->form_entry_count;
    edit->form_entries[slot].group_head = pdf_keep_obj(edit->ctx, group_head);
    if (edit->form_entries[slot].group_head == NULL)
        return EXTRACTPDF_ERROR_MUPDF;
    edit->form_entries[slot].tag = extractpdf_pdf_edit_form_tag_for_slot(edit, slot);
    ++edit->form_entry_count;
    extractpdf_pdf_edit_form_make_token(edit, slot, out_ref);
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_form_resolve_ref(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    extractpdf_pdf_edit_form_entry **out_entry)
{
    uint64_t encoded;
    uint32_t slot_plus_one;
    uint32_t tag;
    size_t slot;
    extractpdf_pdf_edit_form_entry *entry;

    if (out_entry != NULL)
        *out_entry = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL || ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (ref->opaque[0] !=
        (edit->session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN))
        return EXTRACTPDF_ERROR_ARGUMENT;
    encoded = ref->opaque[1];
    slot_plus_one = (uint32_t)(encoded & UINT32_MAX);
    tag = (uint32_t)(encoded >> 32);
    if (slot_plus_one == 0 || tag == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;
    slot = (size_t)slot_plus_one - 1;
    if (slot >= edit->form_entry_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    entry = &edit->form_entries[slot];
    if (entry->tag != tag || entry->group_head == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (out_entry != NULL)
        *out_entry = entry;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_form_field_ref_at(
    extractpdf_pdf_edit *edit,
    size_t field_index,
    extractpdf_form_field_ref *out_ref)
{
    extractpdf_pdf_form_model *model = NULL;
    pdf_obj *group_head = NULL;
    extractpdf_status status;
    int caught_code = FZ_ERROR_NONE;

    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (out_ref == NULL || edit == NULL || edit->ctx == NULL ||
        edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_form_build_model(edit, &model);
    if (status != EXTRACTPDF_OK)
        return status;
    if (field_index >= model->field_count) {
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_ARGUMENT;
    }

    fz_var(group_head);
    fz_var(status);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        status = extractpdf_pdf_edit_form_field_head(
            edit, model, field_index, &group_head);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_edit_form_register(edit, group_head, out_ref);
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }
    extractpdf_pdf_form_drop_model(model);
    if (caught_code != FZ_ERROR_NONE) {
        memset(out_ref, 0, sizeof(*out_ref));
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK)
        memset(out_ref, 0, sizeof(*out_ref));
    return status;
}

extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update)
{
    extractpdf_pdf_edit_form_entry *entry = NULL;
    extractpdf_status status;

    if (edit == NULL || ref == NULL || update == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    status = extractpdf_pdf_edit_form_resolve_ref(edit, ref, &entry);
    if (status != EXTRACTPDF_OK)
        return status;
    (void)entry;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
