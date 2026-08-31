#ifndef QUANTAPDF_TEST_PDF_CROP_INTERNAL_H
#define QUANTAPDF_TEST_PDF_CROP_INTERNAL_H

#include <stddef.h>

int crop_raw_expect_local_cropbox(
    const unsigned char *data,
    size_t size,
    int page_index,
    int expect_present,
    const float expected[4]);

int crop_raw_expect_no_local_default_boxes(
    const unsigned char *data,
    size_t size,
    int page_index);

int crop_raw_expect_preserved_graph(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size);

#endif
