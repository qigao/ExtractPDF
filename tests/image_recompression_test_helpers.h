#ifndef QUANTAPDF_IMAGE_RECOMPRESSION_TEST_HELPERS_H
#define QUANTAPDF_IMAGE_RECOMPRESSION_TEST_HELPERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int image_recompression_create_positive_fixture(
    const char *source_path,
    const char *output_path);

int image_recompression_check_positive_output(
    const unsigned char *data,
    size_t size,
    size_t expected_image_count);

int image_recompression_matches_expected_base64(
    const unsigned char *data,
    size_t size,
    const char *expected_path);

int image_recompression_create_policy_fixture(
    const char *source_path,
    const char *output_path);

int image_recompression_check_policy_output(
    const char *source_path,
    const unsigned char *data,
    size_t size,
    int expect_boundary_rewritten);

typedef enum image_recompression_malformed_fixture {
    IMAGE_RECOMPRESSION_MALFORMED_RESOURCES = 1,
    IMAGE_RECOMPRESSION_MALFORMED_XOBJECTS = 2,
    IMAGE_RECOMPRESSION_MALFORMED_APPEARANCE_STATE = 3,
    IMAGE_RECOMPRESSION_MALFORMED_NONSTREAM_IMAGE = 4,
    IMAGE_RECOMPRESSION_MALFORMED_WIDTH = 5,
    IMAGE_RECOMPRESSION_MALFORMED_HEIGHT = 6,
    IMAGE_RECOMPRESSION_MALFORMED_BPC = 7,
    IMAGE_RECOMPRESSION_MALFORMED_IMAGE_MASK = 8,
    IMAGE_RECOMPRESSION_MALFORMED_DECODE_SCALAR = 9,
    IMAGE_RECOMPRESSION_MALFORMED_DECODE_LENGTH = 10,
    IMAGE_RECOMPRESSION_MALFORMED_DECODE_ENTRY = 11,
    IMAGE_RECOMPRESSION_MALFORMED_DECODE_NONFINITE = 12,
    IMAGE_RECOMPRESSION_MALFORMED_DIMENSION_RANGE = 13,
    IMAGE_RECOMPRESSION_MALFORMED_SAMPLE_COUNT = 14
} image_recompression_malformed_fixture;

int image_recompression_create_malformed_fixture(
    const char *source_path,
    const char *output_path,
    image_recompression_malformed_fixture fixture);

#ifdef __cplusplus
}
#endif

#endif
