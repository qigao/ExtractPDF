#include <quantapdf/quantapdf.h>

#include "image_recompression_test_helpers.h"
#include "pdf_image_recompression_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void check_policy_cap(
    quantapdf_document *document,
    size_t cap,
    int expect_boundary_rewritten,
    size_t expected_rewritten)
{
    quantapdf_image_recompression_options options = {
        sizeof(options), 90, cap};
    quantapdf_output *output = NULL;
    quantapdf_test_image_recompression_stats stats = {0, 0, 0, 0};
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(
        quantapdf_recompress_images(document, &options, &output) ==
        QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    CHECK(
        quantapdf_output_save_file(output, POLICY_OUTPUT_PDF) ==
        QUANTAPDF_OK);
    CHECK(image_recompression_check_policy_output(
        POLICY_FIXTURE_PDF, data, size, expect_boundary_rewritten));
    quantapdf_test_image_recompression_get_stats(document, &stats);
    CHECK(stats.unique_images == expected_rewritten);
    CHECK(stats.provider_registrations == expected_rewritten);
    CHECK(stats.provider_invocations == expected_rewritten);
    CHECK(stats.every_provider_once);
    quantapdf_drop_output(output);
}

static void check_malformed_inputs(void)
{
    quantapdf_image_recompression_options options = {
        sizeof(options),
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    int fixture;

    for (fixture = IMAGE_RECOMPRESSION_MALFORMED_RESOURCES;
         fixture <= IMAGE_RECOMPRESSION_MALFORMED_SAMPLE_COUNT;
         ++fixture) {
        quantapdf_document *document = NULL;
        CHECK(image_recompression_create_malformed_fixture(
            POSITIVE_SOURCE_PDF,
            MALFORMED_FIXTURE_PDF,
            (image_recompression_malformed_fixture)fixture));
        CHECK(
            quantapdf_open(MALFORMED_FIXTURE_PDF, NULL, &document) ==
            QUANTAPDF_OK);
        CHECK(document != NULL);
        expect_failure(document, &options, QUANTAPDF_ERROR_FORMAT);
        quantapdf_close(document);
    }
}

static void check_security_fail_closed(void)
{
    quantapdf_image_recompression_options options = {
        sizeof(options),
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(SIGNED_PDF, NULL, &document) == QUANTAPDF_OK);
    expect_failure(document, &options, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(document);
    document = NULL;

    CHECK(
        quantapdf_open(ENCRYPTED_PDF, "user-pass", &document) ==
        QUANTAPDF_OK);
    expect_failure(document, &options, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(document);
}

static void check_fault_atomicity(void)
{
    static const struct {
        quantapdf_test_image_recompression_fault fault;
        quantapdf_status expected;
    } cases[] = {
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PROVIDER_NOMEM,
         QUANTAPDF_ERROR_NOMEM},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_NOMEM,
         QUANTAPDF_ERROR_NOMEM},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_BACKEND,
         QUANTAPDF_ERROR_BACKEND},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PUBLICATION,
         QUANTAPDF_ERROR_BACKEND},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_WORK_BUDGET,
         QUANTAPDF_ERROR_UNSUPPORTED}};
    quantapdf_image_recompression_options options = {
        sizeof(options),
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_document *document = NULL;
    size_t index;

    CHECK(quantapdf_open(POSITIVE_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        quantapdf_test_image_recompression_set_fault(
            document, cases[index].fault);
        expect_failure(document, &options, cases[index].expected);
        expect_success(document, &options);
    }
    quantapdf_close(document);
}

static void check_render_equivalence(
    const char *source_path,
    const char *output_path)
{
    quantapdf_document *source = NULL;
    quantapdf_document *output = NULL;
    int source_pages = 0;
    int output_pages = 0;
    int page_index;
    unsigned int maximum_delta = 0;
    unsigned long long total_delta = 0;
    size_t total_samples = 0;

    CHECK(quantapdf_open(source_path, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_open(output_path, NULL, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(source, &source_pages) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(output, &output_pages) == QUANTAPDF_OK);
    CHECK(source_pages == output_pages);
    for (page_index = 0; page_index < source_pages; ++page_index) {
        quantapdf_page *source_page = NULL;
        quantapdf_page *output_page = NULL;
        quantapdf_bitmap *source_bitmap = NULL;
        quantapdf_bitmap *output_bitmap = NULL;
        quantapdf_rect source_bounds = {0, 0, 0, 0};
        quantapdf_rect output_bounds = {0, 0, 0, 0};
        const unsigned char *source_data = NULL;
        const unsigned char *output_data = NULL;
        size_t source_size = 0;
        size_t output_size = 0;
        int source_width = 0;
        int source_height = 0;
        int source_stride = 0;
        int source_components = 0;
        int output_width = 0;
        int output_height = 0;
        int output_stride = 0;
        int output_components = 0;
        size_t index;

        CHECK(
            quantapdf_load_page(source, page_index, &source_page) ==
            QUANTAPDF_OK);
        CHECK(
            quantapdf_load_page(output, page_index, &output_page) ==
            QUANTAPDF_OK);
        CHECK(quantapdf_page_bounds(source_page, &source_bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_page_bounds(output_page, &output_bounds) == QUANTAPDF_OK);
        CHECK(memcmp(&source_bounds, &output_bounds, sizeof(source_bounds)) == 0);
        CHECK(quantapdf_render_page(source_page, &source_bitmap) == QUANTAPDF_OK);
        CHECK(quantapdf_render_page(output_page, &output_bitmap) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_dimensions(
                source_bitmap,
                &source_width,
                &source_height,
                &source_stride,
                &source_components) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_dimensions(
                output_bitmap,
                &output_width,
                &output_height,
                &output_stride,
                &output_components) == QUANTAPDF_OK);
        CHECK(source_width == output_width);
        CHECK(source_height == output_height);
        CHECK(source_stride == output_stride);
        CHECK(source_components == output_components);
        CHECK(
            quantapdf_bitmap_data(
                source_bitmap, &source_data, &source_size) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_data(
                output_bitmap, &output_data, &output_size) == QUANTAPDF_OK);
        CHECK(source_size == output_size);
        for (index = 0; index < source_size; ++index) {
            unsigned int const delta = source_data[index] > output_data[index]
                ? (unsigned int)(source_data[index] - output_data[index])
                : (unsigned int)(output_data[index] - source_data[index]);
            if (delta > maximum_delta)
                maximum_delta = delta;
            total_delta += delta;
        }
        total_samples += source_size;
        quantapdf_drop_bitmap(source_bitmap);
        quantapdf_drop_bitmap(output_bitmap);
        quantapdf_drop_page(source_page);
        quantapdf_drop_page(output_page);
    }
    CHECK(maximum_delta <= 24);
    CHECK(total_samples != 0);
    CHECK(total_delta <= (unsigned long long)total_samples * 2u);
    quantapdf_close(source);
    quantapdf_close(output);
}

int main(void)
{
    struct extended_options {
        quantapdf_image_recompression_options v1;
        uint32_t ignored_tail;
    } extended = {{sizeof(extended), 90, 1024}, UINT32_C(0xa5a5a5a5)};
    quantapdf_document *document = NULL;
    quantapdf_output *positive_output = NULL;
    quantapdf_output *repeated_output = NULL;
    quantapdf_output *quality_output = NULL;
    quantapdf_image_recompression_options options = {
        sizeof(options),
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_image_recompression_options minimum_options = {
        QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE,
        90,
        0};
    quantapdf_test_image_recompression_stats stats = {0, 0, 0, 0};
    int positive_page_count = 0;

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
    CHECK(
        quantapdf_page_count(document, &positive_page_count) ==
        QUANTAPDF_OK);
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
        const unsigned char *repeated_data = NULL;
        const unsigned char *quality_data = NULL;
        size_t size = 0;
        size_t repeated_size = 0;
        size_t quality_size = 0;
        int page_count = 0;
        CHECK(
            quantapdf_output_data(positive_output, &data, &size) ==
            QUANTAPDF_OK);
        CHECK(data != NULL && size != 0);
        CHECK(image_recompression_matches_expected_base64(
            data, size, EXPECTED_Q90_BASE64));

        CHECK(
            quantapdf_recompress_images(document, &options, &repeated_output) ==
            QUANTAPDF_OK);
        CHECK(repeated_output != NULL);
        CHECK(
            quantapdf_output_data(
                repeated_output, &repeated_data, &repeated_size) ==
            QUANTAPDF_OK);
        CHECK(repeated_size == size);
        CHECK(memcmp(data, repeated_data, size) == 0);

        options.jpeg_quality = 40;
        CHECK(
            quantapdf_recompress_images(document, &options, &quality_output) ==
            QUANTAPDF_OK);
        CHECK(quality_output != NULL);
        CHECK(
            quantapdf_output_data(
                quality_output, &quality_data, &quality_size) ==
            QUANTAPDF_OK);
        CHECK(
            quality_size != size ||
            memcmp(data, quality_data, size) != 0);
        quantapdf_drop_output(quality_output);
        quality_output = NULL;
        quality_data = NULL;
        quality_size = 0;
        options.jpeg_quality = 100;
        CHECK(
            quantapdf_recompress_images(document, &options, &quality_output) ==
            QUANTAPDF_OK);
        CHECK(quality_output != NULL);
        CHECK(
            quantapdf_output_data(
                quality_output, &quality_data, &quality_size) ==
            QUANTAPDF_OK);
        CHECK(image_recompression_check_positive_output(
            quality_data, quality_size, 8));
        CHECK(
            quantapdf_output_save_file(
                positive_output, POSITIVE_OUTPUT_PDF) == QUANTAPDF_OK);

        quantapdf_close(document);
        document = NULL;
        CHECK(image_recompression_check_positive_output(data, size, 8));
        CHECK(
            quantapdf_open(POSITIVE_OUTPUT_PDF, NULL, &document) ==
            QUANTAPDF_OK);
        CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
        CHECK(page_count == positive_page_count);
        quantapdf_close(document);
        document = NULL;
        check_render_equivalence(
            POSITIVE_FIXTURE_PDF, POSITIVE_OUTPUT_PDF);
    }
    quantapdf_drop_output(positive_output);
    quantapdf_drop_output(repeated_output);
    quantapdf_drop_output(quality_output);

    CHECK(image_recompression_create_policy_fixture(
        POSITIVE_SOURCE_PDF, POLICY_FIXTURE_PDF));
    CHECK(quantapdf_open(POLICY_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    check_policy_cap(document, 5, 0, 1);
    check_policy_cap(document, 6, 1, 2);
    quantapdf_close(document);
    check_malformed_inputs();
    check_security_fail_closed();
    check_fault_atomicity();
    return 0;
}
