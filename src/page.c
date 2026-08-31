#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_load_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_page **out_page)
{
    quantapdf_page *page;
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = NULL;

    if (document == NULL || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(page_count);
    fz_var(caught_code);

    fz_try(document->ctx)
    {
        page_count = fz_count_pages(document->ctx, document->doc);
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
        fz_report_error(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    if (page_index >= page_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    page = (quantapdf_page *)calloc(1, sizeof(*page));
    if (page == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    page->document = document;
    caught_code = FZ_ERROR_NONE;

    fz_try(document->ctx)
    {
        page->page = fz_load_page(document->ctx, document->doc, page_index);
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
        fz_report_error(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        free(page);
        return quantapdf_status_from_backend(caught_code);
    }

    *out_page = page;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_bounds(
    quantapdf_page *page,
    quantapdf_rect *out_bounds)
{
    fz_rect bounds;
    int caught_code = FZ_ERROR_NONE;

    if (page == NULL || out_bounds == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(bounds);
    fz_var(caught_code);

    fz_try(page->document->ctx)
    {
        bounds = fz_bound_page(page->document->ctx, page->page);
    }
    fz_catch(page->document->ctx)
    {
        caught_code = fz_caught(page->document->ctx);
        fz_report_error(page->document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);

    out_bounds->x0 = bounds.x0;
    out_bounds->y0 = bounds.y0;
    out_bounds->x1 = bounds.x1;
    out_bounds->y1 = bounds.y1;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_box_bounds(
    quantapdf_page *page,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    fz_box_type mupdf_box;
    fz_rect bounds;
    int caught_code = FZ_ERROR_NONE;

    if (page == NULL || out_bounds == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    switch (box) {
    case QUANTAPDF_PAGE_BOX_MEDIA:
        mupdf_box = FZ_MEDIA_BOX;
        break;
    case QUANTAPDF_PAGE_BOX_CROP:
        mupdf_box = FZ_CROP_BOX;
        break;
    default:
        return QUANTAPDF_ERROR_ARGUMENT;
    }

    fz_var(bounds);
    fz_var(caught_code);

    fz_try(page->document->ctx)
    {
        bounds = fz_bound_page_box(page->document->ctx, page->page, mupdf_box);
    }
    fz_catch(page->document->ctx)
    {
        caught_code = fz_caught(page->document->ctx);
        fz_report_error(page->document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);

    out_bounds->x0 = bounds.x0;
    out_bounds->y0 = bounds.y0;
    out_bounds->x1 = bounds.x1;
    out_bounds->y1 = bounds.y1;
    return QUANTAPDF_OK;
}

void quantapdf_drop_page(quantapdf_page *page)
{
    if (page == NULL)
        return;

    if (page->page != NULL)
        fz_drop_page(page->document->ctx, page->page);
    free(page);
}
