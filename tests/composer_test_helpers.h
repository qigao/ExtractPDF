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
int quantapdf_test_make_oversized_png(
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_make_truncated_jpeg(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size);
int quantapdf_test_pdf_content_contains(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *needle);
int quantapdf_test_pdf_content_order(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *first,
    const char *second);
size_t quantapdf_test_pdf_content_count(
    const unsigned char *data,
    size_t size,
    size_t page_index,
    const char *needle);
void quantapdf_test_use_comma_locale(int enabled);

#ifdef __cplusplus
}
#endif

#endif
