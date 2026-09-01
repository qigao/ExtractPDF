#include "internal.h"
#include "backend/qpdf_composer.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int quantapdf_composer_rect_valid(const quantapdf_rect *rect)
{
    return rect != NULL &&
        isfinite(rect->x0) && isfinite(rect->y0) &&
        isfinite(rect->x1) && isfinite(rect->y1) &&
        rect->x1 > rect->x0 && rect->y1 > rect->y0;
}

static int quantapdf_composer_utf8_is_winansi(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    while (*cursor != 0u) {
        uint32_t codepoint;
        size_t count;
        size_t i;

        if (*cursor < 0x80u) {
            codepoint = *cursor;
            count = 1u;
        } else if (*cursor >= 0xc2u && *cursor <= 0xdfu) {
            codepoint = (uint32_t)(*cursor & 0x1fu);
            count = 2u;
        } else if (*cursor >= 0xe0u && *cursor <= 0xefu) {
            codepoint = (uint32_t)(*cursor & 0x0fu);
            count = 3u;
        } else {
            return 0;
        }
        for (i = 1u; i < count; ++i) {
            if (cursor[i] == 0u || (cursor[i] & 0xc0u) != 0x80u)
                return 0;
            codepoint = (codepoint << 6u) | (uint32_t)(cursor[i] & 0x3fu);
        }
        if ((count == 3u && codepoint < 0x800u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return 0;
        if (!((codepoint >= 0x20u && codepoint <= 0x7eu) ||
              (codepoint >= 0xa0u && codepoint <= 0xffu) ||
              codepoint == '\n' || codepoint == '\r' ||
              codepoint == '\t' || codepoint == 0x20acu ||
              (codepoint >= 0x2013u && codepoint <= 0x2026u) ||
              codepoint == 0x0192u || codepoint == 0x02c6u ||
              codepoint == 0x02dcu || codepoint == 0x0152u ||
              codepoint == 0x0153u || codepoint == 0x0160u ||
              codepoint == 0x0161u || codepoint == 0x0178u ||
              codepoint == 0x017du || codepoint == 0x017eu ||
              codepoint == 0x2030u || codepoint == 0x2039u ||
              codepoint == 0x203au || codepoint == 0x2122u))
            return 0;
        cursor += count;
    }
    return 1;
}

static quantapdf_status quantapdf_composer_reserve_operation(
    quantapdf_composer *composer)
{
    quantapdf_composer_operation *grown;
    size_t new_capacity;

    if (composer->operation_count >= composer->max_operations)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->operation_count < composer->operation_capacity)
        return QUANTAPDF_OK;

    new_capacity = composer->operation_capacity == 0u
        ? (composer->max_operations < 16u ? composer->max_operations : 16u)
        : composer->operation_capacity * 2u;
    if (new_capacity < composer->operation_capacity ||
        new_capacity > composer->max_operations)
        new_capacity = composer->max_operations;
    if (new_capacity > SIZE_MAX / sizeof(*grown))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    grown = (quantapdf_composer_operation *)realloc(
        composer->operations, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    composer->operations = grown;
    composer->operation_capacity = new_capacity;
    return QUANTAPDF_OK;
}

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
    quantapdf_composer_operation operation;
    quantapdf_status status;
    size_t text_size;

    if (composer == NULL || text_utf8 == NULL || options == NULL ||
        options->struct_size < QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_MIN_SIZE ||
        page_index >= composer->page_count ||
        !quantapdf_composer_rect_valid(bounds) ||
        options->font < QUANTAPDF_COMPOSER_FONT_HELVETICA ||
        options->font > QUANTAPDF_COMPOSER_FONT_COURIER_BOLD_OBLIQUE ||
        !isfinite(options->font_size) || options->font_size <= 0.0f ||
        !isfinite(options->line_height_multiplier) ||
        options->line_height_multiplier <= 0.0f ||
        options->alignment < QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT ||
        options->alignment > QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT ||
        (options->argb >> 24u) != 0xffu)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!quantapdf_composer_utf8_is_winansi(text_utf8))
        return QUANTAPDF_ERROR_FORMAT;

    text_size = strlen(text_utf8) + 1u;
    if (text_size > composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = quantapdf_composer_reserve_operation(composer);
    if (status != QUANTAPDF_OK)
        return status;

    memset(&operation, 0, sizeof(operation));
    operation.value.text.text_utf8 = (char *)malloc(text_size);
    if (operation.value.text.text_utf8 == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(operation.value.text.text_utf8, text_utf8, text_size);
    operation.kind = QUANTAPDF_COMPOSER_OPERATION_TEXT;
    operation.page_index = page_index;
    operation.bounds = *bounds;
    operation.value.text.options = *options;
    composer->operations[composer->operation_count] = operation;
    ++composer->operation_count;
    composer->resource_bytes += text_size;
    return QUANTAPDF_OK;
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
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (composer == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (composer->page_count == 0u)
        return QUANTAPDF_ERROR_STATE;

    output = (quantapdf_output *)calloc(1u, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    status = quantapdf_qpdf_compose(
        composer, &output->data, &output->size);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}

void quantapdf_drop_composer(quantapdf_composer *composer)
{
    size_t i;

    if (composer == NULL)
        return;
    for (i = 0u; i < composer->operation_count; ++i) {
        if (composer->operations[i].kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
            free(composer->operations[i].value.text.text_utf8);
    }
    free(composer->operations);
    free(composer->pages);
    free(composer);
}
