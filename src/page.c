#include "internal.h"

#include <stdlib.h>

extractpdf_status extractpdf_load_page(
    extractpdf_document *document,
    int page_index,
    extractpdf_page **out_page)
{
    extractpdf_page *page;
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_page == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_page = NULL;

    if (document == NULL || page_index < 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

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
        return extractpdf_status_from_mupdf(caught_code);
    if (page_index >= page_count)
        return EXTRACTPDF_ERROR_ARGUMENT;

    page = (extractpdf_page *)calloc(1, sizeof(*page));
    if (page == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

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
        return extractpdf_status_from_mupdf(caught_code);
    }

    *out_page = page;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_page_bounds(
    extractpdf_page *page,
    extractpdf_rect *out_bounds)
{
    fz_rect bounds;
    int caught_code = FZ_ERROR_NONE;

    if (page == NULL || out_bounds == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

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
        return extractpdf_status_from_mupdf(caught_code);

    out_bounds->x0 = bounds.x0;
    out_bounds->y0 = bounds.y0;
    out_bounds->x1 = bounds.x1;
    out_bounds->y1 = bounds.y1;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_page_box_bounds(
    extractpdf_page *page,
    extractpdf_page_box box,
    extractpdf_rect *out_bounds)
{
    fz_box_type mupdf_box;
    fz_rect bounds;
    int caught_code = FZ_ERROR_NONE;

    if (page == NULL || out_bounds == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    switch (box) {
    case EXTRACTPDF_PAGE_BOX_MEDIA:
        mupdf_box = FZ_MEDIA_BOX;
        break;
    case EXTRACTPDF_PAGE_BOX_CROP:
        mupdf_box = FZ_CROP_BOX;
        break;
    default:
        return EXTRACTPDF_ERROR_ARGUMENT;
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
        return extractpdf_status_from_mupdf(caught_code);

    out_bounds->x0 = bounds.x0;
    out_bounds->y0 = bounds.y0;
    out_bounds->x1 = bounds.x1;
    out_bounds->y1 = bounds.y1;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_page(extractpdf_page *page)
{
    if (page == NULL)
        return;

    if (page->page != NULL)
        fz_drop_page(page->document->ctx, page->page);
    free(page);
}
