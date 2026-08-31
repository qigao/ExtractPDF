#include "internal.h"

quantapdf_status quantapdf_document_audit(
    quantapdf_document *document,
    quantapdf_audit_result *out_result)
{
    if (out_result == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (out_result->struct_size >= QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE)
        out_result->findings = 0;
    if (document == NULL ||
        out_result->struct_size < QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE)
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_sanitize(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || flags == 0 ||
        (flags & ~QUANTAPDF_SANITIZE_ALL) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}
