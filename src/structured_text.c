#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void extractpdf_copy_rect(extractpdf_rect *out, fz_rect in)
{
    out->x0 = in.x0;
    out->y0 = in.y0;
    out->x1 = in.x1;
    out->y1 = in.y1;
}

static void extractpdf_union_rect(extractpdf_rect *dst, fz_rect in)
{
    if (in.x0 < dst->x0)
        dst->x0 = in.x0;
    if (in.y0 < dst->y0)
        dst->y0 = in.y0;
    if (in.x1 > dst->x1)
        dst->x1 = in.x1;
    if (in.y1 > dst->y1)
        dst->y1 = in.y1;
}

static void *extractpdf_calloc_array(size_t count, size_t element_size)
{
    if (count == 0)
        return NULL;
    if (element_size != 0 && count > SIZE_MAX / element_size)
        return NULL;
    return calloc(count, element_size);
}

static int extractpdf_increment_size(size_t *value)
{
    if (*value == SIZE_MAX)
        return 0;
    ++*value;
    return 1;
}

static size_t extractpdf_utf8_encode(int value, char out[4])
{
    uint32_t rune;

    if (value < 0 || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        rune = UINT32_C(0xfffd);
    else
        rune = (uint32_t)value;

    if (rune <= UINT32_C(0x7f)) {
        out[0] = (char)rune;
        return 1;
    }
    if (rune <= UINT32_C(0x7ff)) {
        out[0] = (char)(UINT32_C(0xc0) | (rune >> 6));
        out[1] = (char)(UINT32_C(0x80) | (rune & UINT32_C(0x3f)));
        return 2;
    }
    if (rune <= UINT32_C(0xffff)) {
        out[0] = (char)(UINT32_C(0xe0) | (rune >> 12));
        out[1] = (char)(UINT32_C(0x80) | ((rune >> 6) & UINT32_C(0x3f)));
        out[2] = (char)(UINT32_C(0x80) | (rune & UINT32_C(0x3f)));
        return 3;
    }

    out[0] = (char)(UINT32_C(0xf0) | (rune >> 18));
    out[1] = (char)(UINT32_C(0x80) | ((rune >> 12) & UINT32_C(0x3f)));
    out[2] = (char)(UINT32_C(0x80) | ((rune >> 6) & UINT32_C(0x3f)));
    out[3] = (char)(UINT32_C(0x80) | (rune & UINT32_C(0x3f)));
    return 4;
}

static uint32_t extractpdf_normalize_codepoint(int value)
{
    if (value < 0 || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        return UINT32_C(0xfffd);
    return (uint32_t)value;
}

static void extractpdf_dispose_text_page(extractpdf_text_page *text)
{
    if (text == NULL)
        return;

    free(text->blocks);
    free(text->lines);
    free(text->spans);
    free(text->chars);
    free(text->strings);
    free(text);
}

static int extractpdf_count_structured_text(
    const fz_stext_page *source,
    size_t *out_blocks,
    size_t *out_lines,
    size_t *out_chars)
{
    const fz_stext_block *block;
    size_t blocks = 0;
    size_t lines = 0;
    size_t chars = 0;

    for (block = source->first_block; block != NULL; block = block->next) {
        const fz_stext_line *line;

        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;
        if (!extractpdf_increment_size(&blocks))
            return 0;

        for (line = block->u.t.first_line; line != NULL; line = line->next) {
            const fz_stext_char *ch;

            if (!extractpdf_increment_size(&lines))
                return 0;
            for (ch = line->first_char; ch != NULL; ch = ch->next) {
                if (!extractpdf_increment_size(&chars))
                    return 0;
            }
        }
    }

    *out_blocks = blocks;
    *out_lines = lines;
    *out_chars = chars;
    return 1;
}

static extractpdf_text_page *extractpdf_allocate_text_page(
    size_t block_count,
    size_t line_count,
    size_t char_count)
{
    extractpdf_text_page *text;
    size_t string_capacity;

    if (char_count > (SIZE_MAX - 1) / 5)
        return NULL;
    string_capacity = char_count * 5 + 1;

    text = (extractpdf_text_page *)calloc(1, sizeof(*text));
    if (text == NULL)
        return NULL;

    text->blocks = (extractpdf_text_block_internal *)extractpdf_calloc_array(
        block_count, sizeof(*text->blocks));
    text->lines = (extractpdf_text_line_internal *)extractpdf_calloc_array(
        line_count, sizeof(*text->lines));
    text->spans = (extractpdf_text_span_internal *)extractpdf_calloc_array(
        char_count, sizeof(*text->spans));
    text->chars = (extractpdf_text_char_internal *)extractpdf_calloc_array(
        char_count, sizeof(*text->chars));
    text->strings = (char *)malloc(string_capacity);

    if ((block_count != 0 && text->blocks == NULL) ||
        (line_count != 0 && text->lines == NULL) ||
        (char_count != 0 && (text->spans == NULL || text->chars == NULL)) ||
        text->strings == NULL) {
        extractpdf_dispose_text_page(text);
        return NULL;
    }

    text->block_count = block_count;
    text->line_count = line_count;
    text->char_count = char_count;
    text->strings[0] = '\0';
    return text;
}

static int extractpdf_same_style(
    const fz_stext_char *ch,
    fz_font *font,
    float size,
    uint32_t argb,
    uint16_t bidi,
    uint16_t flags)
{
    return ch->font == font &&
        ch->size == size &&
        ch->argb == argb &&
        ch->bidi == bidi &&
        ch->flags == flags;
}

static void extractpdf_project_structured_text(
    extractpdf_text_page *text,
    const fz_stext_page *source)
{
    const fz_stext_block *source_block;
    size_t block_index = 0;
    size_t line_index = 0;
    size_t span_index = 0;
    size_t char_index = 0;
    size_t string_pos = 0;

    for (source_block = source->first_block;
         source_block != NULL;
         source_block = source_block->next) {
        const fz_stext_line *source_line;
        extractpdf_text_block_internal *block;

        if (source_block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        block = &text->blocks[block_index++];
        extractpdf_copy_rect(&block->bounds, source_block->bbox);
        block->first_line = line_index;

        for (source_line = source_block->u.t.first_line;
             source_line != NULL;
             source_line = source_line->next) {
            const fz_stext_char *source_char;
            extractpdf_text_line_internal *line = &text->lines[line_index++];
            fz_font *style_font = NULL;
            float style_size = 0.0f;
            uint32_t style_argb = 0;
            uint16_t style_bidi = 0;
            uint16_t style_flags = 0;
            size_t current_span_index = 0;
            int have_span = 0;

            extractpdf_copy_rect(&line->bounds, source_line->bbox);
            line->direction_x = source_line->dir.x;
            line->direction_y = source_line->dir.y;
            line->writing_mode = source_line->wmode;
            line->first_span = span_index;

            for (source_char = source_line->first_char;
                 source_char != NULL;
                 source_char = source_char->next) {
                extractpdf_text_span_internal *span;
                extractpdf_text_char_internal *ch;
                fz_rect char_bounds;
                char encoded[4];
                size_t encoded_size;

                if (!have_span ||
                    !extractpdf_same_style(
                        source_char,
                        style_font,
                        style_size,
                        style_argb,
                        style_bidi,
                        style_flags)) {
                    if (have_span)
                        text->strings[string_pos++] = '\0';

                    current_span_index = span_index++;
                    span = &text->spans[current_span_index];
                    char_bounds = fz_rect_from_quad(source_char->quad);
                    extractpdf_copy_rect(&span->bounds, char_bounds);
                    span->font_size = source_char->size;
                    span->argb = source_char->argb;
                    span->bidi_level = source_char->bidi;
                    span->flags = source_char->flags;
                    span->first_char = char_index;
                    span->text_offset = string_pos;

                    style_font = source_char->font;
                    style_size = source_char->size;
                    style_argb = source_char->argb;
                    style_bidi = source_char->bidi;
                    style_flags = source_char->flags;
                    have_span = 1;
                }
                else {
                    span = &text->spans[current_span_index];
                    char_bounds = fz_rect_from_quad(source_char->quad);
                    extractpdf_union_rect(&span->bounds, char_bounds);
                }

                span = &text->spans[current_span_index];
                ch = &text->chars[char_index];
                ch->codepoint = extractpdf_normalize_codepoint(source_char->c);
                ch->bidi = source_char->bidi;
                ch->flags = source_char->flags;
                ch->quad = source_char->quad;
                ch->span_index = current_span_index;

                encoded_size = extractpdf_utf8_encode(source_char->c, encoded);
                memcpy(text->strings + string_pos, encoded, encoded_size);
                string_pos += encoded_size;
                span->text_size += encoded_size;
                ++span->char_count;
                ++char_index;
            }

            if (have_span)
                text->strings[string_pos++] = '\0';
            line->span_count = span_index - line->first_span;
        }

        block->line_count = line_index - block->first_line;
    }

    text->span_count = span_index;
    text->char_count = char_index;
    text->string_size = string_pos;
    if (string_pos == 0)
        text->strings[0] = '\0';
}

extractpdf_status extractpdf_extract_structured_text(
    extractpdf_page *page,
    extractpdf_text_page **out_text)
{
    fz_context *ctx;
    fz_stext_page *source = NULL;
    extractpdf_text_page *text = NULL;
    size_t block_count = 0;
    size_t line_count = 0;
    size_t char_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_text == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_text = NULL;

    if (page == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    ctx = page->document->ctx;
    fz_var(source);
    fz_var(caught_code);

    fz_try(ctx)
    {
        source = fz_new_stext_page_from_page(ctx, page->page, NULL);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (source == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    if (!extractpdf_count_structured_text(
            source, &block_count, &line_count, &char_count)) {
        fz_drop_stext_page(ctx, source);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    text = extractpdf_allocate_text_page(block_count, line_count, char_count);
    if (text == NULL) {
        fz_drop_stext_page(ctx, source);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    extractpdf_project_structured_text(text, source);
    fz_drop_stext_page(ctx, source);

    *out_text = text;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_text_block_count(
    const extractpdf_text_page *text,
    size_t *out_count)
{
    if (out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_count = 0;
    if (text == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_count = text->block_count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_text_get_block_info(
    const extractpdf_text_page *text,
    size_t block_index,
    extractpdf_text_block_info *out_info)
{
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_text_block_info, bounds) +
        sizeof(out_info->bounds);
    if (out_info->struct_size < minimum_size ||
        text == NULL || block_index >= text->block_count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->bounds = text->blocks[block_index].bounds;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_text_line_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t *out_count)
{
    if (out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_count = 0;
    if (text == NULL || block_index >= text->block_count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_count = text->blocks[block_index].line_count;
    return EXTRACTPDF_OK;
}

static const extractpdf_text_line_internal *extractpdf_lookup_line(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index)
{
    const extractpdf_text_block_internal *block;

    if (text == NULL || block_index >= text->block_count)
        return NULL;
    block = &text->blocks[block_index];
    if (line_index >= block->line_count)
        return NULL;
    return &text->lines[block->first_line + line_index];
}

extractpdf_status extractpdf_text_get_line_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    extractpdf_text_line_info *out_info)
{
    const extractpdf_text_line_internal *line;
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_text_line_info, writing_mode) +
        sizeof(out_info->writing_mode);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    line = extractpdf_lookup_line(text, block_index, line_index);
    if (line == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->bounds = line->bounds;
    out_info->direction_x = line->direction_x;
    out_info->direction_y = line->direction_y;
    out_info->writing_mode = line->writing_mode;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_text_span_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count)
{
    const extractpdf_text_line_internal *line;

    if (out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_count = 0;

    line = extractpdf_lookup_line(text, block_index, line_index);
    if (line == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_count = line->span_count;
    return EXTRACTPDF_OK;
}

static const extractpdf_text_span_internal *extractpdf_lookup_span(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index)
{
    const extractpdf_text_line_internal *line;

    line = extractpdf_lookup_line(text, block_index, line_index);
    if (line == NULL || span_index >= line->span_count)
        return NULL;
    return &text->spans[line->first_span + span_index];
}

extractpdf_status extractpdf_text_get_span_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    extractpdf_text_span_info *out_info)
{
    const extractpdf_text_span_internal *span;
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_text_span_info, bidi_level) +
        sizeof(out_info->bidi_level);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    span = extractpdf_lookup_span(text, block_index, line_index, span_index);
    if (span == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->bounds = span->bounds;
    out_info->font_size = span->font_size;
    out_info->argb = span->argb;
    out_info->bidi_level = span->bidi_level;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_text_span_text(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size)
{
    const extractpdf_text_span_internal *span;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (out_utf8 == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    span = extractpdf_lookup_span(text, block_index, line_index, span_index);
    if (span == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_utf8 = text->strings + span->text_offset;
    *out_size = span->text_size;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_text_page(extractpdf_text_page *text)
{
    extractpdf_dispose_text_page(text);
}
