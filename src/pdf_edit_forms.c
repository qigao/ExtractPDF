#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUANTAPDF_FORM_REF_DOMAIN UINT64_C(0x464f524d5f524546)

struct quantapdf_form {
    quantapdf_pdf_form_model *model;
};

static uint64_t quantapdf_pdf_edit_form_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

quantapdf_status quantapdf_pdf_edit_form_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_form **out_form)
{
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_form *form;
    quantapdf_status status;

    if (out_form == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_form = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_form_build(
        edit->ctx, edit->document, 0, &model, NULL);
    if (status != QUANTAPDF_OK)
        return status;
    form = (quantapdf_form *)calloc(1, sizeof(*form));
    if (form == NULL) {
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_NOMEM;
    }
    form->model = model;
    *out_form = form;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_form_reserve_entries(
    quantapdf_pdf_edit *edit,
    size_t needed)
{
    quantapdf_pdf_edit_form_entry *grown;
    size_t capacity;
    size_t maximum = (size_t)UINT32_MAX - 1;

    if (needed <= edit->form_entry_capacity)
        return QUANTAPDF_OK;
    if (needed > maximum || needed > SIZE_MAX / sizeof(*edit->form_entries))
        return QUANTAPDF_ERROR_NOMEM;
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
        return QUANTAPDF_ERROR_NOMEM;
    grown = (quantapdf_pdf_edit_form_entry *)realloc(
        edit->form_entries, capacity * sizeof(*edit->form_entries));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memset(grown + edit->form_entry_capacity, 0,
        (capacity - edit->form_entry_capacity) * sizeof(*grown));
    edit->form_entries = grown;
    edit->form_entry_capacity = capacity;
    return QUANTAPDF_OK;
}

static uint32_t quantapdf_pdf_edit_form_tag_for_slot(
    quantapdf_pdf_edit *edit,
    size_t slot)
{
    uint64_t mixed = quantapdf_pdf_edit_form_mix64(
        edit->session_cookie ^ QUANTAPDF_FORM_REF_DOMAIN ^
        (uint64_t)(slot + 1));
    uint32_t tag = (uint32_t)(mixed ^ (mixed >> 32));

    return tag != 0 ? tag : 1;
}

static void quantapdf_pdf_edit_form_make_token(
    quantapdf_pdf_edit *edit,
    size_t slot,
    quantapdf_form_field_ref *out_ref)
{
    const quantapdf_pdf_edit_form_entry *entry = &edit->form_entries[slot];

    out_ref->opaque[0] = edit->session_cookie ^ QUANTAPDF_FORM_REF_DOMAIN;
    out_ref->opaque[1] =
        ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
}

static int quantapdf_pdf_edit_form_locator_equal(
    const quantapdf_pdf_edit_form_entry *entry,
    const quantapdf_pdf_form_locator *locator)
{
    if (entry == NULL || locator == NULL)
        return 0;
    if (entry->locator_step_count != locator->step_count)
        return 0;
    if (entry->locator_step_count == 0)
        return 1;
    if (entry->locator_steps == NULL || locator->steps == NULL)
        return 0;
    return memcmp(
        entry->locator_steps,
        locator->steps,
        entry->locator_step_count * sizeof(*entry->locator_steps)) == 0;
}

static quantapdf_status quantapdf_pdf_edit_form_register(
    quantapdf_pdf_edit *edit,
    const quantapdf_pdf_form_live_field *live,
    quantapdf_form_field_ref *out_ref)
{
    size_t *locator_copy;
    size_t slot;
    quantapdf_status status;

    if (live == NULL || live->locator.step_count == 0 ||
        live->locator.steps == NULL)
        return QUANTAPDF_ERROR_FORMAT;
    for (slot = 0; slot < edit->form_entry_count; ++slot) {
        if (quantapdf_pdf_edit_form_locator_equal(
                &edit->form_entries[slot], &live->locator)) {
            quantapdf_pdf_edit_form_make_token(edit, slot, out_ref);
            return QUANTAPDF_OK;
        }
    }
    if (edit->form_entry_count >= (size_t)UINT32_MAX - 1)
        return QUANTAPDF_ERROR_NOMEM;
    if (live->locator.step_count > SIZE_MAX / sizeof(*locator_copy))
        return QUANTAPDF_ERROR_NOMEM;
    locator_copy = (size_t *)malloc(
        live->locator.step_count * sizeof(*locator_copy));
    if (locator_copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(
        locator_copy,
        live->locator.steps,
        live->locator.step_count * sizeof(*locator_copy));

    status = quantapdf_pdf_edit_form_reserve_entries(
        edit, edit->form_entry_count + 1);
    if (status != QUANTAPDF_OK) {
        free(locator_copy);
        return status;
    }

    slot = edit->form_entry_count;
    edit->form_entries[slot].locator_steps = locator_copy;
    edit->form_entries[slot].locator_step_count = live->locator.step_count;
    edit->form_entries[slot].tag = quantapdf_pdf_edit_form_tag_for_slot(edit, slot);
    ++edit->form_entry_count;
    quantapdf_pdf_edit_form_make_token(edit, slot, out_ref);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_form_resolve_ref(
    quantapdf_pdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    quantapdf_pdf_edit_form_entry **out_entry)
{
    uint64_t encoded;
    uint32_t slot_plus_one;
    uint32_t tag;
    size_t slot;
    quantapdf_pdf_edit_form_entry *entry;

    if (out_entry != NULL)
        *out_entry = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL || ref == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (ref->opaque[0] !=
        (edit->session_cookie ^ QUANTAPDF_FORM_REF_DOMAIN))
        return QUANTAPDF_ERROR_ARGUMENT;
    encoded = ref->opaque[1];
    slot_plus_one = (uint32_t)(encoded & UINT32_MAX);
    tag = (uint32_t)(encoded >> 32);
    if (slot_plus_one == 0 || tag == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    slot = (size_t)slot_plus_one - 1;
    if (slot >= edit->form_entry_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    entry = &edit->form_entries[slot];
    if (entry->tag != tag || entry->locator_step_count == 0 ||
        entry->locator_steps == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (out_entry != NULL)
        *out_entry = entry;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_form_field_ref_at(
    quantapdf_pdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref)
{
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_pdf_form_provenance *provenance = NULL;
    quantapdf_status status;

    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (out_ref == NULL || edit == NULL || edit->ctx == NULL ||
        edit->document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_form_build(
        edit->ctx, edit->document, 1, &model, &provenance);
    if (status != QUANTAPDF_OK)
        return status;
    if (field_index >= model->field_count) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    if (provenance == NULL || provenance->field_count != model->field_count ||
        provenance->fields[field_index].group_head == NULL ||
        provenance->fields[field_index].locator.step_count == 0 ||
        provenance->fields[field_index].locator.steps == NULL) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_FORMAT;
    }

    status = quantapdf_pdf_edit_form_register(
        edit, &provenance->fields[field_index], out_ref);
    quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
    quantapdf_pdf_form_drop_model(model);
    if (status != QUANTAPDF_OK)
        memset(out_ref, 0, sizeof(*out_ref));
    return status;
}

static quantapdf_status quantapdf_pdf_edit_form_find_current_field(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_form_entry *entry,
    quantapdf_pdf_form_model **out_model,
    quantapdf_pdf_form_provenance **out_provenance,
    size_t *out_field_index)
{
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_pdf_form_provenance *provenance = NULL;
    quantapdf_status status;
    size_t i;
    size_t match = SIZE_MAX;

    *out_model = NULL;
    *out_provenance = NULL;
    *out_field_index = SIZE_MAX;
    status = quantapdf_pdf_form_build(
        edit->ctx, edit->document, 1, &model, &provenance);
    if (status != QUANTAPDF_OK)
        return status;
    if (provenance == NULL || provenance->field_count != model->field_count) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_FORMAT;
    }
    for (i = 0; i < provenance->field_count; ++i) {
        if (!quantapdf_pdf_edit_form_locator_equal(
                entry, &provenance->fields[i].locator))
            continue;
        if (match != SIZE_MAX) {
            quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
            quantapdf_pdf_form_drop_model(model);
            return QUANTAPDF_ERROR_STATE;
        }
        match = i;
    }
    if (match == SIZE_MAX) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_STATE;
    }
    status = quantapdf_pdf_form_capture_provenance_widgets(
        edit->ctx, edit->document, model, provenance);
    if (status != QUANTAPDF_OK) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return status;
    }
    *out_model = model;
    *out_provenance = provenance;
    *out_field_index = match;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_form_set_values(
    quantapdf_pdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update)
{
    quantapdf_pdf_edit_form_entry *entry = NULL;
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_pdf_form_provenance *provenance = NULL;
    quantapdf_status status;
    size_t field_index = SIZE_MAX;

    if (edit == NULL || ref == NULL || update == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_pdf_edit_form_resolve_ref(edit, ref, &entry);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_pdf_edit_form_find_current_field(
        edit, entry, &model, &provenance, &field_index);
    if (status != QUANTAPDF_OK)
        return status;

    status = quantapdf_pdf_edit_form_mutation_preflight(edit);
    if (status != QUANTAPDF_OK) {
        quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
        return status;
    }

    if (model->fields[field_index].type == QUANTAPDF_FORM_FIELD_CHECKBOX ||
        model->fields[field_index].type == QUANTAPDF_FORM_FIELD_RADIO_BUTTON) {
        status = quantapdf_pdf_edit_form_apply_button(
            edit, model, field_index, &provenance->fields[field_index], update);
    } else if (model->fields[field_index].type == QUANTAPDF_FORM_FIELD_COMBO_BOX ||
        model->fields[field_index].type == QUANTAPDF_FORM_FIELD_LIST_BOX) {
        status = quantapdf_pdf_edit_form_apply_choice(
            edit, model, field_index, &provenance->fields[field_index], update);
    } else if (model->fields[field_index].type == QUANTAPDF_FORM_FIELD_TEXT) {
        status = quantapdf_pdf_edit_form_apply_text(
            edit, model, field_index, &provenance->fields[field_index], update);
    } else {
        status = QUANTAPDF_ERROR_UNSUPPORTED;
    }

    quantapdf_pdf_form_drop_provenance(edit->ctx, provenance);
    quantapdf_pdf_form_drop_model(model);
    return status;
}
