#include "pdf_trim_internal.h"

#include <math.h>
#include <stddef.h>

static int extractpdf_pdf_trim_dict_has_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key)
{
    int count;
    int index;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key))
            return 1;
    }
    return 0;
}

typedef struct extractpdf_pdf_trim_signature_scan {
    pdf_document *document;
    int has_signed_field;
} extractpdf_pdf_trim_signature_scan;

static void extractpdf_pdf_trim_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    extractpdf_pdf_trim_signature_scan *scan =
        (extractpdf_pdf_trim_signature_scan *)data;

    if (scan->has_signed_field)
        return;
    if (!pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static int extractpdf_pdf_trim_has_signed_field(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    extractpdf_pdf_trim_signature_scan scan;
    pdf_obj *field_type = NULL;
    pdf_obj *fields;

    scan.document = document;
    scan.has_signed_field = 0;
    fields = pdf_dict_getp(
        ctx,
        pdf_trailer(ctx, document),
        "Root/AcroForm/Fields");
    pdf_walk_tree(
        ctx,
        fields,
        PDF_NAME(Kids),
        extractpdf_pdf_trim_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field;
}

extractpdf_status extractpdf_pdf_trim_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    int encrypted = 0;
    int signed_field = 0;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(encrypted);
    fz_var(signed_field);
    fz_var(caught_code);
    fz_try(ctx)
    {
        pdf_obj *trailer = pdf_trailer(ctx, document);
        encrypted = extractpdf_pdf_trim_dict_has_key(
            ctx, trailer, PDF_NAME(Encrypt));
        if (!encrypted)
            signed_field = extractpdf_pdf_trim_has_signed_field(ctx, document);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (encrypted || signed_field)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_trim_finite_public_rect(extractpdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1);
}

static int extractpdf_pdf_trim_public_equal(
    extractpdf_rect left,
    extractpdf_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int extractpdf_pdf_trim_public_inside(
    extractpdf_rect inner,
    extractpdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static int extractpdf_pdf_trim_raw_inside(fz_rect inner, fz_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

static int extractpdf_pdf_trim_raw_equal(fz_rect left, fz_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static fz_rect extractpdf_pdf_trim_intersection(fz_rect left, fz_rect right)
{
    fz_rect result;

    result.x0 = fmaxf(left.x0, right.x0);
    result.y0 = fmaxf(left.y0, right.y0);
    result.x1 = fminf(left.x1, right.x1);
    result.y1 = fminf(left.y1, right.y1);
    return result;
}

static int extractpdf_pdf_trim_positive_raw(fz_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1) &&
        rect.x0 < rect.x1 && rect.y0 < rect.y1;
}

extractpdf_status extractpdf_pdf_trim_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_pdf_trim_plan *plans,
    int *out_any_changed)
{
    const size_t minimum_size =
        offsetof(extractpdf_page_trim, bounds) + sizeof(extractpdf_rect);
    size_t index;

    if (out_any_changed != NULL)
        *out_any_changed = 0;
    if (ctx == NULL || document == NULL || trims == NULL ||
        trim_count == 0 || plans == NULL || out_any_changed == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    for (index = 0; index < trim_count; ++index) {
        extractpdf_pdf_page_box_view view;
        fz_rect public_rect;
        fz_rect requested_media_pdf;
        fz_matrix public_to_pdf;
        extractpdf_status status;
        size_t prior;

        if (trims[index].struct_size < minimum_size)
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (!extractpdf_pdf_trim_finite_public_rect(trims[index].bounds) ||
            !(trims[index].bounds.x0 < trims[index].bounds.x1) ||
            !(trims[index].bounds.y0 < trims[index].bounds.y1))
            return EXTRACTPDF_ERROR_ARGUMENT;

        for (prior = 0; prior < index; ++prior) {
            if (trims[prior].page_index == trims[index].page_index)
                return EXTRACTPDF_ERROR_ARGUMENT;
        }

        status = extractpdf_pdf_page_box_resolve(
            ctx, document, trims[index].page_index, &view);
        if (status != EXTRACTPDF_OK)
            return status;
        if (!extractpdf_pdf_trim_public_inside(
                trims[index].bounds, view.media_public))
            return EXTRACTPDF_ERROR_ARGUMENT;

        public_rect.x0 = trims[index].bounds.x0;
        public_rect.y0 = trims[index].bounds.y0;
        public_rect.x1 = trims[index].bounds.x1;
        public_rect.y1 = trims[index].bounds.y1;
        public_to_pdf = fz_invert_matrix(view.pdf_to_public);
        requested_media_pdf = fz_transform_rect(public_rect, public_to_pdf);
        requested_media_pdf.x0 = fminf(
            requested_media_pdf.x0, requested_media_pdf.x1);
        requested_media_pdf.y0 = fminf(
            requested_media_pdf.y0, requested_media_pdf.y1);
        requested_media_pdf.x1 = fmaxf(
            requested_media_pdf.x0, requested_media_pdf.x1);
        requested_media_pdf.y1 = fmaxf(
            requested_media_pdf.y0, requested_media_pdf.y1);

        if (!extractpdf_pdf_trim_positive_raw(requested_media_pdf) ||
            !extractpdf_pdf_trim_raw_inside(
                requested_media_pdf, view.media_pdf))
            return EXTRACTPDF_ERROR_ARGUMENT;

        plans[index].page_index = trims[index].page_index;
        plans[index].requested_public = trims[index].bounds;
        plans[index].requested_media_pdf = requested_media_pdf;
        plans[index].changed = !extractpdf_pdf_trim_public_equal(
            trims[index].bounds, view.media_public);

        if (view.has_explicit_crop) {
            plans[index].output_visible_pdf = extractpdf_pdf_trim_intersection(
                requested_media_pdf, view.crop_pdf);
        }
        else {
            plans[index].output_visible_pdf = requested_media_pdf;
        }

        if (!extractpdf_pdf_trim_positive_raw(
                plans[index].output_visible_pdf))
            return plans[index].changed ?
                EXTRACTPDF_ERROR_ARGUMENT : EXTRACTPDF_ERROR_FORMAT;

        plans[index].frame_changed = !extractpdf_pdf_trim_raw_equal(
            plans[index].output_visible_pdf, view.visible_pdf);
        if (plans[index].changed)
            *out_any_changed = 1;
    }

    return EXTRACTPDF_OK;
}
