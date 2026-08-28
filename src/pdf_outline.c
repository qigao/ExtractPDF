#include "pdf_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXTRACTPDF_OUTLINE_MAX_DEPTH 256u

typedef struct extractpdf_outline_node_internal {
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    extractpdf_outline_destination_kind destination_kind;
    int target_page;
    extractpdf_point target;
    size_t title_offset;
    size_t title_size;
    size_t uri_offset;
    size_t uri_size;
    int has_title;
    int is_open;
} extractpdf_outline_node_internal;

struct extractpdf_outline {
    extractpdf_outline_node_internal *nodes;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};

typedef struct extractpdf_outline_preflight_frame {
    pdf_obj *parent;
    pdf_obj *node;
    pdf_obj *expected_prev;
    size_t depth;
} extractpdf_outline_preflight_frame;

typedef struct extractpdf_outline_preflight {
    extractpdf_outline_preflight_frame *stack;
    size_t stack_count;
    size_t stack_capacity;
    size_t node_count;
    int too_deep;
} extractpdf_outline_preflight;

static void extractpdf_dispose_outline(extractpdf_outline *outline)
{
    if (outline == NULL)
        return;
    free(outline->nodes);
    free(outline->strings);
    free(outline);
}

static void extractpdf_init_outline_node(extractpdf_outline_node_internal *node)
{
    memset(node, 0, sizeof(*node));
    node->parent_index = SIZE_MAX;
    node->first_child_index = SIZE_MAX;
    node->next_sibling_index = SIZE_MAX;
    node->destination_kind = EXTRACTPDF_OUTLINE_DESTINATION_NONE;
    node->target_page = -1;
}

static extractpdf_outline *extractpdf_allocate_outline(size_t count)
{
    extractpdf_outline *outline;
    size_t index;

    outline = (extractpdf_outline *)calloc(1, sizeof(*outline));
    if (outline == NULL)
        return NULL;

    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*outline->nodes)) {
            free(outline);
            return NULL;
        }
        outline->nodes = (extractpdf_outline_node_internal *)calloc(
            count, sizeof(*outline->nodes));
        if (outline->nodes == NULL) {
            free(outline);
            return NULL;
        }
        for (index = 0; index < count; ++index)
            extractpdf_init_outline_node(&outline->nodes[index]);
    }

    outline->count = count;
    return outline;
}

static extractpdf_status extractpdf_outline_append_string(
    extractpdf_outline *outline,
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
        return EXTRACTPDF_ERROR_NOMEM;

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
            return EXTRACTPDF_ERROR_NOMEM;

        grown = (char *)realloc(outline->strings, capacity);
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        outline->strings = grown;
        outline->string_capacity = capacity;
    }

    *out_offset = outline->string_size;
    *out_size = size;
    memcpy(outline->strings + outline->string_size, text, size + 1);
    outline->string_size = required;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_preflight_push(
    extractpdf_outline_preflight *preflight,
    const extractpdf_outline_preflight_frame *frame)
{
    size_t capacity;
    extractpdf_outline_preflight_frame *grown;

    if (preflight->stack_count == preflight->stack_capacity) {
        if (preflight->stack_capacity == 0) {
            capacity = 32;
        } else {
            if (preflight->stack_capacity > SIZE_MAX / 2)
                return EXTRACTPDF_ERROR_NOMEM;
            capacity = preflight->stack_capacity * 2;
        }
        if (capacity > SIZE_MAX / sizeof(*preflight->stack))
            return EXTRACTPDF_ERROR_NOMEM;

        grown = (extractpdf_outline_preflight_frame *)realloc(
            preflight->stack, capacity * sizeof(*preflight->stack));
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        preflight->stack = grown;
        preflight->stack_capacity = capacity;
    }

    preflight->stack[preflight->stack_count++] = *frame;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_preflight_pdf_outline(
    fz_context *ctx,
    pdf_document *pdf,
    size_t *out_count)
{
    pdf_obj *trailer;
    pdf_obj *root;
    pdf_obj *outlines;
    pdf_obj *first;
    pdf_obj *last;
    pdf_mark_bits *marks = NULL;
    extractpdf_outline_preflight *preflight;
    extractpdf_outline_preflight_frame frame;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;
    size_t node_count;
    int too_deep;

    *out_count = 0;

    preflight = (extractpdf_outline_preflight *)calloc(1, sizeof(*preflight));
    if (preflight == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    fz_var(marks);
    fz_var(status);
    fz_var(caught_code);

    fz_try(ctx)
    {
        trailer = pdf_trailer(ctx, pdf);
        root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        if (!pdf_is_dict(ctx, root)) {
            status = EXTRACTPDF_ERROR_FORMAT;
        } else {
            outlines = pdf_dict_get(ctx, root, PDF_NAME(Outlines));
            if (outlines == NULL) {
                status = EXTRACTPDF_OK;
            } else if (!pdf_is_dict(ctx, outlines)) {
                status = EXTRACTPDF_ERROR_FORMAT;
            } else {
                first = pdf_dict_get(ctx, outlines, PDF_NAME(First));
                last = pdf_dict_get(ctx, outlines, PDF_NAME(Last));
                if ((first == NULL) != (last == NULL)) {
                    status = EXTRACTPDF_ERROR_FORMAT;
                } else if (first != NULL) {
                    marks = pdf_new_mark_bits(ctx, pdf);
                    frame.parent = outlines;
                    frame.node = first;
                    frame.expected_prev = NULL;
                    frame.depth = 1;
                    status = extractpdf_preflight_push(preflight, &frame);
                }
            }
        }

        while (status == EXTRACTPDF_OK && preflight->stack_count != 0) {
            pdf_obj *next;
            pdf_obj *child;
            pdf_obj *child_last;

            frame = preflight->stack[--preflight->stack_count];

            if (!pdf_is_dict(ctx, frame.node) ||
                !pdf_is_indirect(ctx, frame.node)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            if (pdf_mark_bits_set(ctx, marks, frame.node)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            if (pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.node, PDF_NAME(Parent)),
                    frame.parent) != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            if (pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.node, PDF_NAME(Prev)),
                    frame.expected_prev) != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            if (preflight->node_count == SIZE_MAX) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }

            ++preflight->node_count;
            if (frame.depth > EXTRACTPDF_OUTLINE_MAX_DEPTH)
                preflight->too_deep = 1;

            next = pdf_dict_get(ctx, frame.node, PDF_NAME(Next));
            child = pdf_dict_get(ctx, frame.node, PDF_NAME(First));
            child_last = pdf_dict_get(ctx, frame.node, PDF_NAME(Last));

            if ((child == NULL) != (child_last == NULL)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            if (next == NULL &&
                pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.parent, PDF_NAME(Last)),
                    frame.node) != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            if (next != NULL) {
                extractpdf_outline_preflight_frame sibling = frame;
                sibling.node = next;
                sibling.expected_prev = frame.node;
                status = extractpdf_preflight_push(preflight, &sibling);
                if (status != EXTRACTPDF_OK)
                    break;
            }

            if (child != NULL) {
                extractpdf_outline_preflight_frame child_frame;
                if (frame.depth == SIZE_MAX) {
                    status = EXTRACTPDF_ERROR_NOMEM;
                    break;
                }
                child_frame.parent = frame.node;
                child_frame.node = child;
                child_frame.expected_prev = NULL;
                child_frame.depth = frame.depth + 1;
                status = extractpdf_preflight_push(preflight, &child_frame);
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    node_count = preflight->node_count;
    too_deep = preflight->too_deep;
    if (marks != NULL)
        pdf_drop_mark_bits(ctx, marks);
    free(preflight->stack);
    free(preflight);

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (status != EXTRACTPDF_OK)
        return status;
    if (too_deep)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    *out_count = node_count;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_decode_outline_item(
    fz_context *ctx,
    fz_document *document,
    int page_count,
    extractpdf_outline *outline,
    size_t index,
    size_t parent_index,
    size_t previous_sibling_index,
    const fz_outline_item *item)
{
    extractpdf_outline_node_internal *node = &outline->nodes[index];
    extractpdf_status status;

    node->parent_index = parent_index;
    if (parent_index != SIZE_MAX &&
        outline->nodes[parent_index].first_child_index == SIZE_MAX)
        outline->nodes[parent_index].first_child_index = index;
    if (previous_sibling_index != SIZE_MAX)
        outline->nodes[previous_sibling_index].next_sibling_index = index;

    node->is_open = item->is_open != 0;

    if (item->title != NULL) {
        status = extractpdf_outline_append_string(
            outline, item->title, &node->title_offset, &node->title_size);
        if (status != EXTRACTPDF_OK)
            return status;
        node->has_title = 1;
    }

    if (item->uri == NULL) {
        node->destination_kind = EXTRACTPDF_OUTLINE_DESTINATION_NONE;
        return EXTRACTPDF_OK;
    }

    if (fz_is_external_link(ctx, item->uri)) {
        node->destination_kind = EXTRACTPDF_OUTLINE_DESTINATION_URI;
        return extractpdf_outline_append_string(
            outline, item->uri, &node->uri_offset, &node->uri_size);
    }

    {
        fz_link_dest destination = fz_resolve_link_dest(ctx, document, item->uri);
        int page;

        if (destination.loc.chapter < 0 || destination.loc.page < 0)
            return EXTRACTPDF_ERROR_FORMAT;

        page = fz_page_number_from_location(ctx, document, destination.loc);
        if (page < 0 || page >= page_count)
            return EXTRACTPDF_ERROR_FORMAT;

        node->destination_kind = EXTRACTPDF_OUTLINE_DESTINATION_INTERNAL;
        node->target_page = page;
        node->target.x = destination.x;
        node->target.y = destination.y;
    }

    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_flatten_pdf_outline(
    fz_context *ctx,
    fz_document *document,
    extractpdf_outline *outline)
{
    fz_outline_iterator *iter = NULL;
    size_t *parent_stack;
    size_t stack_count = 0;
    size_t index = 0;
    size_t parent_index = SIZE_MAX;
    size_t previous_sibling_index = SIZE_MAX;
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status = EXTRACTPDF_OK;

    parent_stack = (size_t *)malloc(
        EXTRACTPDF_OUTLINE_MAX_DEPTH * sizeof(*parent_stack));
    if (parent_stack == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

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
            status = EXTRACTPDF_ERROR_FORMAT;

        while (status == EXTRACTPDF_OK && index < outline->count) {
            fz_outline_item *item;
            size_t current_index = index;
            size_t cursor_index = current_index;
            int move;
            int exhausted = 0;

            item = fz_outline_iterator_item(ctx, iter);
            if (item == NULL) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            status = extractpdf_decode_outline_item(
                ctx,
                document,
                page_count,
                outline,
                current_index,
                parent_index,
                previous_sibling_index,
                item);
            if (status != EXTRACTPDF_OK)
                break;

            move = fz_outline_iterator_down(ctx, iter);
            if (move == FZ_OUTLINE_ITERATOR_AT_ITEM) {
                if (stack_count >= EXTRACTPDF_OUTLINE_MAX_DEPTH) {
                    status = EXTRACTPDF_ERROR_UNSUPPORTED;
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
                    status = EXTRACTPDF_ERROR_FORMAT;
                    break;
                }
            } else {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            while (status == EXTRACTPDF_OK) {
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
                        status = EXTRACTPDF_ERROR_FORMAT;
                        break;
                    }
                    cursor_index = parent_index;
                    parent_index = parent_stack[--stack_count];
                    continue;
                }

                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }

            if (exhausted)
                break;
        }

        if (status == EXTRACTPDF_OK && index != outline->count)
            status = EXTRACTPDF_ERROR_FORMAT;
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
        return extractpdf_status_from_mupdf(caught_code);
    return status;
}

extractpdf_status extractpdf_document_outline(
    extractpdf_document *document,
    extractpdf_outline **out_outline)
{
    fz_context *ctx;
    pdf_document *pdf = NULL;
    extractpdf_outline *outline;
    extractpdf_status preflight_status = EXTRACTPDF_OK;
    extractpdf_status flatten_status;
    size_t count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_outline == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_outline = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    ctx = document->ctx;
    fz_var(pdf);
    fz_var(preflight_status);
    fz_var(count);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, document->doc);
        if (pdf != NULL)
            preflight_status = extractpdf_preflight_pdf_outline(
                ctx, pdf, &count);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (preflight_status != EXTRACTPDF_OK)
        return preflight_status;

    outline = extractpdf_allocate_outline(count);
    if (outline == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    if (count != 0) {
        flatten_status = extractpdf_flatten_pdf_outline(
            ctx, document->doc, outline);
        if (flatten_status != EXTRACTPDF_OK) {
            extractpdf_dispose_outline(outline);
            return flatten_status;
        }
    }

    *out_outline = outline;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_outline_count(
    const extractpdf_outline *outline,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;

    if (outline == NULL || out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_count = outline->count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_outline_get_info(
    const extractpdf_outline *outline,
    size_t index,
    extractpdf_outline_info *out_info)
{
    const extractpdf_outline_node_internal *node;
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_outline_info, is_open) +
        sizeof(out_info->is_open);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->parent_index = SIZE_MAX;
    out_info->first_child_index = SIZE_MAX;
    out_info->next_sibling_index = SIZE_MAX;
    out_info->destination_kind = EXTRACTPDF_OUTLINE_DESTINATION_NONE;
    out_info->target_page = -1;
    out_info->target.x = 0.0f;
    out_info->target.y = 0.0f;
    out_info->is_open = 0;

    if (outline == NULL || index >= outline->count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    out_info->parent_index = node->parent_index;
    out_info->first_child_index = node->first_child_index;
    out_info->next_sibling_index = node->next_sibling_index;
    out_info->destination_kind = node->destination_kind;
    out_info->target_page = node->target_page;
    out_info->target = node->target;
    out_info->is_open = node->is_open;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_outline_title(
    const extractpdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const extractpdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    if (!node->has_title)
        return EXTRACTPDF_OK;

    *out_utf8 = outline->strings + node->title_offset;
    *out_size = node->title_size;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_outline_uri(
    const extractpdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    const extractpdf_outline_node_internal *node;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (outline == NULL || out_utf8 == NULL || out_size == NULL ||
        index >= outline->count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    node = &outline->nodes[index];
    if (node->destination_kind != EXTRACTPDF_OUTLINE_DESTINATION_URI)
        return EXTRACTPDF_ERROR_ARGUMENT;

    *out_utf8 = outline->strings + node->uri_offset;
    *out_size = node->uri_size;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_outline(extractpdf_outline *outline)
{
    extractpdf_dispose_outline(outline);
}
