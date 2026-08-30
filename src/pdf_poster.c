#include "pdf_poster_internal.h"

extractpdf_status extractpdf_poster_split_pages(
    extractpdf_document *document,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    extractpdf_output **out_output)
{
    pdf_document *source_pdf;
    extractpdf_pdf_poster_plan *plan = NULL;
    extractpdf_status status;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        splits == NULL || split_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_poster_check_security(document->ctx, source_pdf);
    if (status != EXTRACTPDF_OK)
        return status;

    status = extractpdf_pdf_poster_build_plan(
        document->ctx, source_pdf, splits, split_count, 0, &plan);
    if (status != EXTRACTPDF_OK)
        return status;

    if (!plan->any_changed)
        status = extractpdf_serialize_pdf(document->ctx, source_pdf, out_output);
    else
        status = EXTRACTPDF_ERROR_UNSUPPORTED;

    extractpdf_pdf_poster_drop_plan(plan);
    return status;
}
