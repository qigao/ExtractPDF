#ifndef QUANTAPDF_TEST_PDF_POSTER_SPLIT_INTERNAL_H
#define QUANTAPDF_TEST_PDF_POSTER_SPLIT_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int poster_raw_check_basic_tiles(
    const unsigned char *data,
    size_t size,
    int first_tile_page,
    size_t tile_count,
    const float (*expected_boxes)[4],
    int expected_rotate,
    float expected_user_unit);

int poster_raw_check_interactive(
    const unsigned char *data,
    size_t size);

int poster_raw_check_navigation(
    const unsigned char *data,
    size_t size);

int poster_create_catalog_signature_fixture(
    const char *source_path,
    const char *output_path);

int poster_run_geometry_tests(void);
int poster_run_policy_tests(void);
int poster_run_interactive_tests(void);
int poster_run_navigation_tests(void);
int poster_run_batch_tests(void);

#ifdef __cplusplus
}
#endif

#endif
