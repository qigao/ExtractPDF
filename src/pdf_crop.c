#include "pdf_internal.h"

extractpdf_status extractpdf_crop_pages(
    extractpdf_document *document,
    const extractpdf_page_crop *crops,
    size_t crop_count,
    extractpdf_output **out_output)
{
    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || crops == NULL || crop_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
