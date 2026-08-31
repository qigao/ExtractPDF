#include "pdf_outline_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUANTAPDF_OUTLINE_MAX_DEPTH 256u

typedef struct quantapdf_outline_node_internal {
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    size_t title_offset;
    size_t title_size;
    size_t uri_offset;
    size_t uri_size;
    int has_title;
    int is_open;
} quantapdf_outline_node_internal;

struct quantapdf_outline {
    quantapdf_outline_node_internal *nodes;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};

static void quantapdf_dispose_outline(quantapdf_outline *outline)
{
    if (outline == NULL)
        return;
    free(outline->nodes);
    free(outline->strings);
    free(outline);
}

static void quantapdf_init_outline_node(quantapdf_outline_node_internal *node)
{
    memset(node, 0, sizeof(*node));
    node->parent_index = SIZE_MAX;
    node->first_child_index = SIZE_MAX;
    node->next_sibling_index = SIZE_MAX;
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
    node->target_page = -1;
}

static quantapdf_outline *quantapdf_allocate_outline(size_t count)
{
    quantapdf_outline *outline;
    size_t index;

    outline = (quantapdf_outline *)calloc(1, sizeof(*outline));
    if (outline == NULL)
        return NULL;

    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*outline->nodes)) {
            free(outline);
            return NULL;
        }
        outline->nodes = (quantapdf_outline_node_internal *)calloc(
            count, sizeof(*outline->nodes));
        if (outline->nodes == NULL) {
            free(outline);
            return NULL;
        }
        for (index = 0; index < count; ++index)
            quantapdf_init_outline_node(&outline->nodes[index]);
    }

    outline->count = count;
    return outline;
}

static quantapdf_status quantapdf_outline_append_string(
    quantapdf_outline *outline,
    const char *text,
    size_t *out_offset,
    size_t *out_size)
{
    size_t size;
    size_t required;
    size_t capacity;
    char *grown;

    size = strlen(text);
    if (size == SIZE_MAX || outline->string_size > SIZE_MAX - size - 1)
        return QUANTAPDF_ERROR_NOMEM;

    required = outline->string_size + size + 1;
    if (required > outline->string_capacity) {
        capacity = outline->string_capacity != 0 ?
            outline->string_capacity : 64;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        if (capacity < required)
            return QUANTAPDF_ERROR_NOMEM;

        grown = (char *)realloc(outline->strings, capacity);
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        outline->strings = grown;
        outline->string_capacity = capacity;
    }

    *out_offset = outline->string_size;
    *out_size = size;
    memcpy(outline->strings + outline->string_size, text, size + 1);
    outline->string_size = required;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_decode_outline_item(
    fz_context *ctx,
    fz_document *document,
    int page_count,
    quantapdf_outline *outline,
    size_t index,
    size_t parent_index,
    size_t previous_sibling_index,
    const fz_outline_item *item)
{
    quantapdf_outline_node_internal *node = &outline->nodes[index];
    quantapdf_status status;

    node->parent_index = parent_index;
    if (parent_index != SIZE_MAX &&
        outline->nodes[parent_index].first_child_index == SIZE_MAX)
        outline->nodes[parent_index].first_child_index = index;
    if (previous_sibling_index != SIZE_MAX)
        outline->nodes[previous_sibling_index].next_sibling_index = index;

    node->is_open = item->is_open != 0;

    if (item->title != NULL) {
        status = quantapdf_outline_append_string(
            outline, item->title, &node->title_offset, &node->title_size);
        if (status != QUANTAPDF_OK)
            return status;
        node->has_title = 1;
    }

    if (item->uri == NULL) {
        node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
        return QUANTAPDF_OK;
    }

    if (fz_is_external_link(ctx, item->uri)) {
        node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
        return quantapdf_outline_append_string(
            outline, item->uri, &node->uri_offset, &node->uri_size);
    }

    {
        fz_link_dest destination = fz_resolve_link_dest(ctx, document, item->uri);
        int page;

        if (destination.loc.chapter < 0 || destination.loc.page < 0)
            return QUANTAPDF_ERROR_FORMAT;

        page = fz_page_number_from_location(ctx, document, destination.loc);
        if (page < 0 || page >= page_count)
            return QUANTAPDF_ERROR_FORMAT;

        node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_INTERNAL;
        node->target_page = page;
        node->target.x = destination.x;
        node->target.y = destination.y;
    }

    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_flatten_pdf_outline(
    fz_context *ctx,
    fz_document *document,
    quantapdf_outline *outline)
{
    fz_outline_iterator *iter = NULL;
    size_t *parent_stack;
    size_t stack_count = 0;
    size_t index = 0;
    size_t parent_index = SIZE_MAX;
    size_t previous_sibling_index = SIZE_MAX;
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status = QUANTAPDF_OK;

    parent_stack = (size_t *)malloc(
        QUANTAPDF_OUTLINE_MAX_DEPTH * sizeof(*parent_stack));
    if (parent_stack == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    fz_var(iter);
    fz_var(index);
    fz_var(stack_count);
    fz_var(parent_index);
    fz_var(previous_sibling_index);
    fz_var(page_count);
    fz_var(caught_code);
    fz_var(status);

    fz_try(ctx)
    {
        page_count = fz_count_pages(ctx, document);
        iter = fz_new_outline_iterator(ctx, document);
        if (iter == NULL)
            status = QUANTAPDF_ERROR_FORMAT;

        while (status == QUANTAPDF_OK && index < outline->count) {
            fz_outline_item *item;
            size_t current_index = index;
            size_t cursor_index = current_index;
            int move;
            int exhausted = 0;

            item = fz_outline_iterator_item(ctx, iter);
            if (item == NULL) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }

            status = quantapdf_decode_outline_item(
                ctx,
                document,
                page_count,
                outline,
                current_index,
                parent_index,
                previous_sibling_index,
                item);
            if (status != QUANTAPDF_OK)
                break;

            move = fz_outline_iterator_down(ctx, iter);
            if (move == FZ_OUTLINE_ITERATOR_AT_ITEM) {
                if (stack_count >= QUANTAPDF_OUTLINE_MAX_DEPTH) {
                    status = QUANTAPDF_ERROR_UNSUPPORTED;
                    break;
                }
                parent_stack[stack_count++] = parent_index;
                parent_index = current_index;
                previous_sibling_index = SIZE_MAX;
                ++index;
                continue;
            }
            if (move == FZ_OUTLINE_ITERATOR_AT_EMPTY) {
                if (fz_outline_iterator_up(ctx, iter) !=
                    FZ_OUTLINE_ITERATOR_AT_ITEM) {
                    status = QUANTAPDF_ERROR_FORMAT;
                    break;
                }
            } else {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }

            while (status == QUANTAPDF_OK) {
                move = fz_outline_iterator_next(ctx, iter);
                if (move == FZ_OUTLINE_ITERATOR_AT_ITEM) {
                    previous_sibling_index = cursor_index;
                    ++index;
                    break;
                }
                if (move == FZ_OUTLINE_ITERATOR_AT_EMPTY) {
                    if (stack_count == 0) {
                        ++index;
                        exhausted = 1;
                        break;
                    }
                    if (fz_outline_iterator_up(ctx, iter) !=
                        FZ_OUTLINE_ITERATOR_AT_ITEM) {
                        status = QUANTAPDF_ERROR_FORMAT;
                        break;
                    }
                    cursor_index = parent_index;
                    parent_index = parent_stack[--stack_count];
                    continue;
                }

                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }

            if (exhausted)
                break;
        }

        if (status == QUANTAPDF_OK && index != outline->count)
            status = QUANTAPDF_ERROR_FORMAT;
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (iter != NULL)
        fz_drop_outline_iterator(ctx, iter);
    free(parent_stack);

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    return status;
}

quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline)
{
    fz_context *ctx;
    pdf_document *pdf = NULL;
    quantapdf_outline *outline;
    quantapdf_status preflight_status = QUANTAPDF_OK;
    quantapdf_status flatten_status;
    size_t count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_outline == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_outline = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    ctx = document->ctx;
    fz_var(pdf);
    fz_var(preflight_status);
    fz_var(count);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, document->doc);
        if (pdf != NULL)
            preflight_status = quantapdf_pdf_outline_walk_strict(
                ctx, pdf, NULL, NULL, &count);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    if (pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (preflight_status != QUANTAPDF_OK)
        return preflight_status;

    outline = quantapdf_allocate_outline(count);
    if (outline == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (count != 0) {
        flatten_status = quantapdf_flatten_pdf_outline(
            ctx, document->doc, outline);
        if (flatten_status != QUANTAPDF_OK) {
            quantapdf_dispose_outline(outline);
            return flatten_status;
        }
    }

    *out_outline = outline;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_count(
    const quantapdf_outline *outline,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;

    if (outline == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = outline->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_get_info(
    const quantapdf_outline *outline,
    size_t index,
    quantapdf_outline_info *out_info)
{
    const quantapdf_outline_node_internal *node;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_outline_info, is_open) +
        sizeof(out_info->is_open);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->parent_index = SIZE_MAX;
    out_info->first_child_index = SIZE_MAX;
    out_info->next_sibling_index = SIZE_MAX;
    out_info->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
    out_info->target_page = -1;
    out_info->target.x = 0.0f;
    out_info->target.y = 0.0f;
    out_info->is_open = 0;

    if (outline == NULL || index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    out_info->parent_index = node->parent_index;
    out_info->first_child_index = node->first_child_index;
    out_info->next_sibling_index = node->next_sibling_index;
    out_info->destination_kind = node->destination_kind;
    out_info->target_page = node->target_page;
    out_info->target = node->target;
    out_info->is_open = node->is_open;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    if (!node->has_title)
        return QUANTAPDF_OK;

    *out_utf8 = outline->strings + node->title_offset;
    *out_size = node->title_size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_outline_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    if (node->destination_kind != QUANTAPDF_OUTLINE_DESTINATION_URI)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_utf8 = outline->strings + node->uri_offset;
    *out_size = node->uri_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_outline(quantapdf_outline *outline)
{
    quantapdf_dispose_outline(outline);
}
