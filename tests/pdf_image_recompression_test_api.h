#ifndef QUANTAPDF_PDF_IMAGE_RECOMPRESSION_TEST_API_H
#define QUANTAPDF_PDF_IMAGE_RECOMPRESSION_TEST_API_H

#include <quantapdf/quantapdf.h>

#include <stddef.h>

typedef struct quantapdf_test_image_recompression_stats {
    size_t unique_images;
    size_t provider_registrations;
    size_t provider_invocations;
    int every_provider_once;
} quantapdf_test_image_recompression_stats;

void quantapdf_test_image_recompression_get_stats(
    quantapdf_document *document,
    quantapdf_test_image_recompression_stats *out_stats);

#endif
