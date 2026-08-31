#include "pdf_outline_common.h"

#include <stdlib.h>

#define QUANTAPDF_PDF_OUTLINE_MAX_DEPTH 256u

typedef struct poster_outline_frame {
    pdf_obj *parent;
    pdf_obj *node;
    pdf_obj *expected_prev;
    size_t depth;
} poster_outline_frame;

typedef struct poster_outline_walk {
    poster_outline_frame *stack;
    size_t stack_count;
    size_t stack_capacity;
    size_t node_count;
    int too_deep;
} poster_outline_walk;

static quantapdf_status outline_push(
    poster_outline_walk *walk,
    const poster_outline_frame *frame)
{
    size_t capacity;
    poster_outline_frame *grown;

    if (walk->stack_count == walk->stack_capacity) {
        capacity = walk->stack_capacity == 0 ? 32 : walk->stack_capacity * 2;
        if (capacity < walk->stack_capacity ||
            capacity > SIZE_MAX / sizeof(*walk->stack))
            return QUANTAPDF_ERROR_NOMEM;
        grown = (poster_outline_frame *)realloc(
            walk->stack, capacity * sizeof(*walk->stack));
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        walk->stack = grown;
        walk->stack_capacity = capacity;
    }
    walk->stack[walk->stack_count++] = *frame;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_outline_walk_strict(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_outline_visit_fn visit,
    void *user,
    size_t *out_count)
{
    poster_outline_walk walk = {0};
    poster_outline_frame frame;
    pdf_mark_bits *marks = NULL;
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (out_count != NULL)
        *out_count = 0;
    if (ctx == NULL || document == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(marks);
    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        pdf_obj *root = pdf_dict_get(
            ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        pdf_obj *outlines;
        pdf_obj *first;
        pdf_obj *last;

        if (!pdf_is_dict(ctx, root)) {
            status = QUANTAPDF_ERROR_FORMAT;
        } else {
            outlines = pdf_dict_get(ctx, root, PDF_NAME(Outlines));
            if (outlines != NULL) {
                if (!pdf_is_dict(ctx, outlines)) {
                    status = QUANTAPDF_ERROR_FORMAT;
                } else {
                    first = pdf_dict_get(ctx, outlines, PDF_NAME(First));
                    last = pdf_dict_get(ctx, outlines, PDF_NAME(Last));
                    if ((first == NULL) != (last == NULL)) {
                        status = QUANTAPDF_ERROR_FORMAT;
                    } else if (first != NULL) {
                        marks = pdf_new_mark_bits(ctx, document);
                        frame.parent = outlines;
                        frame.node = first;
                        frame.expected_prev = NULL;
                        frame.depth = 1;
                        status = outline_push(&walk, &frame);
                    }
                }
            }
        }

        while (status == QUANTAPDF_OK && walk.stack_count != 0) {
            pdf_obj *next;
            pdf_obj *child;
            pdf_obj *child_last;
            size_t current_index;

            frame = walk.stack[--walk.stack_count];
            if (!pdf_is_dict(ctx, frame.node) ||
                !pdf_is_indirect(ctx, frame.node)) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            if (pdf_mark_bits_set(ctx, marks, frame.node)) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            if (pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.node, PDF_NAME(Parent)),
                    frame.parent) != 0 ||
                pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.node, PDF_NAME(Prev)),
                    frame.expected_prev) != 0) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            if (walk.node_count == SIZE_MAX) {
                status = QUANTAPDF_ERROR_NOMEM;
                break;
            }
            current_index = walk.node_count++;
            if (frame.depth > QUANTAPDF_PDF_OUTLINE_MAX_DEPTH)
                walk.too_deep = 1;

            if (visit != NULL) {
                status = visit(
                    ctx, document, frame.node, current_index, user);
                if (status != QUANTAPDF_OK)
                    break;
            }

            next = pdf_dict_get(ctx, frame.node, PDF_NAME(Next));
            child = pdf_dict_get(ctx, frame.node, PDF_NAME(First));
            child_last = pdf_dict_get(ctx, frame.node, PDF_NAME(Last));
            if ((child == NULL) != (child_last == NULL)) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            if (next == NULL &&
                pdf_objcmp(
                    ctx,
                    pdf_dict_get(ctx, frame.parent, PDF_NAME(Last)),
                    frame.node) != 0) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }

            if (next != NULL) {
                poster_outline_frame sibling = frame;
                sibling.node = next;
                sibling.expected_prev = frame.node;
                status = outline_push(&walk, &sibling);
                if (status != QUANTAPDF_OK)
                    break;
            }
            if (child != NULL) {
                poster_outline_frame child_frame;
                if (frame.depth == SIZE_MAX) {
                    status = QUANTAPDF_ERROR_NOMEM;
                    break;
                }
                child_frame.parent = frame.node;
                child_frame.node = child;
                child_frame.expected_prev = NULL;
                child_frame.depth = frame.depth + 1;
                status = outline_push(&walk, &child_frame);
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (marks != NULL)
        pdf_drop_mark_bits(ctx, marks);
    free(walk.stack);

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    if (status != QUANTAPDF_OK)
        return status;
    if (walk.too_deep)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    *out_count = walk.node_count;
    return QUANTAPDF_OK;
}
