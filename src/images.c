#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static void quantapdf_copy_point(quantapdf_point *out, fz_point point)
{
    out->x = point.x;
    out->y = point.y;
}

static void quantapdf_copy_quad(quantapdf_quad *out, fz_quad quad)
{
    quantapdf_copy_point(&out->ul, quad.ul);
    quantapdf_copy_point(&out->ur, quad.ur);
    quantapdf_copy_point(&out->ll, quad.ll);
    quantapdf_copy_point(&out->lr, quad.lr);
}

static void quantapdf_zero_quad(quantapdf_quad *quad)
{
    quad->ul.x = 0.0f;
    quad->ul.y = 0.0f;
    quad->ur.x = 0.0f;
    quad->ur.y = 0.0f;
    quad->ll.x = 0.0f;
    quad->ll.y = 0.0f;
    quad->lr.x = 0.0f;
    quad->lr.y = 0.0f;
}

static void quantapdf_dispose_image_page(quantapdf_image_page *images)
{
    size_t i;

    if (images == NULL)
        return;

    if (images->document != NULL && images->document->ctx != NULL) {
        for (i = 0; i < images->count; ++i)
            fz_drop_image(images->document->ctx, images->items[i].image);
    }

    free(images->items);
    free(images);
}

static int quantapdf_grow_image_page(quantapdf_image_page *images)
{
    quantapdf_image_occurrence_internal *items;
    size_t new_capacity;

    if (images->count < images->capacity)
        return 1;

    if (images->capacity == 0)
        new_capacity = 4;
    else {
        if (images->capacity > SIZE_MAX / 2)
            return 0;
        new_capacity = images->capacity * 2;
    }

    if (new_capacity > SIZE_MAX / sizeof(*images->items))
        return 0;

    items = (quantapdf_image_occurrence_internal *)realloc(
        images->items,
        new_capacity * sizeof(*images->items));
    if (items == NULL)
        return 0;

    images->items = items;
    images->capacity = new_capacity;
    return 1;
}

typedef struct quantapdf_image_capture_device {
    fz_device super;
    quantapdf_image_page *snapshot;
} quantapdf_image_capture_device;

static void quantapdf_capture_fill_image(
    fz_context *ctx,
    fz_device *device,
    fz_image *image,
    fz_matrix ctm,
    float alpha,
    fz_color_params color_params)
{
    quantapdf_image_capture_device *capture =
        (quantapdf_image_capture_device *)device;
    quantapdf_image_page *snapshot = capture->snapshot;
    quantapdf_image_occurrence_internal *item;
    fz_quad quad;

    (void)alpha;
    (void)color_params;

    if (snapshot == NULL || snapshot->oom || image == NULL || image->imagemask)
        return;

    if (!quantapdf_grow_image_page(snapshot)) {
        snapshot->oom = 1;
        return;
    }

    item = &snapshot->items[snapshot->count];
    item->image = fz_keep_image(ctx, image);
    quad = fz_transform_quad(fz_quad_from_rect(fz_unit_rect), ctm);
    quantapdf_copy_quad(&item->quad, quad);
    item->pixel_width = image->w;
    item->pixel_height = image->h;
    item->components = image->n;
    item->bits_per_component = image->bpc;
    item->has_alpha = (image->mask != NULL || image->use_colorkey) ? 1 : 0;
    ++snapshot->count;
}

quantapdf_status quantapdf_extract_images(
    quantapdf_page *page,
    quantapdf_image_page **out_images)
{
    quantapdf_image_page *images;
    quantapdf_image_capture_device *capture = NULL;
    fz_device *device = NULL;
    fz_context *ctx;
    int caught_code = FZ_ERROR_NONE;

    if (out_images == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_images = NULL;

    if (page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    images = (quantapdf_image_page *)calloc(1, sizeof(*images));
    if (images == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    images->document = page->document;
    ctx = page->document->ctx;

    fz_var(device);
    fz_var(capture);
    fz_var(caught_code);

    fz_try(ctx)
    {
        capture = fz_new_derived_device(ctx, quantapdf_image_capture_device);
        capture->snapshot = images;
        capture->super.fill_image = quantapdf_capture_fill_image;
        device = &capture->super;
        fz_run_page_contents(ctx, page->page, device, fz_identity, NULL);
        fz_close_device(ctx, device);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (device != NULL)
        fz_drop_device(ctx, device);

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status status = quantapdf_status_from_backend(caught_code);
        quantapdf_dispose_image_page(images);
        return status;
    }

    if (images->oom) {
        quantapdf_dispose_image_page(images);
        return QUANTAPDF_ERROR_NOMEM;
    }

    *out_images = images;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_image_count(
    const quantapdf_image_page *images,
    size_t *out_count)
{
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;

    if (images == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = images->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_image_get_info(
    const quantapdf_image_page *images,
    size_t index,
    quantapdf_image_info *out_info)
{
    const quantapdf_image_occurrence_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_image_info, has_alpha) +
        sizeof(out_info->has_alpha);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    quantapdf_zero_quad(&out_info->quad);
    out_info->pixel_width = 0;
    out_info->pixel_height = 0;
    out_info->components = 0;
    out_info->bits_per_component = 0;
    out_info->has_alpha = 0;

    if (images == NULL || index >= images->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &images->items[index];
    out_info->quad = item->quad;
    out_info->pixel_width = item->pixel_width;
    out_info->pixel_height = item->pixel_height;
    out_info->components = item->components;
    out_info->bits_per_component = item->bits_per_component;
    out_info->has_alpha = item->has_alpha;
    return QUANTAPDF_OK;
}

void quantapdf_drop_image_page(quantapdf_image_page *images)
{
    quantapdf_dispose_image_page(images);
}
