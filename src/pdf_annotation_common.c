#include "pdf_annotation_common.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int extractpdf_pdf_annotation_dict_find(
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

static extractpdf_annotation_type extractpdf_pdf_annotation_type_from_name(
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

int extractpdf_pdf_annotation_classify(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type *out_type)
{
    pdf_obj *subtype = NULL;
    const char *name;
    int present;

    *out_type = EXTRACTPDF_ANNOTATION_UNKNOWN;
    present = extractpdf_pdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(Subtype), &subtype);
    if (!present || !pdf_is_name(ctx, subtype))
        return 1;

    name = pdf_to_name(ctx, subtype);
    if (strcmp(name, "Link") == 0 ||
        strcmp(name, "Popup") == 0 ||
        strcmp(name, "Widget") == 0)
        return 0;

    *out_type = extractpdf_pdf_annotation_type_from_name(name);
    return 1;
}

static extractpdf_status extractpdf_pdf_annotation_read_bounds(
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

    present = extractpdf_pdf_annotation_dict_find(
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

static extractpdf_status extractpdf_pdf_annotation_read_flags(
    fz_context *ctx,
    pdf_obj *annotation,
    uint32_t *out_flags)
{
    pdf_obj *flags_obj = NULL;
    int64_t value;
    int present;

    *out_flags = 0;
    present = extractpdf_pdf_annotation_dict_find(
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

static extractpdf_status extractpdf_pdf_annotation_read_contents(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_pdf_annotation_view *out_view)
{
    pdf_obj *contents_obj = NULL;
    const char *text;
    int present;

    present = extractpdf_pdf_annotation_dict_find(
        ctx, annotation, PDF_NAME(Contents), &contents_obj);
    if (!present)
        return EXTRACTPDF_OK;
    if (!pdf_is_string(ctx, contents_obj))
        return EXTRACTPDF_ERROR_FORMAT;

    text = pdf_to_text_string(ctx, contents_obj);
    if (text == NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    out_view->has_contents = 1;
    out_view->contents_utf8 = text;
    out_view->contents_size = strlen(text);
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_annotation_read_view(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_annotation_type type,
    fz_matrix page_ctm,
    extractpdf_pdf_annotation_view *out_view)
{
    extractpdf_status status;

    memset(out_view, 0, sizeof(*out_view));
    out_view->type = type;

    status = extractpdf_pdf_annotation_read_bounds(
        ctx, annotation, page_ctm, &out_view->bounds);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_pdf_annotation_read_flags(
        ctx, annotation, &out_view->flags);
    if (status != EXTRACTPDF_OK)
        return status;

    return extractpdf_pdf_annotation_read_contents(ctx, annotation, out_view);
}
