#include "pdf_trim_internal.h"

#include <stdint.h>
#include <stdlib.h>

extractpdf_status extractpdf_trim_pages(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_output **out_output)
{
    pdf_document *source_pdf;
    extractpdf_pdf_trim_plan *plans = NULL;
    extractpdf_status status;
    int any_changed = 0;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        trims == NULL || trim_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (source_pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_trim_check_security(document->ctx, source_pdf);
    if (status != EXTRACTPDF_OK)
        return status;

    if (trim_count > SIZE_MAX / sizeof(*plans))
        return EXTRACTPDF_ERROR_NOMEM;
    plans = (extractpdf_pdf_trim_plan *)calloc(trim_count, sizeof(*plans));
    if (plans == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    status = extractpdf_pdf_trim_build_plan(
        document->ctx,
        source_pdf,
        trims,
        trim_count,
        plans,
        &any_changed);
    if (status == EXTRACTPDF_OK) {
        if (any_changed) {
            status = EXTRACTPDF_ERROR_UNSUPPORTED;
        }
        else {
            status = extractpdf_serialize_pdf(
                document->ctx, source_pdf, out_output);
        }
    }

    free(plans);
    return status;
}
