#include "pdf_crop_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define EXTRACTPDF_PDF_CROP_MAX_PAGE_TREE_DEPTH 256

static int extractpdf_pdf_crop_dict_has_key(
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

typedef struct extractpdf_pdf_crop_signature_scan {
    pdf_document *document;
    int has_signed_field;
} extractpdf_pdf_crop_signature_scan;

static void extractpdf_pdf_crop_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    extractpdf_pdf_crop_signature_scan *scan =
        (extractpdf_pdf_crop_signature_scan *)data;

    if (scan->has_signed_field)
        return;
    if (!pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static int extractpdf_pdf_crop_has_signed_field(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    extractpdf_pdf_crop_signature_scan scan;
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
        extractpdf_pdf_crop_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field;
}

extractpdf_status extractpdf_pdf_crop_check_security(
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
        encrypted = extractpdf_pdf_crop_dict_has_key(
            ctx, trailer, PDF_NAME(Encrypt));
        if (!encrypted)
            signed_field = extractpdf_pdf_crop_has_signed_field(ctx, document);
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

static extractpdf_status extractpdf_pdf_crop_parse_box(
    fz_context *ctx,
    pdf_obj *object,
    fz_rect *out_rect)
{
    float x0;
    float y0;
    float x1;
    float y1;

    if (!pdf_is_array(ctx, object) || pdf_array_len(ctx, object) != 4)
        return EXTRACTPDF_ERROR_FORMAT;
    if (!pdf_is_number(ctx, pdf_array_get(ctx, object, 0)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 1)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 2)) ||
        !pdf_is_number(ctx, pdf_array_get(ctx, object, 3)))
        return EXTRACTPDF_ERROR_FORMAT;

    x0 = pdf_to_real(ctx, pdf_array_get(ctx, object, 0));
    y0 = pdf_to_real(ctx, pdf_array_get(ctx, object, 1));
    x1 = pdf_to_real(ctx, pdf_array_get(ctx, object, 2));
    y1 = pdf_to_real(ctx, pdf_array_get(ctx, object, 3));
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
        return EXTRACTPDF_ERROR_FORMAT;

    out_rect->x0 = fminf(x0, x1);
    out_rect->y0 = fminf(y0, y1);
    out_rect->x1 = fmaxf(x0, x1);
    out_rect->y1 = fmaxf(y0, y1);
    if (!(out_rect->x0 < out_rect->x1) ||
        !(out_rect->y0 < out_rect->y1))
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_crop_same_object(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return pdf_objcmp_resolve(ctx, left, right) == 0;
}

static extractpdf_status extractpdf_pdf_crop_resolve_page_imp(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_pdf_crop_page_view *out_view)
{
    pdf_obj *page_obj;
    pdf_obj *node;
    pdf_obj *seen[EXTRACTPDF_PDF_CROP_MAX_PAGE_TREE_DEPTH + 1];
    size_t seen_count = 0;
    pdf_obj *media_obj = NULL;
    pdf_obj *crop_obj = NULL;
    pdf_obj *rotate_obj = NULL;
    pdf_obj *user_unit_obj;
    fz_rect public_visible;
    fz_matrix pdf_to_public;
    int page_count;
    int rotate = 0;
    float user_unit = 1.0f;
    size_t depth;

    page_count = pdf_count_pages(ctx, document);
    if (page_index < 0 || page_index >= page_count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    page_obj = pdf_lookup_page_obj(ctx, document, page_index);
    if (!pdf_is_dict(ctx, page_obj))
        return EXTRACTPDF_ERROR_FORMAT;

    node = page_obj;
    for (depth = 0;; ++depth) {
        pdf_obj *parent;
        size_t index;

        if (depth > EXTRACTPDF_PDF_CROP_MAX_PAGE_TREE_DEPTH)
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        if (!pdf_is_dict(ctx, node))
            return EXTRACTPDF_ERROR_FORMAT;

        for (index = 0; index < seen_count; ++index) {
            if (extractpdf_pdf_crop_same_object(ctx, seen[index], node))
                return EXTRACTPDF_ERROR_FORMAT;
        }
        seen[seen_count++] = node;

        if (media_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(MediaBox));
            if (value != NULL)
                media_obj = value;
        }
        if (crop_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(CropBox));
            if (value != NULL)
                crop_obj = value;
        }
        if (rotate_obj == NULL) {
            pdf_obj *value = pdf_dict_get(ctx, node, PDF_NAME(Rotate));
            if (value != NULL)
                rotate_obj = value;
        }

        parent = pdf_dict_get(ctx, node, PDF_NAME(Parent));
        if (parent == NULL || pdf_is_null(ctx, parent))
            break;
        node = parent;
    }

    if (media_obj == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (extractpdf_pdf_crop_parse_box(ctx, media_obj, &out_view->media_pdf) !=
        EXTRACTPDF_OK)
        return EXTRACTPDF_ERROR_FORMAT;

    if (crop_obj == NULL)
        out_view->crop_pdf = out_view->media_pdf;
    else if (extractpdf_pdf_crop_parse_box(ctx, crop_obj, &out_view->crop_pdf) !=
             EXTRACTPDF_OK)
        return EXTRACTPDF_ERROR_FORMAT;

    out_view->visible_pdf.x0 = fmaxf(
        out_view->media_pdf.x0, out_view->crop_pdf.x0);
    out_view->visible_pdf.y0 = fmaxf(
        out_view->media_pdf.y0, out_view->crop_pdf.y0);
    out_view->visible_pdf.x1 = fminf(
        out_view->media_pdf.x1, out_view->crop_pdf.x1);
    out_view->visible_pdf.y1 = fminf(
        out_view->media_pdf.y1, out_view->crop_pdf.y1);
    if (!(out_view->visible_pdf.x0 < out_view->visible_pdf.x1) ||
        !(out_view->visible_pdf.y0 < out_view->visible_pdf.y1))
        return EXTRACTPDF_ERROR_FORMAT;

    if (rotate_obj != NULL) {
        if (!pdf_is_int(ctx, rotate_obj))
            return EXTRACTPDF_ERROR_FORMAT;
        rotate = pdf_to_int(ctx, rotate_obj);
        if (rotate % 90 != 0)
            return EXTRACTPDF_ERROR_FORMAT;
        rotate %= 360;
        if (rotate < 0)
            rotate += 360;
    }

    user_unit_obj = pdf_dict_get(ctx, page_obj, PDF_NAME(UserUnit));
    if (user_unit_obj != NULL) {
        if (!pdf_is_number(ctx, user_unit_obj))
            return EXTRACTPDF_ERROR_FORMAT;
        user_unit = pdf_to_real(ctx, user_unit_obj);
        if (!isfinite(user_unit) || !(user_unit > 0.0f))
            return EXTRACTPDF_ERROR_FORMAT;
    }

    pdf_page_obj_transform(
        ctx, page_obj, NULL, &out_view->public_to_pdf);
    pdf_to_public = fz_invert_matrix(out_view->public_to_pdf);
    public_visible = fz_transform_rect(out_view->visible_pdf, pdf_to_public);
    if (!isfinite(public_visible.x0) || !isfinite(public_visible.y0) ||
        !isfinite(public_visible.x1) || !isfinite(public_visible.y1) ||
        !(public_visible.x0 < public_visible.x1) ||
        !(public_visible.y0 < public_visible.y1))
        return EXTRACTPDF_ERROR_FORMAT;

    out_view->page_obj = page_obj;
    out_view->visible_public.x0 = public_visible.x0;
    out_view->visible_public.y0 = public_visible.y0;
    out_view->visible_public.x1 = public_visible.x1;
    out_view->visible_public.y1 = public_visible.y1;
    out_view->rotate_degrees = rotate;
    out_view->user_unit = user_unit;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_crop_resolve_page(
    fz_context *ctx,
    pdf_document *document,
    int page_index,
    extractpdf_pdf_crop_page_view *out_view)
{
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL || out_view == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    memset(out_view, 0, sizeof(*out_view));

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = extractpdf_pdf_crop_resolve_page_imp(
            ctx, document, page_index, out_view);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return status;
}

static int extractpdf_pdf_crop_finite_public_rect(extractpdf_rect rect)
{
    return isfinite(rect.x0) && isfinite(rect.y0) &&
        isfinite(rect.x1) && isfinite(rect.y1);
}

static int extractpdf_pdf_crop_rect_equal(
    extractpdf_rect left,
    extractpdf_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int extractpdf_pdf_crop_public_inside(
    extractpdf_rect inner,
    extractpdf_rect outer)
{
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 &&
        inner.x1 <= outer.x1 && inner.y1 <= outer.y1;
}

extractpdf_status extractpdf_pdf_crop_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_pdf_crop_plan *plans,
    int *out_any_changed)
{
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;
    size_t index;
    const size_t minimum_size =
        offsetof(extractpdf_page_crop, bounds) + sizeof(extractpdf_rect);

    if (out_any_changed != NULL)
        *out_any_changed = 0;
    if (ctx == NULL || document == NULL || crops == NULL ||
        crop_count == 0 || plans == NULL || out_any_changed == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        for (index = 0; index < crop_count && status == EXTRACTPDF_OK; ++index) {
            extractpdf_pdf_crop_page_view view;
            fz_rect public_rect;
            size_t prior;

            if (crops[index].struct_size < minimum_size) {
                status = EXTRACTPDF_ERROR_ARGUMENT;
                break;
            }
            if (!extractpdf_pdf_crop_finite_public_rect(crops[index].bounds) ||
                !(crops[index].bounds.x0 < crops[index].bounds.x1) ||
                !(crops[index].bounds.y0 < crops[index].bounds.y1)) {
                status = EXTRACTPDF_ERROR_ARGUMENT;
                break;
            }
            for (prior = 0; prior < index; ++prior) {
                if (crops[prior].page_index == crops[index].page_index) {
                    status = EXTRACTPDF_ERROR_ARGUMENT;
                    break;
                }
            }
            if (status != EXTRACTPDF_OK)
                break;

            status = extractpdf_pdf_crop_resolve_page_imp(
                ctx, document, crops[index].page_index, &view);
            if (status != EXTRACTPDF_OK)
                break;
            if (!extractpdf_pdf_crop_public_inside(
                    crops[index].bounds, view.visible_public)) {
                status = EXTRACTPDF_ERROR_ARGUMENT;
                break;
            }

            plans[index].page_index = crops[index].page_index;
            plans[index].requested_public = crops[index].bounds;
            plans[index].changed = !extractpdf_pdf_crop_rect_equal(
                crops[index].bounds, view.visible_public);

            public_rect.x0 = crops[index].bounds.x0;
            public_rect.y0 = crops[index].bounds.y0;
            public_rect.x1 = crops[index].bounds.x1;
            public_rect.y1 = crops[index].bounds.y1;
            plans[index].requested_pdf = fz_transform_rect(
                public_rect, view.public_to_pdf);

            if (!isfinite(plans[index].requested_pdf.x0) ||
                !isfinite(plans[index].requested_pdf.y0) ||
                !isfinite(plans[index].requested_pdf.x1) ||
                !isfinite(plans[index].requested_pdf.y1) ||
                plans[index].requested_pdf.x0 < view.visible_pdf.x0 ||
                plans[index].requested_pdf.y0 < view.visible_pdf.y0 ||
                plans[index].requested_pdf.x1 > view.visible_pdf.x1 ||
                plans[index].requested_pdf.y1 > view.visible_pdf.y1) {
                status = EXTRACTPDF_ERROR_ARGUMENT;
                break;
            }

            if (plans[index].changed)
                *out_any_changed = 1;
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return status;
}
