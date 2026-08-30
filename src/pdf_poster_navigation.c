#include "pdf_poster_internal.h"

#include "pdf_outline_common.h"

#include <math.h>
#include <stdlib.h>

#define POSTER_NAME_TREE_MAX_DEPTH 256u

typedef struct poster_navigation_walk {
    extractpdf_pdf_poster_plan *plan;
    const extractpdf_pdf_poster_private_split *runtime;
    int apply;
} poster_navigation_walk;

static extractpdf_pdf_poster_split_plan *find_split(
    extractpdf_pdf_poster_plan *plan,
    int page_index,
    size_t *out_index)
{
    size_t i;
    for (i = 0; i < plan->split_count; ++i) {
        if (plan->splits[i].page_index == page_index) {
            if (out_index != NULL)
                *out_index = i;
            return &plan->splits[i];
        }
    }
    return NULL;
}

static extractpdf_status reserve_destinations(
    extractpdf_pdf_poster_plan *plan,
    size_t needed)
{
    extractpdf_pdf_poster_dest_plan *grown;
    size_t capacity;

    if (needed <= plan->destination_capacity)
        return EXTRACTPDF_OK;
    capacity = plan->destination_capacity != 0 ? plan->destination_capacity : 8;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity < needed ||
        capacity > SIZE_MAX / sizeof(*plan->destinations))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_poster_dest_plan *)realloc(
        plan->destinations, capacity * sizeof(*plan->destinations));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    plan->destinations = grown;
    plan->destination_capacity = capacity;
    return EXTRACTPDF_OK;
}

static extractpdf_status choose_destination_tile(
    const extractpdf_pdf_poster_split_plan *split,
    float public_x,
    float public_y,
    size_t *out_tile_index)
{
    size_t row;
    size_t column;
    int found_row = 0;
    int found_column = 0;

    if (!isfinite(public_x) || !isfinite(public_y) ||
        public_x < split->page.visible_public.x0 ||
        public_x > split->page.visible_public.x1 ||
        public_y < split->page.visible_public.y0 ||
        public_y > split->page.visible_public.y1)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    for (column = 0; column < split->columns; ++column) {
        if (public_x >= split->x_edges[column] &&
            (public_x < split->x_edges[column + 1] ||
             (column + 1 == split->columns &&
              public_x <= split->x_edges[column + 1]))) {
            found_column = 1;
            break;
        }
    }
    for (row = 0; row < split->rows; ++row) {
        if (public_y >= split->y_edges[row] &&
            (public_y < split->y_edges[row + 1] ||
             (row + 1 == split->rows &&
              public_y <= split->y_edges[row + 1]))) {
            found_row = 1;
            break;
        }
    }
    if (!found_column || !found_row)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    *out_tile_index = row * split->columns + column;
    return EXTRACTPDF_OK;
}

static const extractpdf_pdf_poster_dest_plan *find_destination_plan(
    const extractpdf_pdf_poster_plan *plan,
    extractpdf_pdf_poster_dest_owner_kind owner_kind,
    int owner_page_index,
    size_t owner_ordinal)
{
    size_t i;
    for (i = 0; i < plan->destination_count; ++i) {
        const extractpdf_pdf_poster_dest_plan *entry = &plan->destinations[i];
        if (entry->owner_kind == owner_kind &&
            entry->owner_page_index == owner_page_index &&
            entry->owner_ordinal == owner_ordinal)
            return entry;
    }
    return NULL;
}

static extractpdf_status process_destination(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk,
    pdf_obj *destination,
    extractpdf_pdf_poster_dest_owner_kind owner_kind,
    int owner_page_index,
    size_t owner_ordinal)
{
    pdf_obj *page_operand;
    pdf_obj *kind;
    int page_index;
    size_t split_index = SIZE_MAX;
    extractpdf_pdf_poster_split_plan *split;
    const extractpdf_pdf_poster_dest_plan *existing;

    if (pdf_is_name(ctx, destination) || pdf_is_string(ctx, destination))
        return EXTRACTPDF_OK;
    if (!pdf_is_array(ctx, destination) || pdf_array_len(ctx, destination) < 2)
        return EXTRACTPDF_ERROR_FORMAT;

    page_operand = pdf_array_get(ctx, destination, 0);
    if (!pdf_is_indirect(ctx, page_operand) || !pdf_is_dict(ctx, page_operand) ||
        !pdf_name_eq(
            ctx, pdf_dict_get(ctx, page_operand, PDF_NAME(Type)), PDF_NAME(Page)))
        return EXTRACTPDF_ERROR_FORMAT;
    page_index = pdf_lookup_page_number(ctx, document, page_operand);
    if (page_index < 0 || page_index >= walk->plan->source_page_count)
        return EXTRACTPDF_ERROR_FORMAT;

    kind = pdf_array_get(ctx, destination, 1);
    if (!pdf_is_name(ctx, kind))
        return EXTRACTPDF_ERROR_FORMAT;

    split = find_split(walk->plan, page_index, &split_index);
    if (split == NULL || !split->changed)
        return EXTRACTPDF_OK;
    if (!pdf_name_eq(ctx, kind, PDF_NAME(XYZ)))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (pdf_array_len(ctx, destination) < 5)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    {
        pdf_obj *x_object = pdf_array_get(ctx, destination, 2);
        pdf_obj *y_object = pdf_array_get(ctx, destination, 3);
        pdf_obj *zoom_object = pdf_array_get(ctx, destination, 4);
        float x;
        float y;
        float public_x;
        float public_y;
        size_t tile_index;
        extractpdf_status status;

        if (pdf_is_null(ctx, x_object) || pdf_is_null(ctx, y_object))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        if (!pdf_is_number(ctx, x_object) || !pdf_is_number(ctx, y_object))
            return EXTRACTPDF_ERROR_FORMAT;
        if (!pdf_is_null(ctx, zoom_object) && !pdf_is_number(ctx, zoom_object))
            return EXTRACTPDF_ERROR_FORMAT;

        x = pdf_to_real(ctx, x_object);
        y = pdf_to_real(ctx, y_object);
        if (!isfinite(x) || !isfinite(y))
            return EXTRACTPDF_ERROR_FORMAT;
        public_x = split->page.pdf_to_public.a * x +
            split->page.pdf_to_public.c * y + split->page.pdf_to_public.e;
        public_y = split->page.pdf_to_public.b * x +
            split->page.pdf_to_public.d * y + split->page.pdf_to_public.f;
        status = choose_destination_tile(
            split, public_x, public_y, &tile_index);
        if (status != EXTRACTPDF_OK)
            return status;

        if (walk->apply) {
            existing = find_destination_plan(
                walk->plan, owner_kind, owner_page_index, owner_ordinal);
            if (existing == NULL ||
                existing->source_target_page_index != page_index ||
                existing->split_plan_index != split_index ||
                existing->tile_index != tile_index ||
                existing->split_plan_index >= walk->plan->split_count ||
                walk->runtime == NULL ||
                walk->runtime[existing->split_plan_index].tile_pages == NULL ||
                existing->tile_index >=
                    walk->runtime[existing->split_plan_index].tile_count)
                return EXTRACTPDF_ERROR_FORMAT;
            pdf_array_put(
                ctx,
                destination,
                0,
                walk->runtime[existing->split_plan_index]
                    .tile_pages[existing->tile_index]);
            return EXTRACTPDF_OK;
        }

        status = reserve_destinations(
            walk->plan, walk->plan->destination_count + 1);
        if (status != EXTRACTPDF_OK)
            return status;
        {
            extractpdf_pdf_poster_dest_plan *entry =
                &walk->plan->destinations[walk->plan->destination_count++];
            entry->owner_kind = owner_kind;
            entry->owner_page_index = owner_page_index;
            entry->owner_ordinal = owner_ordinal;
            entry->source_target_page_index = page_index;
            entry->target_public.x = public_x;
            entry->target_public.y = public_y;
            entry->split_plan_index = split_index;
            entry->tile_index = tile_index;
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status process_links(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk)
{
    int page_count = pdf_count_pages(ctx, document);
    int page_index;

    for (page_index = 0; page_index < page_count; ++page_index) {
        pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        int count;
        int index;

        if (annots == NULL)
            continue;
        if (!pdf_is_array(ctx, annots))
            return EXTRACTPDF_ERROR_FORMAT;
        count = pdf_array_len(ctx, annots);
        for (index = 0; index < count; ++index) {
            pdf_obj *annotation = pdf_array_get(ctx, annots, index);
            pdf_obj *subtype;
            pdf_obj *dest;
            pdf_obj *action;
            extractpdf_status status;

            if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation))
                return EXTRACTPDF_ERROR_FORMAT;
            subtype = pdf_dict_get(ctx, annotation, PDF_NAME(Subtype));
            if (!pdf_is_name(ctx, subtype))
                return EXTRACTPDF_ERROR_FORMAT;
            if (!pdf_name_eq(ctx, subtype, PDF_NAME(Link)))
                continue;

            dest = pdf_dict_get(ctx, annotation, PDF_NAME(Dest));
            action = pdf_dict_get(ctx, annotation, PDF_NAME(A));
            if (dest != NULL) {
                status = process_destination(
                    ctx,
                    document,
                    walk,
                    dest,
                    EXTRACTPDF_PDF_POSTER_DEST_LINK_DIRECT,
                    page_index,
                    (size_t)index);
                if (status != EXTRACTPDF_OK)
                    return status;
                continue;
            }
            if (action != NULL && pdf_is_dict(ctx, action) &&
                pdf_name_eq(
                    ctx, pdf_dict_get(ctx, action, PDF_NAME(S)), PDF_NAME(GoTo))) {
                pdf_obj *target = pdf_dict_get(ctx, action, PDF_NAME(D));
                if (target == NULL)
                    return EXTRACTPDF_ERROR_FORMAT;
                status = process_destination(
                    ctx,
                    document,
                    walk,
                    target,
                    EXTRACTPDF_PDF_POSTER_DEST_LINK_ACTION,
                    page_index,
                    (size_t)index);
                if (status != EXTRACTPDF_OK)
                    return status;
            }
        }
    }
    return EXTRACTPDF_OK;
}

typedef struct poster_outline_context {
    poster_navigation_walk *walk;
} poster_outline_context;

static extractpdf_status outline_visit(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *item,
    size_t preorder_index,
    void *user)
{
    poster_outline_context *outline = (poster_outline_context *)user;
    pdf_obj *dest = pdf_dict_get(ctx, item, PDF_NAME(Dest));
    pdf_obj *action = pdf_dict_get(ctx, item, PDF_NAME(A));
    pdf_obj *kind;

    if (dest != NULL && action != NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    if (dest != NULL)
        return process_destination(
            ctx,
            document,
            outline->walk,
            dest,
            EXTRACTPDF_PDF_POSTER_DEST_OUTLINE_DIRECT,
            -1,
            preorder_index);
    if (action == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, action))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_dict_get(ctx, action, PDF_NAME(Next)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    kind = pdf_dict_get(ctx, action, PDF_NAME(S));
    if (!pdf_is_name(ctx, kind))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_name_eq(ctx, kind, PDF_NAME(URI)))
        return EXTRACTPDF_OK;
    if (pdf_name_eq(ctx, kind, PDF_NAME(GoTo))) {
        pdf_obj *target = pdf_dict_get(ctx, action, PDF_NAME(D));
        if (target == NULL)
            return EXTRACTPDF_ERROR_FORMAT;
        return process_destination(
            ctx,
            document,
            outline->walk,
            target,
            EXTRACTPDF_PDF_POSTER_DEST_OUTLINE_ACTION,
            -1,
            preorder_index);
    }
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

static extractpdf_status process_outlines(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk)
{
    poster_outline_context outline;
    size_t count = 0;
    outline.walk = walk;
    return extractpdf_pdf_outline_walk_strict(
        ctx, document, outline_visit, &outline, &count);
}

static extractpdf_status destination_from_definition(
    fz_context *ctx,
    pdf_obj *value,
    pdf_obj **out_destination)
{
    *out_destination = NULL;
    if (pdf_is_array(ctx, value)) {
        *out_destination = value;
        return EXTRACTPDF_OK;
    }
    if (!pdf_is_dict(ctx, value))
        return EXTRACTPDF_ERROR_FORMAT;
    *out_destination = pdf_dict_get(ctx, value, PDF_NAME(D));
    if (*out_destination == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}

static extractpdf_status process_name_tree_node(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk,
    pdf_obj *node,
    size_t depth,
    pdf_mark_bits *marks,
    size_t *ordinal)
{
    pdf_obj *names;
    pdf_obj *kids;
    int count;
    int index;

    if (depth > POSTER_NAME_TREE_MAX_DEPTH)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (!pdf_is_dict(ctx, node))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_is_indirect(ctx, node) && pdf_mark_bits_set(ctx, marks, node))
        return EXTRACTPDF_ERROR_FORMAT;

    names = pdf_dict_get(ctx, node, PDF_NAME(Names));
    kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
    if (names != NULL && kids != NULL)
        return EXTRACTPDF_ERROR_FORMAT;

    if (names != NULL) {
        if (!pdf_is_array(ctx, names) || (pdf_array_len(ctx, names) % 2) != 0)
            return EXTRACTPDF_ERROR_FORMAT;
        count = pdf_array_len(ctx, names);
        for (index = 0; index < count; index += 2) {
            pdf_obj *key = pdf_array_get(ctx, names, index);
            pdf_obj *value = pdf_array_get(ctx, names, index + 1);
            pdf_obj *destination;
            extractpdf_status status;

            if (!pdf_is_string(ctx, key))
                return EXTRACTPDF_ERROR_FORMAT;
            status = destination_from_definition(ctx, value, &destination);
            if (status != EXTRACTPDF_OK)
                return status;
            status = process_destination(
                ctx,
                document,
                walk,
                destination,
                EXTRACTPDF_PDF_POSTER_DEST_NAME_TREE,
                -1,
                *ordinal);
            if (status != EXTRACTPDF_OK)
                return status;
            ++*ordinal;
        }
        return EXTRACTPDF_OK;
    }

    if (kids == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_array(ctx, kids))
        return EXTRACTPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, kids);
    for (index = 0; index < count; ++index) {
        pdf_obj *kid = pdf_array_get(ctx, kids, index);
        extractpdf_status status;
        if (!pdf_is_indirect(ctx, kid) || !pdf_is_dict(ctx, kid))
            return EXTRACTPDF_ERROR_FORMAT;
        status = process_name_tree_node(
            ctx, document, walk, kid, depth + 1, marks, ordinal);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status process_named_destinations(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk,
    pdf_obj *root)
{
    pdf_obj *names_root = pdf_dict_get(ctx, root, PDF_NAME(Names));
    pdf_obj *dests;
    pdf_mark_bits *marks = NULL;
    size_t ordinal = 0;
    extractpdf_status status = EXTRACTPDF_OK;

    if (names_root == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, names_root))
        return EXTRACTPDF_ERROR_FORMAT;
    dests = pdf_dict_get(ctx, names_root, PDF_NAME(Dests));
    if (dests == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, dests))
        return EXTRACTPDF_ERROR_FORMAT;

    marks = pdf_new_mark_bits(ctx, document);
    status = process_name_tree_node(
        ctx, document, walk, dests, 1, marks, &ordinal);
    pdf_drop_mark_bits(ctx, marks);
    return status;
}

static extractpdf_status process_legacy_destinations(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk,
    pdf_obj *root)
{
    pdf_obj *dests = pdf_dict_get(ctx, root, PDF_NAME(Dests));
    int count;
    int index;

    if (dests == NULL)
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, dests))
        return EXTRACTPDF_ERROR_FORMAT;
    count = pdf_dict_len(ctx, dests);
    for (index = 0; index < count; ++index) {
        pdf_obj *key = pdf_dict_get_key(ctx, dests, index);
        pdf_obj *value = pdf_dict_get_val(ctx, dests, index);
        pdf_obj *destination;
        extractpdf_status status;

        if (!pdf_is_name(ctx, key))
            return EXTRACTPDF_ERROR_FORMAT;
        status = destination_from_definition(ctx, value, &destination);
        if (status != EXTRACTPDF_OK)
            return status;
        status = process_destination(
            ctx,
            document,
            walk,
            destination,
            EXTRACTPDF_PDF_POSTER_DEST_LEGACY_DICT,
            -1,
            (size_t)index);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status process_navigation_registry(
    fz_context *ctx,
    pdf_document *document,
    poster_navigation_walk *walk)
{
    pdf_obj *root;
    extractpdf_status status;

    root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
    if (!pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;

    if (pdf_dict_gets(ctx, root, "OpenAction") != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(AA)) != NULL ||
        pdf_dict_gets(ctx, root, "PageLabels") != NULL ||
        pdf_dict_gets(ctx, root, "Threads") != NULL ||
        pdf_dict_get(ctx, root, PDF_NAME(StructTreeRoot)) != NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = process_links(ctx, document, walk);
    if (status != EXTRACTPDF_OK)
        return status;
    status = process_outlines(ctx, document, walk);
    if (status != EXTRACTPDF_OK)
        return status;
    status = process_named_destinations(ctx, document, walk, root);
    if (status != EXTRACTPDF_OK)
        return status;
    return process_legacy_destinations(ctx, document, walk, root);
}

extractpdf_status extractpdf_pdf_poster_navigation_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan)
{
    poster_navigation_walk walk;

    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    free(plan->destinations);
    plan->destinations = NULL;
    plan->destination_count = 0;
    plan->destination_capacity = 0;
    walk.plan = plan;
    walk.runtime = NULL;
    walk.apply = 0;
    return process_navigation_registry(ctx, document, &walk);
}

extractpdf_status extractpdf_pdf_poster_apply_navigation(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_poster_plan *plan,
    extractpdf_pdf_poster_private_split *runtime)
{
    poster_navigation_walk walk;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    walk.plan = (extractpdf_pdf_poster_plan *)plan;
    walk.runtime = runtime;
    walk.apply = 1;

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = process_navigation_registry(ctx, document, &walk);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }
    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return status;
}
