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

#ifdef __cplusplus
}
#endif

#endif
