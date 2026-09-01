#ifndef QUANTAPDF_COMPOSER_TEST_HELPERS_H
#define QUANTAPDF_COMPOSER_TEST_HELPERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int quantapdf_test_make_jpeg(unsigned char **out_data, size_t *out_size);
int quantapdf_test_make_png(
    int alpha,
    unsigned char **out_data,
    size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
