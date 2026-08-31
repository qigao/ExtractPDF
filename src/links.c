#include "internal.h"
#include "backend/pdfium_document.h"

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

quantapdf_status quantapdf_extract_links(
    quantapdf_page *page,
    quantapdf_link_page **out_links)
{
    if (out_links == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_links = NULL;

    if (page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return quantapdf_pdfium_extract_links(page->pdfium_page, out_links);
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
