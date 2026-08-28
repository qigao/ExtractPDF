#include <extractpdf/extractpdf.h>

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

static int verify_delete_complement(
    extractpdf_document *source,
    const int *retained_indices,
    size_t retained_count,
    const char *const *expected_text,
    const float *expected_width,
    const float *expected_height)
{
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    extractpdf_document *reopened = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    int reopened_count = 0;
    size_t i;
    int ok = 0;

    (void)remove(DELETE_OUTPUT_PDF);

    if (extractpdf_export_pages(source, retained_indices, retained_count, &first) !=
            EXTRACTPDF_OK ||
        first == NULL)
        goto done;
    if (extractpdf_export_pages(source, retained_indices, retained_count, &second) !=
            EXTRACTPDF_OK ||
        second == NULL)
        goto done;

    if (extractpdf_output_data(first, &first_data, &first_size) != EXTRACTPDF_OK ||
        first_data == NULL || first_size < 5 ||
        memcmp(first_data, "%PDF-", 5) != 0)
        goto done;
    if (extractpdf_output_data(second, &second_data, &second_size) != EXTRACTPDF_OK ||
        second_data == NULL || second_size != first_size ||
        memcmp(first_data, second_data, first_size) != 0)
        goto done;

    if (!write_bytes(DELETE_OUTPUT_PDF, first_data, first_size))
        goto done;
    if (extractpdf_open(DELETE_OUTPUT_PDF, NULL, &reopened) != EXTRACTPDF_OK)
        goto done;
    if (extractpdf_page_count(reopened, &reopened_count) != EXTRACTPDF_OK ||
        reopened_count != (int)retained_count)
        goto done;

    for (i = 0; i < retained_count; ++i) {
        if (!expect_page(
                reopened,
                (int)i,
                expected_text[i],
                expected_width[i],
                expected_height[i]))
            goto done;
    }

    ok = 1;

done:
    extractpdf_close(reopened);
    extractpdf_drop_output(first);
    extractpdf_drop_output(second);
    (void)remove(DELETE_OUTPUT_PDF);
    return ok;
}

int main(void)
{
    extractpdf_document *source = NULL;
    extractpdf_output *probe = NULL;
    int page_count = 0;
    int delete_middle_keep[] = {0, 2};
    const char *delete_middle_text[] = {"PAGE-A", "PAGE-C"};
    float delete_middle_width[] = {200.0f, 300.0f};
    float delete_middle_height[] = {200.0f, 150.0f};
    int delete_edges_keep[] = {1};
    const char *delete_edges_text[] = {"PAGE-B"};
    float delete_edges_width[] = {240.0f};
    float delete_edges_height[] = {180.0f};
    int dummy_index = 0;
    int result = 1;

    if (extractpdf_open(COMPOSITION_PDF, NULL, &source) != EXTRACTPDF_OK) {
        fprintf(stderr, "could not open composition source PDF\n");
        goto cleanup;
    }

    if (extractpdf_page_count(source, &page_count) != EXTRACTPDF_OK || page_count != 3 ||
        !expect_page(source, 0, "PAGE-A", 200.0f, 200.0f) ||
        !expect_page(source, 1, "PAGE-B", 240.0f, 180.0f) ||
        !expect_page(source, 2, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "source fixture contract failed\n");
        goto cleanup;
    }

    /* delete {1} => retain complement {0,2} in original source order */
    if (!verify_delete_complement(
            source,
            delete_middle_keep,
            sizeof(delete_middle_keep) / sizeof(delete_middle_keep[0]),
            delete_middle_text,
            delete_middle_width,
            delete_middle_height)) {
        fprintf(stderr, "delete-middle complement contract failed\n");
        goto cleanup;
    }

    /* delete {0,2} => retain complement {1} */
    if (!verify_delete_complement(
            source,
            delete_edges_keep,
            sizeof(delete_edges_keep) / sizeof(delete_edges_keep[0]),
            delete_edges_text,
            delete_edges_width,
            delete_edges_height)) {
        fprintf(stderr, "delete-edges complement contract failed\n");
        goto cleanup;
    }

    /* delete-all would require an empty retained set; V1 intentionally rejects it. */
    probe = output_sentinel();
    if (extractpdf_export_pages(source, &dummy_index, 0, &probe) !=
            EXTRACTPDF_ERROR_ARGUMENT ||
        probe != NULL) {
        fprintf(stderr, "delete-all unsupported contract failed\n");
        if (probe != NULL && probe != output_sentinel())
            extractpdf_drop_output(probe);
        probe = NULL;
        goto cleanup;
    }

    if (extractpdf_page_count(source, &page_count) != EXTRACTPDF_OK || page_count != 3 ||
        !expect_page(source, 0, "PAGE-A", 200.0f, 200.0f) ||
        !expect_page(source, 1, "PAGE-B", 240.0f, 180.0f) ||
        !expect_page(source, 2, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "source document changed after delete-by-complement exports\n");
        goto cleanup;
    }

    result = 0;

cleanup:
    extractpdf_close(source);
    if (probe != NULL && probe != output_sentinel())
        extractpdf_drop_output(probe);
    (void)remove(DELETE_OUTPUT_PDF);
    return result;
}
