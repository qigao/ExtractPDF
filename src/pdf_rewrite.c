#include "internal.h"

quantapdf_status quantapdf_rewrite_lossless(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->qpdf_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    return QUANTAPDF_ERROR_UNSUPPORTED;
}
