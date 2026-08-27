#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int extractpdf_render_format(
    const extractpdf_render_options *options,
    extractpdf_pixel_format *out_format,
    int *out_alpha)
{
    extractpdf_pixel_format format = EXTRACTPDF_PIXEL_FORMAT_RGB8;

    if (options != NULL) {
        if (options->struct_size < EXTRACTPDF_RENDER_OPTIONS_V1_SIZE)
            return 0;
        format = (extractpdf_pixel_format)options->pixel_format;
    }

    switch (format) {
    case EXTRACTPDF_PIXEL_FORMAT_RGB8:
        *out_format = format;
        *out_alpha = 0;
        return 1;
    case EXTRACTPDF_PIXEL_FORMAT_RGBA8:
        *out_format = format;
        *out_alpha = 1;
        return 1;
    default:
        return 0;
    }
}

extractpdf_status extractpdf_render_page(
    extractpdf_page *page,
    const extractpdf_render_options *options,
    extractpdf_bitmap **out_bitmap)
{
    extractpdf_bitmap *bitmap = NULL;
    extractpdf_pixel_format format;
    fz_pixmap *pixmap = NULL;
    int alpha = 0;
    int caught_code = FZ_ERROR_NONE;
    int width;
    int height;
    int stride;
    int components;
    size_t data_size;

    if (out_bitmap == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (!extractpdf_render_format(options, &format, &alpha))
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(pixmap);
    fz_var(caught_code);

    fz_try(page->document->ctx)
    {
        pixmap = fz_new_pixmap_from_page(
            page->document->ctx,
            page->page,
            fz_identity,
            fz_device_rgb(page->document->ctx),
            alpha);
    }
    fz_catch(page->document->ctx)
    {
        caught_code = fz_caught(page->document->ctx);
        fz_report_error(page->document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (pixmap == NULL)
        return EXTRACTPDF_ERROR_MUPDF;

    width = fz_pixmap_width(page->document->ctx, pixmap);
    height = fz_pixmap_height(page->document->ctx, pixmap);
    stride = fz_pixmap_stride(page->document->ctx, pixmap);
    components = fz_pixmap_components(page->document->ctx, pixmap);

    if (width <= 0 || height <= 0 || stride <= 0 ||
        components != (alpha ? 4 : 3)) {
        fz_drop_pixmap(page->document->ctx, pixmap);
        return EXTRACTPDF_ERROR_MUPDF;
    }

    if ((size_t)height > SIZE_MAX / (size_t)stride) {
        fz_drop_pixmap(page->document->ctx, pixmap);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    data_size = (size_t)height * (size_t)stride;

    bitmap = (extractpdf_bitmap *)calloc(1, sizeof(*bitmap));
    if (bitmap == NULL) {
        fz_drop_pixmap(page->document->ctx, pixmap);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    bitmap->pixels = (unsigned char *)malloc(data_size);
    if (bitmap->pixels == NULL) {
        free(bitmap);
        fz_drop_pixmap(page->document->ctx, pixmap);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    memcpy(
        bitmap->pixels,
        fz_pixmap_samples(page->document->ctx, pixmap),
        data_size);
    fz_drop_pixmap(page->document->ctx, pixmap);

    bitmap->width = width;
    bitmap->height = height;
    bitmap->stride = stride;
    bitmap->pixel_format = format;
    bitmap->data_size = data_size;

    *out_bitmap = bitmap;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_bitmap_get_info(
    const extractpdf_bitmap *bitmap,
    extractpdf_bitmap_info *out_info)
{
    if (bitmap == NULL || out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->width = bitmap->width;
    out_info->height = bitmap->height;
    out_info->stride = bitmap->stride;
    out_info->pixel_format = bitmap->pixel_format;
    out_info->data_size = bitmap->data_size;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_bitmap_get_pixels(
    const extractpdf_bitmap *bitmap,
    const unsigned char **out_pixels,
    size_t *out_size)
{
    if (bitmap == NULL || out_pixels == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_pixels = bitmap->pixels;
    *out_size = bitmap->data_size;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_bitmap(extractpdf_bitmap *bitmap)
{
    if (bitmap == NULL)
        return;

    free(bitmap->pixels);
    free(bitmap);
}
