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

void extractpdf_drop_page(extractpdf_page *page)
{
    if (page == NULL)
        return;

    if (page->page != NULL)
        fz_drop_page(page->document->ctx, page->page);
    free(page);
}
