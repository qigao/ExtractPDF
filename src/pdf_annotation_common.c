#include "pdf_annotation_common.h"
#include "pdf_object_common.h"

#include <string.h>

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
    present = extractpdf_pdf_dict_find(
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

static extractpdf_status extractpdf_pdf_annotation_read_contents(
    fz_context *ctx,
    pdf_obj *annotation,
    extractpdf_pdf_annotation_view *out_view)
{
    pdf_obj *contents_obj = NULL;
    const char *text;
    int present;

    present = extractpdf_pdf_dict_find(
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

    status = extractpdf_pdf_read_rect(
        ctx, annotation, PDF_NAME(Rect), page_ctm, &out_view->bounds);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_pdf_read_optional_uint32(
        ctx, annotation, PDF_NAME(F), 0, &out_view->flags);
    if (status != EXTRACTPDF_OK)
        return status;

    return extractpdf_pdf_annotation_read_contents(ctx, annotation, out_view);
}
