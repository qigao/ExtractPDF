#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void quantapdf_zero_rect(quantapdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static void quantapdf_dispose_link_page(quantapdf_link_page *links)
{
    size_t i;

    if (links == NULL)
        return;

    for (i = 0; i < links->count; ++i)
        free(links->items[i].uri);
    free(links->items);
    free(links);
}

static char *quantapdf_copy_uri(const char *uri, size_t *out_size)
{
    char *copy;
    size_t size;

    *out_size = 0;
    if (uri == NULL)
        return NULL;

    size = strlen(uri);
    if (size == SIZE_MAX)
        return NULL;

    copy = (char *)malloc(size + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, uri, size + 1);
    *out_size = size;
    return copy;
}

quantapdf_status quantapdf_extract_links(
    quantapdf_page *page,
    quantapdf_link_page **out_links)
{
    quantapdf_link_page *snapshot;
    fz_context *ctx;
    fz_link *head = NULL;
    fz_link *link;
    size_t count = 0;
    size_t index = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_links == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_links = NULL;

    if (page == NULL || page->page == NULL || page->document == NULL ||
        page->document->ctx == NULL || page->document->doc == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    snapshot = (quantapdf_link_page *)calloc(1, sizeof(*snapshot));
    if (snapshot == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    ctx = page->document->ctx;
    fz_var(head);
    fz_var(caught_code);

    fz_try(ctx)
    {
        head = fz_load_links(ctx, page->page);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status status = quantapdf_status_from_mupdf(caught_code);
        quantapdf_dispose_link_page(snapshot);
        return status;
    }

    for (link = head; link != NULL; link = link->next) {
        if (count == SIZE_MAX) {
            fz_drop_link(ctx, head);
            quantapdf_dispose_link_page(snapshot);
            return QUANTAPDF_ERROR_NOMEM;
        }
        ++count;
    }

    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*snapshot->items)) {
            fz_drop_link(ctx, head);
            quantapdf_dispose_link_page(snapshot);
            return QUANTAPDF_ERROR_NOMEM;
        }
        snapshot->items = (quantapdf_link_internal *)calloc(
            count,
            sizeof(*snapshot->items));
        if (snapshot->items == NULL) {
            fz_drop_link(ctx, head);
            quantapdf_dispose_link_page(snapshot);
            return QUANTAPDF_ERROR_NOMEM;
        }
    }
    snapshot->count = count;

    for (link = head; link != NULL; link = link->next, ++index) {
        quantapdf_link_internal *item = &snapshot->items[index];

        item->hotspot.x0 = link->rect.x0;
        item->hotspot.y0 = link->rect.y0;
        item->hotspot.x1 = link->rect.x1;
        item->hotspot.y1 = link->rect.y1;
        item->target_page = -1;

        if (fz_is_external_link(ctx, link->uri)) {
            item->kind = QUANTAPDF_LINK_URI;
            item->uri = quantapdf_copy_uri(link->uri, &item->uri_size);
            if (item->uri == NULL) {
                fz_drop_link(ctx, head);
                quantapdf_dispose_link_page(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }
        else {
            caught_code = FZ_ERROR_NONE;
            item->kind = QUANTAPDF_LINK_INTERNAL;
            fz_try(ctx)
            {
                fz_link_dest dest = fz_resolve_link_dest(
                    ctx,
                    page->document->doc,
                    link->uri);
                item->target_page = fz_page_number_from_location(
                    ctx,
                    page->document->doc,
                    dest.loc);
                item->target.x = dest.x;
                item->target.y = dest.y;
            }
            fz_catch(ctx)
            {
                caught_code = fz_caught(ctx);
                fz_report_error(ctx);
            }

            if (caught_code != FZ_ERROR_NONE) {
                quantapdf_status status = quantapdf_status_from_mupdf(caught_code);
                fz_drop_link(ctx, head);
                quantapdf_dispose_link_page(snapshot);
                return status;
            }
        }
    }

    fz_drop_link(ctx, head);
    *out_links = snapshot;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_link_count(
    const quantapdf_link_page *links,
    size_t *out_count)
{
    if (out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = 0;

    if (links == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_count = links->count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_link_get_info(
    const quantapdf_link_page *links,
    size_t index,
    quantapdf_link_info *out_info)
{
    const quantapdf_link_internal *item;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_link_info, target) +
        sizeof(out_info->target);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    quantapdf_zero_rect(&out_info->hotspot);
    out_info->kind = (quantapdf_link_kind)0;
    out_info->target_page = -1;
    out_info->target.x = 0.0f;
    out_info->target.y = 0.0f;

    if (links == NULL || index >= links->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    item = &links->items[index];
    out_info->hotspot = item->hotspot;
    out_info->kind = item->kind;
    out_info->target_page = item->target_page;
    out_info->target = item->target;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_link_uri(
    const quantapdf_link_page *links,
    size_t index,
    const char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (out_utf8 == NULL || out_size == NULL || links == NULL ||
        index >= links->count)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (links->items[index].kind != QUANTAPDF_LINK_URI ||
        links->items[index].uri == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    *out_utf8 = links->items[index].uri;
    *out_size = links->items[index].uri_size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_link_page(quantapdf_link_page *links)
{
    quantapdf_dispose_link_page(links);
}
