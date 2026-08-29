#include "internal.h"

extractpdf_status extractpdf_trim_pages(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_output **out_output)
{
    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || trims == NULL || trim_count == 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
