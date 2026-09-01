#include "pdf_security_rewrite_test_api.h"

#include "internal.h"

void quantapdf_security_test_set_fault(
    quantapdf_document *document,
    int fault)
{
    if (document != NULL)
        document->test_security_fault = fault;
}

void quantapdf_security_test_get_provider_stats(
    const quantapdf_document *document,
    size_t *out_entries,
    size_t *out_configure_requests,
    size_t *out_write_requests,
    size_t *out_restores)
{
    if (out_entries != NULL)
        *out_entries = document == NULL
            ? 0u : document->test_security_provider_entries;
    if (out_configure_requests != NULL)
        *out_configure_requests = document == NULL
            ? 0u : document->test_security_configure_requests;
    if (out_write_requests != NULL)
        *out_write_requests = document == NULL
            ? 0u : document->test_security_write_requests;
    if (out_restores != NULL)
        *out_restores = document == NULL
            ? 0u : document->test_security_provider_restores;
}
