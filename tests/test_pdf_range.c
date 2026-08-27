#include <extractpdf/extractpdf.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int expect_page(
    extractpdf_document *document,
    int index,
    const char *needle,
    float width,
    float height)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;
    int ok = 0;

    if (extractpdf_load_page(document, index, &page) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_page_bounds(page, &bounds) != EXTRACTPDF_OK)
        goto done;
    if (bounds.x0 != 0.0f || bounds.y0 != 0.0f ||
        bounds.x1 != width || bounds.y1 != height)
        goto done;
    if (extractpdf_extract_text(page, &text, &text_size) != EXTRACTPDF_OK)
        goto done;
    if (text == NULL || strstr(text, needle) == NULL)
        goto done;

    ok = 1;

done:
    extractpdf_free(text);
    extractpdf_drop_page(page);
    return ok;
}

static int expect_range_error(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_status expected)
{
    extractpdf_output *output = output_sentinel();
    extractpdf_status status = extractpdf_export_page_range(
        document, first_page, page_count, &output);

    if (status != expected || output != NULL) {
        if (output != NULL && output != output_sentinel())
            extractpdf_drop_output(output);
        return 0;
    }
    return 1;
}

static int range_equals_indices(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    const int *indices,
    size_t index_count)
{
    extractpdf_output *range_output = NULL;
    extractpdf_output *index_output = NULL;
    const unsigned char *range_data = NULL;
    const unsigned char *index_data = NULL;
    size_t range_size = 0;
    size_t index_size = 0;
    int ok = 0;

    if (extractpdf_export_page_range(
            document, first_page, page_count, &range_output) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_export_pages(
            document, indices, index_count, &index_output) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_output_data(range_output, &range_data, &range_size) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_output_data(index_output, &index_data, &index_size) != EXTRACTPDF_OK)
        goto done;
    if (range_data == NULL || index_data == NULL || range_size == 0 ||
        range_size != index_size || memcmp(range_data, index_data, range_size) != 0)
        goto done;

    ok = 1;

done:
    extractpdf_drop_output(range_output);
    extractpdf_drop_output(index_output);
    return ok;
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_document *reopened = NULL;
    extractpdf_document *text_document = NULL;
    extractpdf_output *range_output = NULL;
    extractpdf_output *index_output = NULL;
    extractpdf_output *probe = NULL;
    const unsigned char *range_data = NULL;
    const unsigned char *index_data = NULL;
    size_t range_size = 0;
    size_t index_size = 0;
    int page_count = 0;
    int primary_indices[] = {1, 2};
    int single_index[] = {2};
    int full_indices[] = {0, 1, 2};
    int result = 1;

    (void)remove(RANGE_OUTPUT_PDF);

    if (extractpdf_open(COMPOSITION_PDF, NULL, &document) != EXTRACTPDF_OK) {
        fprintf(stderr, "could not open composition source PDF\n");
        goto cleanup;
    }

    if (!expect_range_error(NULL, 0, 1, EXTRACTPDF_ERROR_ARGUMENT)) {
        fprintf(stderr, "NULL document range contract failed\n");
        goto cleanup;
    }

    if (extractpdf_export_page_range(document, 0, 1, NULL) !=
        EXTRACTPDF_ERROR_ARGUMENT) {
        fprintf(stderr, "NULL out_output range contract failed\n");
        goto cleanup;
    }

    if (!expect_range_error(document, -1, 1, EXTRACTPDF_ERROR_ARGUMENT) ||
        !expect_range_error(document, 0, 0, EXTRACTPDF_ERROR_ARGUMENT) ||
        !expect_range_error(document, 2, 2, EXTRACTPDF_ERROR_ARGUMENT) ||
        !expect_range_error(document, INT_MAX, 2, EXTRACTPDF_ERROR_ARGUMENT)) {
        fprintf(stderr, "range validation/reset contract failed\n");
        goto cleanup;
    }

#if SIZE_MAX > INT_MAX
    if (!expect_range_error(
            document, 0, (size_t)INT_MAX + 1u, EXTRACTPDF_ERROR_ARGUMENT)) {
        fprintf(stderr, "large page_count contract failed\n");
        goto cleanup;
    }
#endif

    if (extractpdf_export_page_range(document, 1, 2, &range_output) !=
            EXTRACTPDF_OK ||
        range_output == NULL) {
        fprintf(stderr, "primary range export failed\n");
        goto cleanup;
    }

    if (extractpdf_export_pages(document, primary_indices, 2, &index_output) !=
            EXTRACTPDF_OK ||
        index_output == NULL) {
        fprintf(stderr, "primary explicit-index export failed\n");
        goto cleanup;
    }

    if (extractpdf_output_data(range_output, &range_data, &range_size) !=
            EXTRACTPDF_OK ||
        extractpdf_output_data(index_output, &index_data, &index_size) !=
            EXTRACTPDF_OK ||
        range_data == NULL || index_data == NULL || range_size < 5 ||
        range_size != index_size || memcmp(range_data, "%PDF-", 5) != 0 ||
        memcmp(range_data, index_data, range_size) != 0) {
        fprintf(stderr, "range/explicit engine-equivalence contract failed\n");
        goto cleanup;
    }

    if (!range_equals_indices(document, 2, 1, single_index, 1)) {
        fprintf(stderr, "single-page range equivalence failed\n");
        goto cleanup;
    }

    if (!range_equals_indices(document, 0, 3, full_indices, 3)) {
        fprintf(stderr, "whole-document range equivalence failed\n");
        goto cleanup;
    }

    extractpdf_close(document);
    document = NULL;

    if (range_data[0] != '%' || range_data[1] != 'P' ||
        !write_bytes(RANGE_OUTPUT_PDF, range_data, range_size)) {
        fprintf(stderr, "range output did not survive source close\n");
        goto cleanup;
    }

    if (extractpdf_open(RANGE_OUTPUT_PDF, NULL, &reopened) != EXTRACTPDF_OK) {
        fprintf(stderr, "could not reopen range output\n");
        goto cleanup;
    }

    if (extractpdf_page_count(reopened, &page_count) != EXTRACTPDF_OK ||
        page_count != 2) {
        fprintf(stderr, "range output page count mismatch\n");
        goto cleanup;
    }

    if (!expect_page(reopened, 0, "PAGE-B", 240.0f, 180.0f) ||
        !expect_page(reopened, 1, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "range output order/text/geometry mismatch\n");
        goto cleanup;
    }

    extractpdf_close(reopened);
    reopened = NULL;

    if (extractpdf_open(COMPOSITION_NON_PDF, NULL, &text_document) !=
            EXTRACTPDF_OK) {
        fprintf(stderr, "non-PDF fixture did not open through generic API\n");
        goto cleanup;
    }

    probe = output_sentinel();
    if (extractpdf_export_page_range(text_document, 0, 1, &probe) !=
            EXTRACTPDF_ERROR_UNSUPPORTED ||
        probe != NULL) {
        fprintf(stderr, "non-PDF range unsupported contract failed\n");
        if (probe != NULL && probe != output_sentinel())
            extractpdf_drop_output(probe);
        probe = NULL;
        goto cleanup;
    }

    result = 0;

cleanup:
    extractpdf_close(document);
    extractpdf_close(reopened);
    extractpdf_close(text_document);
    extractpdf_drop_output(range_output);
    extractpdf_drop_output(index_output);
    if (probe != NULL && probe != output_sentinel())
        extractpdf_drop_output(probe);
    (void)remove(RANGE_OUTPUT_PDF);
    return result;
}
