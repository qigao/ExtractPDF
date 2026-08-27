#include "internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static extractpdf_status extractpdf_render_page_at_dpi(
    extractpdf_page *page,
    float dpi,
    extractpdf_bitmap **out_bitmap)
{
    extractpdf_bitmap *bitmap;
    fz_matrix transform;
    int caught_code = FZ_ERROR_NONE;

    if (out_bitmap == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || !isfinite(dpi) || dpi <= 0.0f)
        return EXTRACTPDF_ERROR_ARGUMENT;

    bitmap = (extractpdf_bitmap *)calloc(1, sizeof(*bitmap));
    if (bitmap == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    bitmap->document = page->document;
    transform = fz_scale(dpi / 72.0f, dpi / 72.0f);
    fz_var(caught_code);

    fz_try(page->document->ctx)
    {
        bitmap->pixmap = fz_new_pixmap_from_page(
            page->document->ctx,
            page->page,
            transform,
            fz_device_rgb(page->document->ctx),
            0);
    }
    fz_catch(page->document->ctx)
    {
        caught_code = fz_caught(page->document->ctx);
        fz_report_error(page->document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        free(bitmap);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (bitmap->pixmap == NULL) {
        free(bitmap);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    *out_bitmap = bitmap;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_render_page(
    extractpdf_page *page,
    extractpdf_bitmap **out_bitmap)
{
    return extractpdf_render_page_at_dpi(page, 72.0f, out_bitmap);
}

extractpdf_status extractpdf_render_page_with_options(
    extractpdf_page *page,
    const extractpdf_render_options *options,
    extractpdf_bitmap **out_bitmap)
{
    size_t minimum_size;

    if (out_bitmap == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || options == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_render_options, dpi) + sizeof(options->dpi);
    if (options->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return extractpdf_render_page_at_dpi(page, options->dpi, out_bitmap);
}

extractpdf_status extractpdf_bitmap_dimensions(
    extractpdf_bitmap *bitmap,
    int *out_width,
    int *out_height,
    int *out_stride,
    int *out_components)
{
    fz_context *ctx;

    if (bitmap == NULL || out_width == NULL || out_height == NULL ||
        out_stride == NULL || out_components == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    ctx = bitmap->document->ctx;
    *out_width = fz_pixmap_width(ctx, bitmap->pixmap);
    *out_height = fz_pixmap_height(ctx, bitmap->pixmap);
    *out_stride = fz_pixmap_stride(ctx, bitmap->pixmap);
    *out_components = fz_pixmap_components(ctx, bitmap->pixmap);
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_bitmap_data(
    extractpdf_bitmap *bitmap,
    const unsigned char **out_data,
    size_t *out_size)
{
    int stride;
    int height;
    size_t row_bytes;
    fz_context *ctx;

    if (bitmap == NULL || out_data == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    ctx = bitmap->document->ctx;
    stride = fz_pixmap_stride(ctx, bitmap->pixmap);
    height = fz_pixmap_height(ctx, bitmap->pixmap);
    row_bytes = stride < 0 ? (size_t)(-(long long)stride) : (size_t)stride;

    *out_data = fz_pixmap_samples(ctx, bitmap->pixmap);
    *out_size = row_bytes * (size_t)height;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_bitmap(extractpdf_bitmap *bitmap)
{
    if (bitmap == NULL)
        return;

    if (bitmap->pixmap != NULL)
        fz_drop_pixmap(bitmap->document->ctx, bitmap->pixmap);
    free(bitmap);
}
