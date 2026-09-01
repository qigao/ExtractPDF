#include "internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static size_t quantapdf_composer_limit_or_default(
    size_t configured,
    size_t default_value)
{
    return configured == 0u ? default_value : configured;
}

quantapdf_status quantapdf_composer_create(
    const quantapdf_composer_options *options,
    quantapdf_composer **out_composer)
{
    quantapdf_composer *composer;
    size_t max_pages = QUANTAPDF_COMPOSER_DEFAULT_MAX_PAGES;
    size_t max_operations = QUANTAPDF_COMPOSER_DEFAULT_MAX_OPERATIONS;
    size_t max_resource_bytes =
        QUANTAPDF_COMPOSER_DEFAULT_MAX_RESOURCE_BYTES;

    if (out_composer == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_composer = NULL;

    if (options != NULL) {
        if (options->struct_size < QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE)
            return QUANTAPDF_ERROR_ARGUMENT;
        max_pages = quantapdf_composer_limit_or_default(
            options->max_pages, max_pages);
        max_operations = quantapdf_composer_limit_or_default(
            options->max_operations, max_operations);
        max_resource_bytes = quantapdf_composer_limit_or_default(
            options->max_resource_bytes, max_resource_bytes);
    }

    if (max_pages > SIZE_MAX / sizeof(quantapdf_composer_page_state))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    composer = (quantapdf_composer *)calloc(1u, sizeof(*composer));
    if (composer == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->max_pages = max_pages;
    composer->max_operations = max_operations;
    composer->max_resource_bytes = max_resource_bytes;
    *out_composer = composer;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_page(
    quantapdf_composer *composer,
    const quantapdf_composer_page_options *options,
    size_t *out_page_index)
{
    quantapdf_composer_page_state *grown;
    size_t new_capacity;

    if (out_page_index != NULL)
        *out_page_index = SIZE_MAX;
    if (composer == NULL || options == NULL || out_page_index == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_MIN_SIZE ||
        !isfinite(options->width_points) ||
        !isfinite(options->height_points) ||
        options->width_points <= 0.0f || options->height_points <= 0.0f)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer->page_count >= composer->max_pages)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (composer->page_count == composer->page_capacity) {
        new_capacity = composer->page_capacity == 0u
            ? (composer->max_pages < 8u ? composer->max_pages : 8u)
            : composer->page_capacity * 2u;
        if (new_capacity < composer->page_capacity ||
            new_capacity > composer->max_pages)
            new_capacity = composer->max_pages;
        grown = (quantapdf_composer_page_state *)realloc(
            composer->pages, new_capacity * sizeof(*grown));
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        composer->pages = grown;
        composer->page_capacity = new_capacity;
    }

    composer->pages[composer->page_count].width_points =
        options->width_points;
    composer->pages[composer->page_count].height_points =
        options->height_points;
    composer->pages[composer->page_count].background_argb =
        options->background_argb;
    *out_page_index = composer->page_count;
    ++composer->page_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_composer_add_image(
    quantapdf_composer *composer,
    const unsigned char *data,
    size_t size,
    quantapdf_composer_image_id *out_image_id)
{
    (void)composer;
    (void)data;
    (void)size;
    if (out_image_id != NULL)
        *out_image_id = 0u;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_composer_draw_text(
    quantapdf_composer *composer,
    size_t page_index,
    const char *text_utf8,
    const quantapdf_rect *bounds,
    const quantapdf_composer_text_options *options)
{
    (void)composer;
    (void)page_index;
    (void)text_utf8;
    (void)bounds;
    (void)options;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_composer_draw_image(
    quantapdf_composer *composer,
    size_t page_index,
    quantapdf_composer_image_id image_id,
    const quantapdf_rect *bounds,
    const quantapdf_composer_image_options *options)
{
    (void)composer;
    (void)page_index;
    (void)image_id;
    (void)bounds;
    (void)options;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_composer_finish(
    const quantapdf_composer *composer,
    quantapdf_output **out_output)
{
    (void)composer;
    if (out_output != NULL)
        *out_output = NULL;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

void quantapdf_drop_composer(quantapdf_composer *composer)
{
    if (composer == NULL)
        return;
    free(composer->pages);
    free(composer);
}
