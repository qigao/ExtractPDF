#include "pdf_edit_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void quantapdf_pdf_edit_zero_ref(quantapdf_annotation_ref *ref)
{
    memset(ref, 0, sizeof(*ref));
}

static void quantapdf_pdf_edit_zero_rect(quantapdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static int quantapdf_pdf_edit_bounds_valid(quantapdf_rect bounds)
{
    return isfinite(bounds.x0) && isfinite(bounds.y0) &&
        isfinite(bounds.x1) && isfinite(bounds.y1) &&
        bounds.x0 <= bounds.x1 && bounds.y0 <= bounds.y1;
}

static int quantapdf_pdf_edit_utf8_valid(
    const unsigned char *text,
    size_t size)
{
    size_t i = 0;

    while (i < size) {
        unsigned char a = text[i];

        if (a == 0)
            return 0;
        if (a <= 0x7f) {
            ++i;
            continue;
        }
        if (a >= 0xc2 && a <= 0xdf) {
            if (i + 1 >= size || text[i + 1] < 0x80 || text[i + 1] > 0xbf)
                return 0;
            i += 2;
            continue;
        }
        if (a == 0xe0) {
            if (i + 2 >= size || text[i + 1] < 0xa0 || text[i + 1] > 0xbf ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf)
                return 0;
            i += 3;
            continue;
        }
        if ((a >= 0xe1 && a <= 0xec) || (a >= 0xee && a <= 0xef)) {
            if (i + 2 >= size || text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf)
                return 0;
            i += 3;
            continue;
        }
        if (a == 0xed) {
            if (i + 2 >= size || text[i + 1] < 0x80 || text[i + 1] > 0x9f ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf)
                return 0;
            i += 3;
            continue;
        }
        if (a == 0xf0) {
            if (i + 3 >= size || text[i + 1] < 0x90 || text[i + 1] > 0xbf ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf ||
                text[i + 3] < 0x80 || text[i + 3] > 0xbf)
                return 0;
            i += 4;
            continue;
        }
        if (a >= 0xf1 && a <= 0xf3) {
            if (i + 3 >= size || text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf ||
                text[i + 3] < 0x80 || text[i + 3] > 0xbf)
                return 0;
            i += 4;
            continue;
        }
        if (a == 0xf4) {
            if (i + 3 >= size || text[i + 1] < 0x80 || text[i + 1] > 0x8f ||
                text[i + 2] < 0x80 || text[i + 2] > 0xbf ||
                text[i + 3] < 0x80 || text[i + 3] > 0xbf)
                return 0;
            i += 4;
            continue;
        }
        return 0;
    }
    return 1;
}

static quantapdf_status quantapdf_pdf_edit_prepare_contents(
    const char *contents_utf8,
    size_t contents_size,
    int *out_present,
    char **out_copy)
{
    char *copy;

    *out_present = 0;
    *out_copy = NULL;

    if (contents_utf8 == NULL) {
        if (contents_size != 0)
            return QUANTAPDF_ERROR_ARGUMENT;
        return QUANTAPDF_OK;
    }

    *out_present = 1;
    if (!quantapdf_pdf_edit_utf8_valid(
            (const unsigned char *)contents_utf8, contents_size))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (contents_size == SIZE_MAX)
        return QUANTAPDF_ERROR_NOMEM;

    copy = (char *)malloc(contents_size + 1);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    if (contents_size != 0)
        memcpy(copy, contents_utf8, contents_size);
    copy[contents_size] = '\0';
    *out_copy = copy;
    return QUANTAPDF_OK;
}

static uint64_t quantapdf_pdf_edit_mix_token(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static quantapdf_status quantapdf_pdf_edit_validate_page(
    quantapdf_pdf_edit *edit,
    int page_index)
{
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

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
        return quantapdf_status_from_mupdf(caught_code);
    if (page_index >= page_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_OK;
}

static int quantapdf_pdf_edit_same_identity(
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

static quantapdf_status quantapdf_pdf_edit_reserve_entries(
    quantapdf_pdf_edit *edit,
    size_t needed)
{
    quantapdf_pdf_edit_annotation_entry *grown;
    size_t capacity;
    size_t maximum = (size_t)UINT32_MAX - 1;

    if (needed <= edit->entry_capacity)
        return QUANTAPDF_OK;
    if (needed > maximum || needed > SIZE_MAX / sizeof(*edit->entries))
        return QUANTAPDF_ERROR_NOMEM;

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
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*edit->entries))
        return QUANTAPDF_ERROR_NOMEM;

    grown = (quantapdf_pdf_edit_annotation_entry *)realloc(
        edit->entries, capacity * sizeof(*edit->entries));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    memset(
        grown + edit->entry_capacity,
        0,
        (capacity - edit->entry_capacity) * sizeof(*grown));
    edit->entries = grown;
    edit->entry_capacity = capacity;
    return QUANTAPDF_OK;
}

static uint32_t quantapdf_pdf_edit_tag_for_slot(
    quantapdf_pdf_edit *edit,
    size_t slot)
{
    uint64_t mixed = quantapdf_pdf_edit_mix_token(
        edit->session_cookie ^ (uint64_t)(slot + 1));
    uint32_t tag = (uint32_t)(mixed ^ (mixed >> 32));

    if (tag == 0)
        tag = 1;
    return tag;
}

static void quantapdf_pdf_edit_make_token(
    quantapdf_pdf_edit *edit,
    size_t slot,
    quantapdf_annotation_ref *out_ref)
{
    const quantapdf_pdf_edit_annotation_entry *entry = &edit->entries[slot];

    out_ref->opaque[0] = edit->session_cookie;
    out_ref->opaque[1] =
        ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
}

static quantapdf_status quantapdf_pdf_edit_resolve_ref(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_pdf_edit_annotation_entry **out_entry)
{
    uint64_t encoded;
    uint32_t slot_plus_one;
    uint32_t tag;
    size_t slot;
    quantapdf_pdf_edit_annotation_entry *entry;

    if (out_entry != NULL)
        *out_entry = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL || ref == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (ref->opaque[0] != edit->session_cookie)
        return QUANTAPDF_ERROR_ARGUMENT;

    encoded = ref->opaque[1];
    slot_plus_one = (uint32_t)(encoded & UINT32_MAX);
    tag = (uint32_t)(encoded >> 32);
    if (slot_plus_one == 0 || tag == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    slot = (size_t)slot_plus_one - 1;
    if (slot >= edit->entry_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    entry = &edit->entries[slot];
    if (entry->tag != tag)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!entry->live || entry->object == NULL)
        return QUANTAPDF_ERROR_STATE;

    if (out_entry != NULL)
        *out_entry = entry;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_register_object(
    quantapdf_pdf_edit *edit,
    pdf_obj *object,
    int page_index,
    quantapdf_annotation_ref *out_ref)
{
    quantapdf_status status;
    size_t slot;

    for (slot = 0; slot < edit->entry_count; ++slot) {
        quantapdf_pdf_edit_annotation_entry *entry = &edit->entries[slot];

        if (!entry->live || entry->object == NULL)
            continue;
        if (quantapdf_pdf_edit_same_identity(edit->ctx, entry->object, object)) {
            pdf_drop_obj(edit->ctx, object);
            quantapdf_pdf_edit_make_token(edit, slot, out_ref);
            return QUANTAPDF_OK;
        }
    }

    if (edit->entry_count >= (size_t)UINT32_MAX - 1) {
        pdf_drop_obj(edit->ctx, object);
        return QUANTAPDF_ERROR_NOMEM;
    }
    status = quantapdf_pdf_edit_reserve_entries(edit, edit->entry_count + 1);
    if (status != QUANTAPDF_OK) {
        pdf_drop_obj(edit->ctx, object);
        return status;
    }

    slot = edit->entry_count;
    edit->entries[slot].object = object;
    edit->entries[slot].page_index = page_index;
    edit->entries[slot].tag = quantapdf_pdf_edit_tag_for_slot(edit, slot);
    edit->entries[slot].live = 1;
    ++edit->entry_count;
    quantapdf_pdf_edit_make_token(edit, slot, out_ref);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_scan_page(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t wanted_index,
    int want_object,
    size_t *out_count,
    pdf_obj **out_object)
{
    pdf_page *page = NULL;
    pdf_obj *kept = NULL;
    size_t count = 0;
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (out_count != NULL)
        *out_count = 0;
    if (out_object != NULL)
        *out_object = NULL;

    status = quantapdf_pdf_edit_validate_page(edit, page_index);
    if (status != QUANTAPDF_OK)
        return status;

    fz_var(page);
    fz_var(kept);
    fz_var(count);
    fz_var(status);
    fz_var(caught_code);
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
            quantapdf_annotation_type type;
            quantapdf_pdf_annotation_view view;

            if (!pdf_is_dict(edit->ctx, annotation))
                continue;
            if (!quantapdf_pdf_annotation_classify(edit->ctx, annotation, &type))
                continue;

            status = quantapdf_pdf_annotation_read_view(
                edit->ctx, annotation, type, page_ctm, &view);
            if (status != QUANTAPDF_OK)
                break;

            if (want_object && count == wanted_index)
                wanted = annotation;
            if (count == SIZE_MAX) {
                status = QUANTAPDF_ERROR_NOMEM;
                break;
            }
            ++count;
        }

        if (status == QUANTAPDF_OK && want_object) {
            if (wanted_index >= count || wanted == NULL)
                status = QUANTAPDF_ERROR_ARGUMENT;
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
        return quantapdf_status_from_mupdf(caught_code);
    }
    if (status != QUANTAPDF_OK) {
        pdf_drop_obj(edit->ctx, kept);
        return status;
    }

    if (out_count != NULL)
        *out_count = count;
    if (out_object != NULL)
        *out_object = kept;
    else
        pdf_drop_obj(edit->ctx, kept);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_resolve_live_annot(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_annotation_entry *entry,
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
            if (quantapdf_pdf_edit_same_identity(
                    edit->ctx, pdf_annot_obj(edit->ctx, annotation), entry->object)) {
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
        return quantapdf_status_from_mupdf(caught_code);
    }
    if (found == NULL) {
        if (page != NULL)
            fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_ERROR_STATE;
    }

    *out_page = page;
    *out_annot = found;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_edit_resolve_live_view(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_pdf_edit_annotation_entry **out_entry,
    pdf_page **out_page,
    pdf_annot **out_annot,
    quantapdf_pdf_annotation_view *out_view)
{
    quantapdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    quantapdf_status status;
    int caught_code = FZ_ERROR_NONE;

    if (out_entry != NULL)
        *out_entry = NULL;
    *out_page = NULL;
    *out_annot = NULL;
    memset(out_view, 0, sizeof(*out_view));

    status = quantapdf_pdf_edit_resolve_ref(edit, ref, &entry);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_pdf_edit_resolve_live_annot(edit, entry, &page, &annotation);
    if (status != QUANTAPDF_OK)
        return status;

    fz_var(status);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        quantapdf_annotation_type type;
        fz_matrix page_ctm;
        pdf_obj *object = pdf_annot_obj(edit->ctx, annotation);

        if (!quantapdf_pdf_annotation_classify(edit->ctx, object, &type)) {
            status = QUANTAPDF_ERROR_STATE;
        } else {
            pdf_page_transform(edit->ctx, page, NULL, &page_ctm);
            status = quantapdf_pdf_annotation_read_view(
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
        return quantapdf_status_from_mupdf(caught_code);
    }
    if (status != QUANTAPDF_OK) {
        fz_drop_page(edit->ctx, &page->super);
        return status;
    }

    if (out_entry != NULL)
        *out_entry = entry;
    *out_page = page;
    *out_annot = annotation;
    return QUANTAPDF_OK;
}

static int quantapdf_pdf_edit_map_create_type(
    quantapdf_annotation_type type,
    enum pdf_annot_type *out_type)
{
    switch (type) {
    case QUANTAPDF_ANNOTATION_TEXT:
        *out_type = PDF_ANNOT_TEXT;
        return 1;
    case QUANTAPDF_ANNOTATION_FREE_TEXT:
        *out_type = PDF_ANNOT_FREE_TEXT;
        return 1;
    case QUANTAPDF_ANNOTATION_SQUARE:
        *out_type = PDF_ANNOT_SQUARE;
        return 1;
    case QUANTAPDF_ANNOTATION_CIRCLE:
        *out_type = PDF_ANNOT_CIRCLE;
        return 1;
    default:
        return 0;
    }
}

static void quantapdf_pdf_edit_set_flags_u32(
    fz_context *ctx,
    pdf_annot *annotation,
    uint32_t flags)
{
    if (flags <= (uint32_t)INT_MAX) {
        pdf_set_annot_flags(ctx, annotation, (int)flags);
    } else {
        pdf_dict_put_int(
            ctx,
            pdf_annot_obj(ctx, annotation),
            PDF_NAME(F),
            (int64_t)(uint64_t)flags);
        pdf_annot_request_resynthesis(ctx, annotation);
    }
}

quantapdf_status quantapdf_pdf_edit_annotation_count(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t *out_count)
{
    size_t count = 0;
    quantapdf_status status;

    if (out_count != NULL)
        *out_count = 0;
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_scan_page(edit, page_index, 0, 0, &count, NULL);
    if (status != QUANTAPDF_OK)
        return status;
    *out_count = count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_annotation_ref_at(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref)
{
    pdf_obj *object = NULL;
    size_t count = 0;
    quantapdf_status status;

    if (out_ref != NULL)
        quantapdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_scan_page(
        edit, page_index, index, 1, &count, &object);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf_pdf_edit_register_object(edit, object, page_index, out_ref);
}

quantapdf_status quantapdf_pdf_edit_annotation_get_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info)
{
    quantapdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    quantapdf_pdf_annotation_view view;
    quantapdf_status status;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_annotation_info, flags) + sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->type = QUANTAPDF_ANNOTATION_UNKNOWN;
    quantapdf_pdf_edit_zero_rect(&out_info->bounds);
    out_info->flags = 0;

    status = quantapdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != QUANTAPDF_OK)
        return status;

    (void)entry;
    (void)annotation;
    out_info->type = view.type;
    out_info->bounds = view.bounds;
    out_info->flags = view.flags;
    fz_drop_page(edit->ctx, &page->super);
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_annotation_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    quantapdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    quantapdf_pdf_annotation_view view;
    quantapdf_status status;
    char *copy;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != QUANTAPDF_OK)
        return status;

    (void)entry;
    (void)annotation;
    if (!view.has_contents) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_OK;
    }
    if (view.contents_size == SIZE_MAX) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_ERROR_NOMEM;
    }

    copy = (char *)malloc(view.contents_size + 1);
    if (copy == NULL) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_ERROR_NOMEM;
    }
    memcpy(copy, view.contents_utf8, view.contents_size);
    copy[view.contents_size] = '\0';
    fz_drop_page(edit->ctx, &page->super);
    *out_utf8 = copy;
    *out_size = view.contents_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_annotation_create(
    quantapdf_pdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref)
{
    enum pdf_annot_type pdf_type = PDF_ANNOT_UNKNOWN;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    pdf_obj *object = NULL;
    char *contents_copy = NULL;
    int contents_present = 0;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;
    size_t minimum_size;

    if (out_ref != NULL)
        quantapdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL || options == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_annotation_create_options, contents_size) +
        sizeof(options->contents_size);
    if (options->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_validate_page(edit, page_index);
    if (status != QUANTAPDF_OK)
        return status;
    if (!quantapdf_pdf_edit_map_create_type(options->type, &pdf_type))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!quantapdf_pdf_edit_bounds_valid(options->bounds))
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_prepare_contents(
        options->contents_utf8,
        options->contents_size,
        &contents_present,
        &contents_copy);
    if (status != QUANTAPDF_OK)
        return status;

    status = quantapdf_pdf_edit_reserve_entries(edit, edit->entry_count + 1);
    if (status != QUANTAPDF_OK) {
        free(contents_copy);
        return status;
    }

    fz_var(page);
    fz_var(annotation);
    fz_var(object);
    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        fz_rect bounds = fz_make_rect(
            options->bounds.x0,
            options->bounds.y0,
            options->bounds.x1,
            options->bounds.y1);

        page = pdf_load_page(edit->ctx, edit->document, page_index);
        pdf_begin_operation(
            edit->ctx, edit->document, "QuantaPDF create annotation");
        operation_open = 1;

        annotation = pdf_create_annot(edit->ctx, page, pdf_type);
        if (pdf_type == PDF_ANNOT_SQUARE || pdf_type == PDF_ANNOT_CIRCLE)
            pdf_dict_del(
                edit->ctx,
                pdf_annot_obj(edit->ctx, annotation),
                PDF_NAME(RD));
        pdf_set_annot_rect(edit->ctx, annotation, bounds);
        quantapdf_pdf_edit_set_flags_u32(
            edit->ctx, annotation, options->flags);
        if (contents_present)
            pdf_set_annot_contents(edit->ctx, annotation, contents_copy);
        (void)pdf_update_annot(edit->ctx, annotation);

#if defined(QUANTAPDF_TESTING)
        if (edit->test_fault ==
            QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_CREATE_MUTATION) {
            edit->test_fault = QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE;
            fz_throw(
                edit->ctx,
                FZ_ERROR_GENERIC,
                "injected QuantaPDF create failure");
        }
#endif

        pdf_end_operation(edit->ctx, edit->document);
        operation_open = 0;
        object = pdf_keep_obj(
            edit->ctx, pdf_annot_obj(edit->ctx, annotation));
    }
    fz_always(edit->ctx)
    {
        if (annotation != NULL)
            pdf_drop_annot(edit->ctx, annotation);
        annotation = NULL;
        if (page != NULL)
            fz_drop_page(edit->ctx, &page->super);
        page = NULL;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        if (operation_open) {
            pdf_abandon_operation(edit->ctx, edit->document);
            operation_open = 0;
        }
        fz_report_error(edit->ctx);
    }

    free(contents_copy);
    if (caught_code != FZ_ERROR_NONE) {
        pdf_drop_obj(edit->ctx, object);
        return quantapdf_status_from_mupdf(caught_code);
    }
    if (object == NULL)
        return QUANTAPDF_ERROR_MUPDF;

    status = quantapdf_pdf_edit_register_object(
        edit, object, page_index, out_ref);
    return status;
}

quantapdf_status quantapdf_pdf_edit_annotation_update(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update)
{
    const uint32_t known_fields =
        QUANTAPDF_ANNOTATION_UPDATE_BOUNDS |
        QUANTAPDF_ANNOTATION_UPDATE_FLAGS |
        QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    quantapdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    quantapdf_pdf_annotation_view view;
    char *contents_copy = NULL;
    int contents_present = 0;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;
    size_t minimum_size;

    if (update == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_annotation_update, contents_size) +
        sizeof(update->contents_size);
    if (update->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;
    if ((update->fields & ~known_fields) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != QUANTAPDF_OK)
        return status;

    if (update->fields == 0) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_OK;
    }
    if (view.type == QUANTAPDF_ANNOTATION_UNKNOWN) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_BOUNDS) != 0) {
        enum pdf_annot_type ignored_type = PDF_ANNOT_UNKNOWN;

        if (!quantapdf_pdf_edit_map_create_type(view.type, &ignored_type)) {
            fz_drop_page(edit->ctx, &page->super);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        if (!quantapdf_pdf_edit_bounds_valid(update->bounds)) {
            fz_drop_page(edit->ctx, &page->super);
            return QUANTAPDF_ERROR_ARGUMENT;
        }
    }
    if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_CONTENTS) != 0) {
        status = quantapdf_pdf_edit_prepare_contents(
            update->contents_utf8,
            update->contents_size,
            &contents_present,
            &contents_copy);
        if (status != QUANTAPDF_OK) {
            fz_drop_page(edit->ctx, &page->super);
            return status;
        }
    }

    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        int applied_fields = 0;

        pdf_begin_operation(
            edit->ctx, edit->document, "QuantaPDF update annotation");
        operation_open = 1;

        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_BOUNDS) != 0) {
            fz_rect bounds = fz_make_rect(
                update->bounds.x0,
                update->bounds.y0,
                update->bounds.x1,
                update->bounds.y1);
            pdf_set_annot_rect(edit->ctx, annotation, bounds);
            ++applied_fields;
#if defined(QUANTAPDF_TESTING)
            if (applied_fields == 1 && edit->test_fault ==
                QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD) {
                edit->test_fault = QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE;
                fz_throw(
                    edit->ctx,
                    FZ_ERROR_GENERIC,
                    "injected QuantaPDF update failure");
            }
#endif
        }

        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_FLAGS) != 0) {
            quantapdf_pdf_edit_set_flags_u32(
                edit->ctx, annotation, update->flags);
            ++applied_fields;
#if defined(QUANTAPDF_TESTING)
            if (applied_fields == 1 && edit->test_fault ==
                QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD) {
                edit->test_fault = QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE;
                fz_throw(
                    edit->ctx,
                    FZ_ERROR_GENERIC,
                    "injected QuantaPDF update failure");
            }
#endif
        }

        if ((update->fields & QUANTAPDF_ANNOTATION_UPDATE_CONTENTS) != 0) {
            if (contents_present) {
                pdf_set_annot_contents(edit->ctx, annotation, contents_copy);
            } else {
                pdf_dict_del(
                    edit->ctx,
                    pdf_annot_obj(edit->ctx, annotation),
                    PDF_NAME(Contents));
                pdf_annot_request_resynthesis(edit->ctx, annotation);
            }
            ++applied_fields;
#if defined(QUANTAPDF_TESTING)
            if (applied_fields == 1 && edit->test_fault ==
                QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD) {
                edit->test_fault = QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE;
                fz_throw(
                    edit->ctx,
                    FZ_ERROR_GENERIC,
                    "injected QuantaPDF update failure");
            }
#endif
        }

        (void)pdf_update_annot(edit->ctx, annotation);
        pdf_end_operation(edit->ctx, edit->document);
        operation_open = 0;
    }
    fz_always(edit->ctx)
    {
        fz_drop_page(edit->ctx, &page->super);
        page = NULL;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        if (operation_open) {
            pdf_abandon_operation(edit->ctx, edit->document);
            operation_open = 0;
        }
        fz_report_error(edit->ctx);
    }

    free(contents_copy);
    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_annotation_delete(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref)
{
    quantapdf_pdf_edit_annotation_entry *entry = NULL;
    pdf_page *page = NULL;
    pdf_annot *annotation = NULL;
    quantapdf_pdf_annotation_view view;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;

    status = quantapdf_pdf_edit_resolve_live_view(
        edit, ref, &entry, &page, &annotation, &view);
    if (status != QUANTAPDF_OK)
        return status;
    if (view.type == QUANTAPDF_ANNOTATION_UNKNOWN) {
        fz_drop_page(edit->ctx, &page->super);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        pdf_begin_operation(
            edit->ctx, edit->document, "QuantaPDF delete annotation");
        operation_open = 1;
        pdf_delete_annot(edit->ctx, page, annotation);
        pdf_end_operation(edit->ctx, edit->document);
        operation_open = 0;
    }
    fz_always(edit->ctx)
    {
        fz_drop_page(edit->ctx, &page->super);
        page = NULL;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        if (operation_open) {
            pdf_abandon_operation(edit->ctx, edit->document);
            operation_open = 0;
        }
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);

    pdf_drop_obj(edit->ctx, entry->object);
    entry->object = NULL;
    entry->live = 0;
    return QUANTAPDF_OK;
}
