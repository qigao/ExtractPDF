#include "internal.h"
#include "backend/qpdf_document.h"

#include <stdlib.h>

quantapdf_status quantapdf_rewrite_lossless(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->qpdf_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    status = quantapdf_qpdf_rewrite_lossless(
        document->qpdf_document,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        free(output->data);
        free(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
