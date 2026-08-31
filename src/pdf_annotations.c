#include "pdf_annotation_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct quantapdf_annotation_internal {
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t contents_offset;
    size_t contents_size;
    int has_contents;
} quantapdf_annotation_internal;

struct quantapdf_annotation_page {
    quantapdf_annotation_internal *items;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};

static void quantapdf_zero_annotation_rect(quantapdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static void quantapdf_dispose_annotation_page(
    quantapdf_annotation_page *annotations)
{
    if (annotations == NULL)
        return;

    free(annotations->items);
    free(annotations->strings);
    free(annotations);
}

static quantapdf_annotation_page *quantapdf_allocate_annotation_page(
    size_t count)
{
    quantapdf_annotation_page *annotations;

    annotations = (quantapdf_annotation_page *)calloc(1, sizeof(*annotations));
    if (annotations == NULL)
        return NULL;

    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*annotations->items)) {
            free(annotations);
            return NULL;
        }
        annotations->items = (quantapdf_annotation_internal *)calloc(
            count, sizeof(*annotations->items));
        if (annotations->items == NULL) {
            free(annotations);
            return NULL;
        }
    }

    annotations->count = count;
    return annotations;
}

static quantapdf_status quantapdf_annotation_append_string(
    quantapdf_annotation_page *annotations,
    const char *text,
    size_t *out_offset,
    size_t *out_size)
{
    size_t size;
    size_t required;
    size_t capacity;
    char *grown;

    size = strlen(text);
    if (size == SIZE_MAX || annotations->string_size > SIZE_MAX - size - 1)
        return QUANTAPDF_ERROR_NOMEM;

    required = annotations->string_size + size + 1;
    if (required > annotations->string_capacity) {
        capacity = annotations->string_capacity != 0 ?
            annotations->string_capacity : 64;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        if (capacity < required)
            return QUANTAPDF_ERROR_NOMEM;

        grown = (char *)realloc(annotations->strings, capacity);
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        annotations->strings = grown;
        annotations->string_capacity = capacity;
    }

    *out_offset = annotations->string_size;
    *out_size = size;
    memcpy(annotations->strings + annotations->string_size, text, size + 1);
    annotations->string_size = required;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_extract_annotations(
    quantapdf_page *page,
    quantapdf_annotation_page **out_annotations)
{
    quantapdf_annotation_page *annotations;
    fz_context *ctx;
    pdf_page *pdf_page = NULL;
    pdf_obj *annots = NULL;
    size_t count = 0;
    size_t output_index = 0;
    int annotation_count = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status = QUANTAPDF_OK;

    if (out_annotations == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_annotations = NULL;

    if (page == NULL || page->document == NULL ||
        page->document->ctx == NULL || page->document->doc == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_page_ensure_mupdf(page);
    if (status != QUANTAPDF_OK)
        return status;

    ctx = page->document->ctx;

    fz_var(pdf_page);
    fz_var(annots);
    fz_var(count);
    fz_var(annotation_count);
    fz_var(caught_code);

    fz_try(ctx)
    {
        int index;

        pdf_page = pdf_page_from_fz_page(ctx, page->page);
        if (pdf_page != NULL) {
            annots = pdf_dict_get(ctx, pdf_page->obj, PDF_NAME(Annots));
            annotation_count = pdf_array_len(ctx, annots);
            for (index = 0; index < annotation_count; ++index) {
                pdf_obj *annotation = pdf_array_get(ctx, annots, index);
                quantapdf_annotation_type type;

                if (!pdf_is_dict(ctx, annotation))
                    continue;
                if (!quantapdf_pdf_annotation_classify(
                        ctx, annotation, &type))
                    continue;
                if (count == SIZE_MAX) {
                    status = QUANTAPDF_ERROR_NOMEM;
                    break;
                }
                ++count;
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    if (pdf_page == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (status != QUANTAPDF_OK)
        return status;

    annotations = quantapdf_allocate_annotation_page(count);
    if (annotations == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (count == 0) {
        *out_annotations = annotations;
        return QUANTAPDF_OK;
    }

    caught_code = FZ_ERROR_NONE;
    status = QUANTAPDF_OK;
    fz_var(output_index);
    fz_var(caught_code);
    fz_var(status);

    fz_try(ctx)
    {
        fz_matrix page_ctm;
        int index;

        pdf_page_transform(ctx, pdf_page, NULL, &page_ctm);
        for (index = 0; index < annotation_count; ++index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, index);
            quantapdf_annotation_type type;
            quantapdf_pdf_annotation_view view;
            quantapdf_annotation_internal *item;

            if (!pdf_is_dict(ctx, annotation))
                continue;
            if (!quantapdf_pdf_annotation_classify(
                    ctx, annotation, &type))
                continue;
            if (output_index >= annotations->count) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }

            status = quantapdf_pdf_annotation_read_view(
                ctx, annotation, type, page_ctm, &view);
            if (status != QUANTAPDF_OK)
                break;

            item = &annotations->items[output_index];
            item->type = view.type;
            item->bounds = view.bounds;
            item->flags = view.flags;
            if (view.has_contents) {
                status = quantapdf_annotation_append_string(
                    annotations,
                    view.contents_utf8,
                    &item->contents_offset,
                    &item->contents_size);
                if (status != QUANTAPDF_OK)
                    break;
                item->has_contents = 1;
            }
            ++output_index;
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status caught_status =
            quantapdf_status_from_backend(caught_code);
        quantapdf_dispose_annotation_page(annotations);
        return caught_status;
    }
    if (status != QUANTAPDF_OK || output_index != annotations->count) {
        if (status == QUANTAPDF_OK)
            status = QUANTAPDF_ERROR_FORMAT;
        quantapdf_dispose_annotation_page(annotations);
        return status;
    }

    *out_annotations = annotations;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_annotation_count(
    const quantapdf_annotation_page *annotations,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;

    if (annotations == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = annotations->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_annotation_get_info(
    const quantapdf_annotation_page *annotations,
    size_t index,
    quantapdf_annotation_info *out_info)
{
    const quantapdf_annotation_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->type = QUANTAPDF_ANNOTATION_UNKNOWN;
    quantapdf_zero_annotation_rect(&out_info->bounds);
    out_info->flags = 0;

    if (annotations == NULL || index >= annotations->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    out_info->type = item->type;
    out_info->bounds = item->bounds;
    out_info->flags = item->flags;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_annotation_contents(
    const quantapdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_annotation_internal *item;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (annotations == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= annotations->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    if (!item->has_contents)
        return QUANTAPDF_OK;

    *out_utf8 = annotations->strings + item->contents_offset;
    *out_size = item->contents_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_annotation_page(
    quantapdf_annotation_page *annotations)
{
    quantapdf_dispose_annotation_page(annotations);
}
