#include <quantapdf/quantapdf.h>

#include "image_recompression_test_helpers.h"
#include "pdf_image_recompression_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(
    QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE ==
        offsetof(quantapdf_image_recompression_options, jpeg_quality) +
            sizeof(int),
    "unexpected image recompression V1 minimum size");
_Static_assert(
    QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_SIZE ==
        sizeof(quantapdf_image_recompression_options),
    "unexpected image recompression V1 size");
_Static_assert(
    QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES ==
        (size_t)64u * (size_t)1024u * (size_t)1024u,
    "unexpected image recompression default decoded-byte cap");

static void check_impl(int ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static void expect_failure(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();

    CHECK(quantapdf_recompress_images(document, options, &output) == expected);
    CHECK(output == NULL);
}

static void expect_success(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options)
{
    quantapdf_output *output = NULL;

    CHECK(quantapdf_recompress_images(document, options, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    quantapdf_drop_output(output);
}

int main(void)
{
    struct extended_options {
        quantapdf_image_recompression_options v1;
        uint32_t ignored_tail;
    } extended = {{sizeof(extended), 90, 1024}, UINT32_C(0xa5a5a5a5)};
    quantapdf_document *document = NULL;
    quantapdf_output *positive_output = NULL;
    quantapdf_image_recompression_options options = {
        sizeof(options),
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_image_recompression_options minimum_options = {
        QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE,
        90,
        0};
    quantapdf_test_image_recompression_stats stats = {0, 0, 0, 0};

    CHECK(
        quantapdf_recompress_images(NULL, &options, NULL) ==
        QUANTAPDF_ERROR_ARGUMENT);
    expect_failure(NULL, &options, QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_open(ONE_PAGE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);

    expect_failure(document, NULL, QUANTAPDF_ERROR_ARGUMENT);
    options.struct_size = QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE - 1;
    expect_failure(document, &options, QUANTAPDF_ERROR_ARGUMENT);
    options.struct_size = sizeof(options);
    options.jpeg_quality = 0;
    expect_failure(document, &options, QUANTAPDF_ERROR_ARGUMENT);
    options.jpeg_quality = 101;
    expect_failure(document, &options, QUANTAPDF_ERROR_ARGUMENT);

    expect_success(document, &minimum_options);
    options.jpeg_quality = 90;
    options.max_decoded_bytes_per_image = 0;
    expect_success(document, &options);
    expect_success(document, &extended.v1);

    quantapdf_close(document);

    CHECK(image_recompression_create_positive_fixture(
        POSITIVE_SOURCE_PDF, POSITIVE_FIXTURE_PDF));
    CHECK(quantapdf_open(POSITIVE_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    options.struct_size = sizeof(options);
    options.jpeg_quality = 90;
    options.max_decoded_bytes_per_image = 0;
    CHECK(
        quantapdf_recompress_images(document, &options, &positive_output) ==
        QUANTAPDF_OK);
    CHECK(positive_output != NULL);
    quantapdf_test_image_recompression_get_stats(document, &stats);
    CHECK(stats.unique_images == 8);
    CHECK(stats.provider_registrations == 8);
    CHECK(stats.provider_invocations == 8);
    CHECK(stats.every_provider_once);
    {
        const unsigned char *data = NULL;
        size_t size = 0;
        CHECK(
            quantapdf_output_data(positive_output, &data, &size) ==
            QUANTAPDF_OK);
        CHECK(data != NULL && size != 0);
        CHECK(image_recompression_check_positive_output(data, size, 8));
    }
    quantapdf_drop_output(positive_output);
    quantapdf_close(document);
    return 0;
}
