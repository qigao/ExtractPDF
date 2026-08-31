#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_image_render(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_bitmap **out_bitmap)
{
    const quantapdf_image_occurrence_internal *occurrence;
    quantapdf_bitmap *bitmap;
    fz_context *ctx;
    fz_pixmap *color = NULL;
    fz_pixmap *rgb = NULL;
    fz_pixmap *mask = NULL;
    fz_pixmap *result = NULL;
    int caught_code = FZ_ERROR_NONE;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (images == NULL || images->document == NULL ||
        images->document->ctx == NULL || index >= images->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    occurrence = &images->items[index];
    if (occurrence->image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    bitmap = (quantapdf_bitmap *)calloc(1, sizeof(*bitmap));
    if (bitmap == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    bitmap->document = images->document;
    ctx = images->document->ctx;

    fz_var(color);
    fz_var(rgb);
    fz_var(mask);
    fz_var(result);
    fz_var(caught_code);

    fz_try(ctx)
    {
        color = fz_get_unscaled_pixmap_from_image(ctx, occurrence->image);

        if (occurrence->image->mask != NULL) {
            /*
             * Keep the public bitmap contract unambiguous: normalize the
             * color plane first, then apply MuPDF's retained image mask.
             * fz_new_pixmap_from_color_and_mask produces premultiplied alpha.
             */
            rgb = fz_convert_pixmap(
                ctx,
                color,
                fz_device_rgb(ctx),
                NULL,
                NULL,
                fz_default_color_params,
                0);
            mask = fz_get_unscaled_pixmap_from_image(
                ctx,
                occurrence->image->mask);
            result = fz_new_pixmap_from_color_and_mask(ctx, rgb, mask);
        }
        else {
            /* Preserve any alpha synthesized by color-key transparency. */
            result = fz_convert_pixmap(
                ctx,
                color,
                fz_device_rgb(ctx),
                NULL,
                NULL,
                fz_default_color_params,
                1);
        }
    }
    fz_always(ctx)
    {
        fz_drop_pixmap(ctx, mask);
        fz_drop_pixmap(ctx, rgb);
        fz_drop_pixmap(ctx, color);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        fz_drop_pixmap(ctx, result);
        free(bitmap);
        return quantapdf_status_from_mupdf(caught_code);
    }

    if (result == NULL) {
        free(bitmap);
        return QUANTAPDF_ERROR_NOMEM;
    }

    bitmap->pixmap = result;
    *out_bitmap = bitmap;
    return QUANTAPDF_OK;
}
