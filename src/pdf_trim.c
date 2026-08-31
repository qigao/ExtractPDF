#include "internal.h"

#include "backend/qpdf_document.h"

#include <stdlib.h>

quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->qpdf_document == NULL ||
        trims == NULL || trim_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    status = quantapdf_qpdf_trim_pages(
        document->qpdf_document,
        trims,
        trim_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
