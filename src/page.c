#include "internal.h"

#include "backend/pdfium_document.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

quantapdf_status quantapdf_load_page(
    quantapdf_document *document,
    int page_index,
    quantapdf_page **out_page)
{
    quantapdf_page *page;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;

    if (out_page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = NULL;

    if (document == NULL || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    page = (quantapdf_page *)calloc(1, sizeof(*page));
    if (page == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    page->document = document;
    page->page_index = page_index;
    status = quantapdf_pdfium_load_page(
        document->pdfium_document, page_index, &page->pdfium_page);
    if (status != QUANTAPDF_OK) {
        free(page);
        return status;
    }
    caught_code = FZ_ERROR_NONE;
    fz_var(caught_code);

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
        quantapdf_pdfium_drop_page(page->pdfium_page);
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
    quantapdf_rect bounds;
    quantapdf_status status;
    double user_unit;
    double width;
    double height;

    if (page == NULL || out_bounds == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_pdfium_page_bounds(page->pdfium_page, &bounds);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status != QUANTAPDF_OK)
        return status;
    width = (double)bounds.x1 * user_unit;
    height = (double)bounds.y1 * user_unit;
    if (!isfinite(width) || !isfinite(height) ||
        width <= 0.0 || height <= 0.0 ||
        width > FLT_MAX || height > FLT_MAX)
        return QUANTAPDF_ERROR_FORMAT;
    out_bounds->x0 = 0.0f;
    out_bounds->y0 = 0.0f;
    out_bounds->x1 = (float)width;
    out_bounds->y1 = (float)height;
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
    quantapdf_pdfium_drop_page(page->pdfium_page);
    free(page);
}
