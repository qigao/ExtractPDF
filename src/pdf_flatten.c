#include "pdf_internal.h"

#include <stdint.h>

extractpdf_status extractpdf_flatten_interactive(
    extractpdf_document *document,
    uint32_t flags,
    extractpdf_output **out_output)
{
    const uint32_t known =
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        flags == 0 || (flags & ~known) != 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_STATE;
}
