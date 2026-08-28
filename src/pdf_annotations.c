#include "pdf_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_annotation_internal {
    extractpdf_annotation_type type;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t contents_offset;
    size_t contents_size;
    int has_contents;
} extractpdf_annotation_internal;

struct extractpdf_annotation_page {
    extractpdf_annotation_internal *items;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};

static void extractpdf_zero_annotation_rect(extractpdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static void extractpdf_dispose_annotation_page(
    extractpdf_annotation_page *annotations)
{
    if (annotations == NULL)
        return;

    free(annotations->items);
    free(annotations->strings);
    free(annotations);
}

static extractpdf_annotation_page *extractpdf_allocate_annotation_page(
    size_t count)
{
    extractpdf_annotation_page *annotations;

    annotations = (extractpdf_annotation_page *)calloc(1, sizeof(*annotations));
    if (annotations == NULL)
        return NULL;

    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*annotations->items)) {
            free(annotations);
            return NULL;
        }
        annotations->items = (extractpdf_annotation_internal *)calloc(
            count, sizeof(*annotations->items));
        if (annotations->items == NULL) {
            free(annotations);
            return NULL;
        }
    }

    annotations->count = count;
    return annotations;
}

static extractpdf_status extractpdf_annotation_append_string(
    extractpdf_annotation_page *annotations,
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
        return EXTRACTPDF_ERROR_NOMEM;

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
            return EXTRACTPDF_ERROR_NOMEM;

        grown = (char *)realloc(annotations->strings, capacity);
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        annotations->strings = grown;
        annotations->string_capacity = capacity;
    }

    *out_offset = annotations->string_size;
    *out_size = size;
    memcpy(annotations->strings + annotations->string_size, text, size + 1);
    annotations->string_size = required;
    return EXTRACTPDF_OK;
}

static int extractpdf_annotation_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count;
    int index;

    *out_value = NULL;
    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        pdf_obj *candidate = pdf_dict_get_key(ctx, dictionary, index);
        if (pdf_name_eq(ctx, candidate, key)) {
            *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

static extractpdf_annotation_type extractpdf_annotation_type_from_name(
    const char *name)
{
    if (strcmp(name, "Text") == 0)
        return EXTRACTPDF_ANNOTATION_TEXT;
    if (strcmp(name, "FreeText") == 0)
        return EXTRACTPDF_ANNOTATION_FREE_TEXT;
    if (strcmp(name, "Line") == 0)
        return EXTRACTPDF_ANNOTATION_LINE;
    if (strcmp(name, "Square") == 0)
        return EXTRACTPDF_ANNOTATION_SQUARE;
    if (strcmp(name, "Circle") == 0)
        return EXTRACTPDF_ANNOTATION_CIRCLE;
    if (strcmp(name, "Polygon") == 0)
        return EXTRACTPDF_ANNOTATION_POLYGON;
    if (strcmp(name, "PolyLine") == 0)
        return EXTRACTPDF_ANNOTATION_POLY_LINE;
    if (strcmp(name, "Highlight") == 0)
        return EXTRACTPDF_ANNOTATION_HIGHLIGHT;
    if (strcmp(name, "Underline") == 0)
        return EXTRACTPDF_ANNOTATION_UNDERLINE;
    if (strcmp(name, "Squiggly") == 0)
        return EXTRACTPDF_ANNOTATION_SQUIGGLY;
    if (strcmp(name, "StrikeOut") == 0)
        return EXTRACTPDF_ANNOTATION_STRIKE_OUT;
    if (strcmp(name, "Redact") == 0)
        return EXTRACTPDF_ANNOTATION_REDACT;
    if (strcmp(name, "Stamp") == 0)
        return EXTRACTPDF_ANNOTATION_STAMP;
    if (strcmp(name, "Caret") == 0)
        return EXTRACTPDF_ANNOTATION_CARET;
    if (strcmp(name, "Ink") == 0)
        return EXTRACTPDF_ANNOTATION_INK;
    if (strcmp(name, "FileAttachment") == 0)
        return EXTRACTPDF_ANNOTATION_FILE_ATTACHMENT;
    if (strcmp(name, "Sound") == 0)
        return EXTRACTPDF_ANNOTATION_SOUND;
    if (strcmp(name, "Movie") == 0)
        return EXTRACTPDF_ANNOTATION_MOVIE;
    if (strcmp(name, "RichMedia") == 0)
        return EXTRACTPDF_ANNOTATION_RICH_MEDIA;
    if (strcmp(name, "Screen") == 0)
        return EXTRACTPDF_ANNOTATION_SCREEN;
    if (strcmp(name, "PrinterMark") == 0)
        return EXTRACTPDF_ANNOTATION_PRINTER_MARK;
    if (strcmp(name, "TrapNet") == 0)
        return EXTRACTPDF_ANNOTATION_TRAP_NET;
    if (strcmp(name, "Watermark") == 0)
        return EXTRACTPDF_ANNOTATION_WATERMARK;
    if (strcmp(name, "3D") == 0)
        return EXTRACTPDF_ANNOTATION_3D;
    if (strcmp(name, "Projection") == 0)
        return EXTRACTPDF_ANNOTATION_PROJECTION;
    return EXTRACTPDF_ANNOTATION_UNKNOWN;
}

static int extractpdf_annotation_classify(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type *out_type)
{
    pdf_obj *subtype = NULL;
    const char *name;
    int present;

    *out_type = EXTRACTPDF_ANNOTATION_UNKNOWN;
    present = extractpdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(Subtype), &subtype);
    if (!present || !pdf_is_name(ctx, subtype))
        return 1;

    name = pdf_to_name(ctx, subtype);
    if (strcmp(name, "Link") == 0 ||
        strcmp(name, "Popup") == 0 ||
        strcmp(name, "Widget") == 0)
        return 0;

    *out_type = extractpdf_annotation_type_from_name(name);
    return 1;
}

static extractpdf_status extractpdf_annotation_read_bounds(
    fz_context *ctx,
    pdf_obj *annotation,
    fz_matrix page_ctm,
    extractpdf_rect *out_bounds)
{
    pdf_obj *rect_obj = NULL;
    float values[4];
    fz_rect raw;
    fz_rect transformed;
    int present;
    int index;

    present = extractpdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(Rect), &rect_obj);
    if (!present || !pdf_is_array(ctx, rect_obj) ||
        pdf_array_len(ctx, rect_obj) != 4)
        return EXTRACTPDF_ERROR_FORMAT;

    for (index = 0; index < 4; ++index) {
        pdf_obj *value = pdf_array_get(ctx, rect_obj, index);
        if (!pdf_is_number(ctx, value))
            return EXTRACTPDF_ERROR_FORMAT;
        values[index] = pdf_to_real(ctx, value);
        if (!isfinite(values[index]))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    raw.x0 = values[0] < values[2] ? values[0] : values[2];
    raw.x1 = values[0] < values[2] ? values[2] : values[0];
    raw.y0 = values[1] < values[3] ? values[1] : values[3];
    raw.y1 = values[1] < values[3] ? values[3] : values[1];
    transformed = fz_transform_rect(raw, page_ctm);

    if (!isfinite(transformed.x0) || !isfinite(transformed.y0) ||
        !isfinite(transformed.x1) || !isfinite(transformed.y1))
        return EXTRACTPDF_ERROR_FORMAT;

    out_bounds->x0 = transformed.x0 < transformed.x1 ?
        transformed.x0 : transformed.x1;
    out_bounds->x1 = transformed.x0 < transformed.x1 ?
        transformed.x1 : transformed.x0;
    out_bounds->y0 = transformed.y0 < transformed.y1 ?
        transformed.y0 : transformed.y1;
    out_bounds->y1 = transformed.y0 < transformed.y1 ?
        transformed.y1 : transformed.y0;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_annotation_read_flags(
    fz_context *ctx,
    pdf_obj *annotation,
    uint32_t *out_flags)
{
    pdf_obj *flags_obj = NULL;
    int64_t value;
    int present;

    *out_flags = 0;
    present = extractpdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(F), &flags_obj);
    if (!present)
        return EXTRACTPDF_OK;
    if (!pdf_is_int(ctx, flags_obj))
        return EXTRACTPDF_ERROR_FORMAT;

    value = pdf_to_int64(ctx, flags_obj);
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return EXTRACTPDF_ERROR_FORMAT;

    *out_flags = (uint32_t)value;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_annotation_read_contents(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_page *annotations,
    extractpdf_annotation_internal *item)
{
    pdf_obj *contents_obj = NULL;
    const char *text;
    int present;

    present = extractpdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(Contents), &contents_obj);
    if (!present)
        return EXTRACTPDF_OK;
    if (!pdf_is_string(ctx, contents_obj))
        return EXTRACTPDF_ERROR_FORMAT;

    text = pdf_to_text_string(ctx, contents_obj);
    if (text == NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    item->has_contents = 1;
    return extractpdf_annotation_append_string(
        annotations,
        text,
        &item->contents_offset,
        &item->contents_size);
}

static extractpdf_status extractpdf_annotation_materialize(
    fz_context *ctx,
    pdf_obj *annotation,
    fz_matrix page_ctm,
    extractpdf_annotation_page *annotations,
    size_t index,
    extractpdf_annotation_type type)
{
    extractpdf_annotation_internal *item = &annotations->items[index];
    extractpdf_status status;

    item->type = type;

    status = extractpdf_annotation_read_bounds(
        ctx, annotation, page_ctm, &item->bounds);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_annotation_read_flags(
        ctx, annotation, &item->flags);
    if (status != EXTRACTPDF_OK)
        return status;

    return extractpdf_annotation_read_contents(
        ctx, annotation, annotations, item);
}

extractpdf_status extractpdf_extract_annotations(
    extractpdf_page *page,
    extractpdf_annotation_page **out_annotations)
{
    extractpdf_annotation_page *annotations;
    fz_context *ctx;
    pdf_page *pdf_page = NULL;
    pdf_obj *annots = NULL;
    size_t count = 0;
    size_t output_index = 0;
    int annotation_count = 0;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status = EXTRACTPDF_OK;

    if (out_annotations == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_annotations = NULL;

    if (page == NULL || page->page == NULL || page->document == NULL ||
        page->document->ctx == NULL || page->document->doc == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

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
                extractpdf_annotation_type type;

                if (!pdf_is_dict(ctx, annotation))
                    continue;
                if (!extractpdf_annotation_classify(ctx, annotation, &type))
                    continue;
                if (count == SIZE_MAX) {
                    status = EXTRACTPDF_ERROR_NOMEM;
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
        return extractpdf_status_from_mupdf(caught_code);
    if (pdf_page == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (status != EXTRACTPDF_OK)
        return status;

    annotations = extractpdf_allocate_annotation_page(count);
    if (annotations == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    if (count == 0) {
        *out_annotations = annotations;
        return EXTRACTPDF_OK;
    }

    caught_code = FZ_ERROR_NONE;
    status = EXTRACTPDF_OK;
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
            extractpdf_annotation_type type;

            if (!pdf_is_dict(ctx, annotation))
                continue;
            if (!extractpdf_annotation_classify(ctx, annotation, &type))
                continue;
            if (output_index >= annotations->count) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            status = extractpdf_annotation_materialize(
                ctx,
                annotation,
                page_ctm,
                annotations,
                output_index,
                type);
            if (status != EXTRACTPDF_OK)
                break;
            ++output_index;
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_status caught_status =
            extractpdf_status_from_mupdf(caught_code);
        extractpdf_dispose_annotation_page(annotations);
        return caught_status;
    }
    if (status != EXTRACTPDF_OK || output_index != annotations->count) {
        if (status == EXTRACTPDF_OK)
            status = EXTRACTPDF_ERROR_FORMAT;
        extractpdf_dispose_annotation_page(annotations);
        return status;
    }

    *out_annotations = annotations;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_annotation_count(
    const extractpdf_annotation_page *annotations,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;

    if (annotations == NULL || out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_count = annotations->count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_annotation_get_info(
    const extractpdf_annotation_page *annotations,
    size_t index,
    extractpdf_annotation_info *out_info)
{
    const extractpdf_annotation_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->type = EXTRACTPDF_ANNOTATION_UNKNOWN;
    extractpdf_zero_annotation_rect(&out_info->bounds);
    out_info->flags = 0;

    if (annotations == NULL || index >= annotations->count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    out_info->type = item->type;
    out_info->bounds = item->bounds;
    out_info->flags = item->flags;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_annotation_contents(
    const extractpdf_annotation_page *annotations,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const extractpdf_annotation_internal *item;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (annotations == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= annotations->count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    item = &annotations->items[index];
    if (!item->has_contents)
        return EXTRACTPDF_OK;

    *out_utf8 = annotations->strings + item->contents_offset;
    *out_size = item->contents_size;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_annotation_page(
    extractpdf_annotation_page *annotations)
{
    extractpdf_dispose_annotation_page(annotations);
}
