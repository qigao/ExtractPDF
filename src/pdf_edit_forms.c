#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXTRACTPDF_FORM_REF_DOMAIN UINT64_C(0x464f524d5f524546)

struct extractpdf_form {
    extractpdf_pdf_form_model *model;
};

static uint64_t extractpdf_pdf_edit_form_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
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

    status = extractpdf_pdf_form_build(
        edit->ctx, edit->document, 0, &model, NULL);
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
        if (extractpdf_pdf_form_same_identity(
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
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status;

    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (out_ref == NULL || edit == NULL || edit->ctx == NULL ||
        edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_form_build(
        edit->ctx, edit->document, 1, &model, &provenance);
    if (status != EXTRACTPDF_OK)
        return status;
    if (field_index >= model->field_count) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_ARGUMENT;
    }
    if (provenance == NULL || provenance->field_count != model->field_count ||
        provenance->fields[field_index].group_head == NULL) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_FORMAT;
    }

    status = extractpdf_pdf_edit_form_register(
        edit, provenance->fields[field_index].group_head, out_ref);
    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    if (status != EXTRACTPDF_OK)
        memset(out_ref, 0, sizeof(*out_ref));
    return status;
}

static extractpdf_status extractpdf_pdf_edit_form_find_current_field(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_entry *entry,
    extractpdf_pdf_form_model **out_model,
    extractpdf_pdf_form_provenance **out_provenance,
    size_t *out_field_index)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status;
    size_t i;
    size_t match = SIZE_MAX;

    *out_model = NULL;
    *out_provenance = NULL;
    *out_field_index = SIZE_MAX;
    status = extractpdf_pdf_form_build(
        edit->ctx, edit->document, 1, &model, &provenance);
    if (status != EXTRACTPDF_OK)
        return status;
    if (provenance == NULL || provenance->field_count != model->field_count) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_FORMAT;
    }
    status = extractpdf_pdf_form_capture_provenance_widgets(
        edit->ctx, edit->document, model, provenance);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return status;
    }
    for (i = 0; i < provenance->field_count; ++i) {
        if (!extractpdf_pdf_form_same_identity(
                edit->ctx, entry->group_head, provenance->fields[i].group_head))
            continue;
        if (match != SIZE_MAX) {
            extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
            extractpdf_pdf_form_drop_model(model);
            return EXTRACTPDF_ERROR_STATE;
        }
        match = i;
    }
    if (match == SIZE_MAX) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_STATE;
    }
    *out_model = model;
    *out_provenance = provenance;
    *out_field_index = match;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_form_task4_text_shape_valid(
    const extractpdf_form_value_update *update)
{
    const extractpdf_form_value_input *value;
    size_t update_min = offsetof(extractpdf_form_value_update, value_count) +
        sizeof(update->value_count);
    size_t value_min;

    if (update->struct_size < update_min ||
        update->presence != EXTRACTPDF_FORM_VALUE_PRESENT ||
        update->value_count != 1 || update->values == NULL)
        return 0;
    value = &update->values[0];
    value_min = offsetof(extractpdf_form_value_input, utf8_size) +
        sizeof(value->utf8_size);
    return value->struct_size >= value_min &&
        value->kind == EXTRACTPDF_FORM_VALUE_UTF8 &&
        value->option_index == SIZE_MAX && value->utf8 != NULL;
}

extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update)
{
    extractpdf_pdf_edit_form_entry *entry = NULL;
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_pdf_edit_form_widget_handles handles;
    extractpdf_status status;
    size_t field_index = SIZE_MAX;

    memset(&handles, 0, sizeof(handles));
    if (edit == NULL || ref == NULL || update == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    status = extractpdf_pdf_edit_form_resolve_ref(edit, ref, &entry);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_edit_form_find_current_field(
        edit, entry, &model, &provenance, &field_index);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_pdf_edit_form_mutation_preflight(edit);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return status;
    }

    if (model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_CHECKBOX ||
        model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_RADIO_BUTTON) {
        status = extractpdf_pdf_edit_form_apply_button(
            edit, model, field_index, &provenance->fields[field_index], update);
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return status;
    }

    if (model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_COMBO_BOX ||
        model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_LIST_BOX) {
        status = extractpdf_pdf_edit_form_apply_choice(
            edit, model, field_index, &provenance->fields[field_index], update);
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return status;
    }

    if (model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_TEXT &&
        provenance->fields[field_index].widget_count == 0) {
        status = extractpdf_pdf_edit_form_apply_zero_widget_text(
            edit, model, field_index, &provenance->fields[field_index], update);
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return status;
    }

    if (model->fields[field_index].type == EXTRACTPDF_FORM_FIELD_TEXT &&
        provenance->fields[field_index].widget_count != 0 &&
        extractpdf_pdf_edit_form_task4_text_shape_valid(update)) {
        status = extractpdf_pdf_edit_form_prepare_widget_handles(
            edit, &provenance->fields[field_index], &handles);
        if (status != EXTRACTPDF_OK) {
            extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
            extractpdf_pdf_form_drop_model(model);
            return status;
        }
#if defined(EXTRACTPDF_TESTING)
        if (edit->test_fault ==
            EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE) {
            edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
            extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
            extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
            extractpdf_pdf_form_drop_model(model);
            return EXTRACTPDF_ERROR_MUPDF;
        }
#endif
        extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
    }

    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
