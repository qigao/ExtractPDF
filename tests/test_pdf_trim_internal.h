#ifndef EXTRACTPDF_TEST_PDF_TRIM_INTERNAL_H
#define EXTRACTPDF_TEST_PDF_TRIM_INTERNAL_H

#include <stddef.h>

int trim_raw_expect_local_mediabox(
    const unsigned char *data,
    size_t size,
    int page_index,
    int expect_present,
    const float expected[4]);

int trim_raw_expect_preserved_cropbox(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size,
    int page_index);

int trim_raw_expect_preserved_graph(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size);

#endif
