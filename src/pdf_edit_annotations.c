#include "pdf_edit_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void extractpdf_pdf_edit_zero_ref(extractpdf_annotation_ref *ref)
{
    memset(ref, 0, sizeof(*ref));
}

static void extractpdf_pdf_edit_zero_rect(extractpdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static uint64_t extractpdf_pdf_edit_mix_token(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static extractpdf_status extractpdf_pdf_edit_validate_page(
    extractpdf_pdf_edit *edit,
    int page_index)
{
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (page_index < 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(page_count);
    fz_var(caught_code);

    fz_try(edit->ctx)
    {
        page_count = pdf_count_pages(edit->ctx, edit->document);
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (page_index >= page_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_same_identity(
    fz_context *ctx,
    pdf_obj *a,
    pdf_obj *b)
{
    int ai = pdf_is_indirect(ctx, a);
    int bi = pdf_is_indirect(ctx, b);

    if (ai || bi) {
        return ai && bi &&
            pdf_to_num(ctx, a) == pdf_to_num(ctx, b) &&
            pdf_to_gen(ctx, a) == pdf_to_gen(ctx, b);
    }
    return a == b;
}

static extractpdf_status extractpdf_pdf_edit_reserve_entries(
    extractpdf_pdf_edit *edit,
    size_t needed)
{
    extractpdf_pdf_edit_annotation_entry *grown;
    size_t capacity;
    size_t maximum = (size_t)UINT32_MAX - 1;

    if (needed <= edit->entry_capacity)
        return EXTRACTPDF_OK;
    if (needed > maximum ||
        needed > SIZE_MAX / sizeof(*edit->entries))
        return EXTRACTPDF_ERROR_NOMEM;

    capacity = edit->entry_capacity != 0 ? edit->entry_capacity : 8;
    while (capacity < needed) {
        size_t next;

        if (capacity >= maximum) {
            capacity = maximum;
            break;
        }
        if (capacity > maximum / 2)
            next = maximum;
        else
            next = capacity * 2;
        if (next <= capacity) {
            capacity = maximum;
            break;
        }
        capacity = next;
    }
    if (capacity < needed ||
        capacity > SIZE_MAX / sizeof(*edit->entries))
        return EXTRACTPDF_ERROR_NOMEM;

    grown = (extractpdf_pdf_edit_annotation_entry *)realloc(
        edit->entries, capacity * sizeof(*edit->entries));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    memset(
        grown + edit->entry_capacity,
        0,
        (capacity - edit->entry_capacity) * sizeof(*grown));
    edit->entries = grown;
    edit->entry_capacity = capacity;
    return EXTRACTPDF_OK;
}

static uint32_t extractpdf_pdf_edit_tag_for_slot(
    extractpdf_pdf_edit *edit,
    size_t slot)
{
    uint64_t mixed = extractpdf_pdf_edit_mix_token(
        edit->session_cookie ^ (uint64_t)(slot + 1));
    uint32_t tag = (uint32_t)(mixed ^ (mixed >> 32));

    if (tag == 0)
        tag = 1;
    return tag;
}

static void extractpdf_pdf_edit_make_token(
    extractpdf_pdf_edit *edit,
    size_t slot,
    extractpdf_annotation_ref *out_ref)
{
    const extractpdf_pdf_edit_annotation_entry *entry = &edit->entries[slot];

    out_ref->opaque[0] = edit->session_cookie;
    out_ref->opaque[1] =
        ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
}

static extractpdf_status extractpdf_pdf_edit_resolve_ref(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    extractpdf_pdf_edit_annotation_entry **out_entry)
{
    uint64_t encoded;
    uint32_t slot_plus_one;
    uint32_t tag;
    size_t slot;
    extractpdf_pdf_edit_annotation_entry *entry;

    if (out_entry != NULL)
        *out_entry = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL ||
        ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (ref->opaque[0] != edit->session_cookie)
        return EXTRACTPDF_ERROR_ARGUMENT;

    encoded = ref->opaque[1];
    slot_plus_one = (uint32_t)(encoded & UINT32_MAX);
    tag = (uint32_t)(encoded >> 32);
    if (slot_plus_one == 0 || tag == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    slot = (size_t)slot_plus_one - 1;
    if (slot >= edit->entry_count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    entry = &edit->entries[slot];
    if (entry->tag != tag)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (!entry->live || entry->object == NULL)
        return EXTRACTPDF_ERROR_STATE;

    if (out_entry != NULL)
        *out_entry = entry;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_register_object(
    extractpdf_pdf_edit *edit,
    pdf_obj *object,
    int page_index,
    extractpdf_annotation_ref *out_ref)
{
    extractpdf_status status;
    size_t slot;

    for (slot = 0; slot < edit->entry_count; ++slot) {
        extractpdf_pdf_edit_annotation_entry *entry = &edit->entries[slot];

        if (!entry->live || entry->object == NULL)
            continue;
        if (extractpdf_pdf_edit_same_identity(
                edit->ctx, entry->object, object)) {
            pdf_drop_obj(edit->ctx, object);
            extractpdf_pdf_edit_make_token(edit, slot, out_ref);
            return EXTRACTPDF_OK;
        }
    }

    if (edit->entry_count >= (size_t)UINT32_MAX - 1) {
        pdf_drop_obj(edit->ctx, object);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    status = extractpdf_pdf_edit_reserve_entries(
        edit, edit->entry_count + 1);
    if (status != EXTRACTPDF_OK) {
        pdf_drop_obj(edit->ctx, object);
        return status;
    }

    slot = edit->entry_count;
    edit->entries[slot].object = object;
    edit->entries[slot].page_index = page_index;
    edit->entries[slot].tag = extractpdf_pdf_edit_tag_for_slot(edit, slot);
    edit->entries[slot].live = 1;
    ++edit->entry_count;

    extractpdf_pdf_edit_make_token(edit, slot, out_ref);
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_scan_page(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t wanted_index,
    int want_object,
    size_t *out_count,
    pdf_obj **out_object)
{
    pdf_page *page = NULL;
    pdf_obj *kept = NULL;
    size_t count = 0;
    extractpdf_status status;
    int caught_code = FZ_ERROR_NONE;

    if (out_count != NULL)
        *out_count = 0;
    if (out_object != NULL)
        *out_object = NULL;

    status = extractpdf_pdf_edit_validate_page(edit, page_index);
    if (status != EXTRACTPDF_OK)
        return status;

    fz_var(page);
    fz_var(kept);
    fz_var(count);
    fz_var(status);
    fz_var(caught_code);

    status = EXTRACTPDF_OK;
    fz_try(edit->ctx)
    {
        pdf_obj *annots;
        pdf_obj *wanted = NULL;
        fz_matrix page_ctm;
        int raw_count;
        int index;

        page = pdf_load_page(edit->ctx, edit->document, page_index);
        pdf_page_transform(edit->ctx, page, NULL, &page_ctm);
        annots = pdf_dict_get(edit->ctx, page->obj, PDF_NAME(Annots));
        raw_count = pdf_array_len(edit->ctx, annots);

        for (index = 0; index < raw_count; ++index) {
            pdf_obj *annotation = pdf_array_get(edit->ctx, annots, index);
            extractpdf_annotation_type type;
            extractpdf_pdf_annotation_view view;

            if (!pdf_is_dict(edit->ctx, annotation))
                continue;
            if (!extractpdf_pdf_annotation_classify(
                    edit->ctx, annotation, &type))
                continue;

            status = extractpdf_pdf_annotation_read_view(
                edit->ctx, annotation, type, page_ctm, &view);
            if (status != EXTRACTPDF_OK)
                break;

            if (want_object && count == wanted_index)
                wanted = annotation;
            if (count == SIZE_MAX) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }
            ++count;
        }

        if (status == EXTRACTPDF_OK && want_object) {
            if (wanted_index >= count || wanted == NULL)
                status = EXTRACTPDF_ERROR_ARGUMENT;
            else
                kept = pdf_keep_obj(edit->ctx, wanted);
        }
    }
    fz_always(edit->ctx)
    {
        if (page != NULL)
            fz_drop_page(edit->ctx, &page->super);
        page = NULL;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        pdf_drop_obj(edit->ctx, kept);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK) {
        pdf_drop_obj(edit->ctx, kept);
        return status;
    }

    if (out_count != NULL)
        *out_count = count;
    if (out_object != NULL)
        *out_object = kept;
    else
        pdf_drop_obj(edit->ctx, kept);
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_resolve_live_annot(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_annotation_entry *entry,
    pdf_page **out_page,
    pdf_annot **out_annot)
{
    pdf_page *page = NULL;
    pdf_annot *found = NULL;
    int caught_code = FZ_ERROR_NONE;

    *out_page = NULL;
    *out_annot = NULL;

    fz_var(page);
    fz_var(found);
    fz_var(caught_code);

    fz_try(edit->ctx)
    {
        pdf_annot *annotation;

        page = pdf_load_page(edit->ctx, edit->document, entry->page_index);
        for (annotation = pdf_first_annot(edit->ctx, page);
             annotation != NULL;
             annotation = pdf_next_annot(edit->ctx, annotation)) {
            if (extractpdf_pdf_edit_same_identity(
                    edit->ctx,
                    pdf_annot_obj(edit->ctx, annotation),
                    entry->object)) {
                found = annotation;
                break;
            }
        }
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        if (page != NULL)
            fz_drop_page(edit->ctx, &page->super);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (found == NULL) {
        if (page != NULL)
            fz_drop_page(edit->ctx, &page->super);
        return EXTRACTPDF_ERROR_STATE;
    }

    *out_page = page;
    *out_annot = found;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_resolve_live_view(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    extractpdf_pdf_edit_annotation_entry **out_entry,
    pdf_page **out_page,
    pdf_annot **out_annot,
    extractpdf_pdf_annotation_view *out_view)
{
    extractpdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    extractpdf_status status;
    int caught_code = FZ_ERROR_NONE;

    if (out_entry != NULL)
        *out_entry = NULL;
    *out_page = NULL;
    *out_annot = NULL;
    memset(out_view, 0, sizeof(*out_view));

    status = extractpdf_pdf_edit_resolve_ref(edit, ref, &entry);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_pdf_edit_resolve_live_annot(
        edit, entry, &page, &annotation);
    if (status != EXTRACTPDF_OK)
        return status;

    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        extractpdf_annotation_type type;
        fz_matrix page_ctm;
        pdf_obj *object = pdf_annot_obj(edit->ctx, annotation);

        if (!extractpdf_pdf_annotation_classify(
                edit->ctx, object, &type)) {
            status = EXTRACTPDF_ERROR_STATE;
        } else {
            pdf_page_transform(edit->ctx, page, NULL, &page_ctm);
            status = extractpdf_pdf_annotation_read_view(
                edit->ctx, object, type, page_ctm, out_view);
        }
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        fz_drop_page(edit->ctx, &page->super);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK) {
        fz_drop_page(edit->ctx, &page->super);
        return status;
    }

    if (out_entry != NULL)
        *out_entry = entry;
    *out_page = page;
    *out_annot = annotation;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_annotation_count(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t *out_count)
{
    size_t count = 0;
    extractpdf_status status;

    if (out_count != NULL)
        *out_count = 0;
    if (out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_scan_page(
        edit, page_index, 0, 0, &count, NULL);
    if (status != EXTRACTPDF_OK)
        return status;

    *out_count = count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_annotation_ref_at(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t index,
    extractpdf_annotation_ref *out_ref)
{
    pdf_obj *object = NULL;
    size_t count = 0;
    extractpdf_status status;

    if (out_ref != NULL)
        extractpdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_scan_page(
        edit, page_index, index, 1, &count, &object);
    if (status != EXTRACTPDF_OK)
        return status;

    return extractpdf_pdf_edit_register_object(
        edit, object, page_index, out_ref);
}

extractpdf_status extractpdf_pdf_edit_annotation_get_info(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    extractpdf_annotation_info *out_info)
{
    extractpdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    extractpdf_pdf_annotation_view view;
    extractpdf_status status;
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->type = EXTRACTPDF_ANNOTATION_UNKNOWN;
    extractpdf_pdf_edit_zero_rect(&out_info->bounds);
    out_info->flags = 0;

    status = extractpdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != EXTRACTPDF_OK)
        return status;

    (void)entry;
    (void)annotation;
    out_info->type = view.type;
    out_info->bounds = view.bounds;
    out_info->flags = view.flags;
    fz_drop_page(edit->ctx, &page->super);
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_annotation_contents(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    extractpdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    extractpdf_pdf_annotation_view view;
    extractpdf_status status;
    char *copy;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (out_utf8 == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != EXTRACTPDF_OK)
        return status;

    (void)entry;
    (void)annotation;
    if (!view.has_contents) {
        fz_drop_page(edit->ctx, &page->super);
        return EXTRACTPDF_OK;
    }
    if (view.contents_size == SIZE_MAX) {
        fz_drop_page(edit->ctx, &page->super);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    copy = (char *)malloc(view.contents_size + 1);
    if (copy == NULL) {
        fz_drop_page(edit->ctx, &page->super);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    memcpy(copy, view.contents_utf8, view.contents_size);
    copy[view.contents_size] = '\0';
    fz_drop_page(edit->ctx, &page->super);

    *out_utf8 = copy;
    *out_size = view.contents_size;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_annotation_create(
    extractpdf_pdf_edit *edit,
    int page_index,
    const extractpdf_annotation_create_options *options,
    extractpdf_annotation_ref *out_ref)
{
    size_t minimum_size;
    extractpdf_status status;

    if (out_ref != NULL)
        extractpdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL || options == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_create_options, contents_size) +
        sizeof(options->contents_size);
    if (options->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_validate_page(edit, page_index);
    if (status != EXTRACTPDF_OK)
        return status;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_update(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    const extractpdf_annotation_update *update)
{
    const uint32_t known_fields =
        EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS |
        EXTRACTPDF_ANNOTATION_UPDATE_FLAGS |
        EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
    extractpdf_pdf_edit_annotation_entry *entry = NULL;
    extractpdf_status status;
    size_t minimum_size;

    if (update == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_update, contents_size) +
        sizeof(update->contents_size);
    if (update->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if ((update->fields & ~known_fields) != 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_resolve_ref(edit, ref, &entry);
    if (status != EXTRACTPDF_OK)
        return status;
    (void)entry;

    if (update->fields == 0)
        return EXTRACTPDF_OK;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_delete(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref)
{
    extractpdf_pdf_edit_annotation_entry *entry = NULL;
    extractpdf_status status = extractpdf_pdf_edit_resolve_ref(
        edit, ref, &entry);

    (void)entry;
    if (status != EXTRACTPDF_OK)
        return status;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
