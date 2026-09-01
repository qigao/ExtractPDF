#ifndef QUANTAPDF_PDF_IMAGE_RECOMPRESSION_TEST_API_H
#define QUANTAPDF_PDF_IMAGE_RECOMPRESSION_TEST_API_H

#include <quantapdf/quantapdf.h>

#include <stddef.h>

typedef struct quantapdf_test_image_recompression_stats {
    size_t unique_images;
    size_t provider_registrations;
    size_t provider_invocations;
    size_t decoded_preflight_bytes;
    int every_provider_once;
} quantapdf_test_image_recompression_stats;

typedef enum quantapdf_test_image_recompression_fault {
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_NONE = 0,
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PROVIDER_NOMEM = 1,
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_NOMEM = 2,
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_BACKEND = 3,
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PUBLICATION = 4,
    QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_WORK_BUDGET = 5
} quantapdf_test_image_recompression_fault;

void quantapdf_test_image_recompression_get_stats(
    quantapdf_document *document,
    quantapdf_test_image_recompression_stats *out_stats);

void quantapdf_test_image_recompression_set_fault(
    quantapdf_document *document,
    quantapdf_test_image_recompression_fault fault);

#endif
