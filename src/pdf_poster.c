#include "pdf_internal.h"

extractpdf_status extractpdf_poster_split_pages(
    extractpdf_document *document,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    extractpdf_output **out_output)
{
    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || splits == NULL || split_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
