#ifndef QUANTAPDF_PDF_SECURITY_REWRITE_TEST_API_H
#define QUANTAPDF_PDF_SECURITY_REWRITE_TEST_API_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

enum {
    QUANTAPDF_SECURITY_TEST_FAULT_ENTROPY_CONFIGURE = 1,
    QUANTAPDF_SECURITY_TEST_FAULT_ENTROPY_WRITE = 2,
    QUANTAPDF_SECURITY_TEST_FAULT_OUTPUT_NOMEM = 3,
    QUANTAPDF_SECURITY_TEST_FAULT_BEFORE_PUBLICATION = 4
};

void quantapdf_security_test_set_fault(
    quantapdf_document *document,
    int fault);

void quantapdf_security_test_get_provider_stats(
    const quantapdf_document *document,
    size_t *out_entries,
    size_t *out_configure_requests,
    size_t *out_write_requests,
    size_t *out_restores);

#endif
