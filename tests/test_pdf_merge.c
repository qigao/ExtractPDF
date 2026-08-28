#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

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

static extractpdf_document *open_output(
    const extractpdf_output *output,
    const char *path)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    extractpdf_document *document = NULL;

    CHECK(extractpdf_output_data(output, &data, &size) == EXTRACTPDF_OK);
    CHECK(data != NULL);
    CHECK(size >= 5);
    CHECK(memcmp(data, "%PDF-", 5) == 0);
    CHECK(write_bytes(path, data, size));
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    return document;
}

static void expect_page(
    extractpdf_document *document,
    int page_index,
    const char *needle,
    int check_geometry,
    float width,
    float height)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    if (check_geometry) {
        CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
        CHECK(bounds.x0 == 0.0f);
        CHECK(bounds.y0 == 0.0f);
        CHECK(bounds.x1 == width);
        CHECK(bounds.y1 == height);
    }
    CHECK(extractpdf_extract_text(page, &text, &text_size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(strstr(text, needle) != NULL);

    extractpdf_free(text);
    extractpdf_drop_page(page);
}

static void expect_same_bytes(
    const extractpdf_output *left,
    const extractpdf_output *right)
{
    const unsigned char *left_data = NULL;
    const unsigned char *right_data = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(extractpdf_output_data(left, &left_data, &left_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(right, &right_data, &right_size) == EXTRACTPDF_OK);
    CHECK(left_size == right_size);
    CHECK(memcmp(left_data, right_data, left_size) == 0);
}

int main(void)
{
    int sentinel = 0;
    extractpdf_document *composition = NULL;
    extractpdf_document *text = NULL;
    extractpdf_document *reopened = NULL;
    extractpdf_output *output_a = NULL;
    extractpdf_output *output_b = NULL;
    extractpdf_output *merged_ab_1 = NULL;
    extractpdf_output *merged_ab_2 = NULL;
    extractpdf_output *merged_single = NULL;
    extractpdf_output *merged_duplicate = NULL;
    extractpdf_output *reset_output = (extractpdf_output *)&sentinel;
    int composition_indices[] = {2, 0};
    int text_indices[] = {0};
    const extractpdf_output *empty_inputs[] = {NULL};
    int page_count = 0;

    (void)remove(MERGE_OUTPUT_PDF);

    CHECK(extractpdf_merge_outputs(NULL, 1, &reset_output) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(reset_output == NULL);

    reset_output = (extractpdf_output *)&sentinel;
    CHECK(extractpdf_merge_outputs(empty_inputs, 0, &reset_output) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(reset_output == NULL);

    CHECK(extractpdf_merge_outputs(empty_inputs, 1, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_open(COMPOSITION_PDF, NULL, &composition) == EXTRACTPDF_OK);
    CHECK(extractpdf_open(TEXT_PDF, NULL, &text) == EXTRACTPDF_OK);

    CHECK(extractpdf_export_pages(
        composition,
        composition_indices,
        sizeof(composition_indices) / sizeof(composition_indices[0]),
        &output_a) == EXTRACTPDF_OK);
    CHECK(output_a != NULL);

    CHECK(extractpdf_export_pages(
        text,
        text_indices,
        sizeof(text_indices) / sizeof(text_indices[0]),
        &output_b) == EXTRACTPDF_OK);
    CHECK(output_b != NULL);

    extractpdf_close(composition);
    extractpdf_close(text);
    composition = NULL;
    text = NULL;

    {
        const extractpdf_output *invalid_inputs[] = {output_a, NULL};
        reset_output = (extractpdf_output *)&sentinel;
        CHECK(extractpdf_merge_outputs(
            invalid_inputs,
            sizeof(invalid_inputs) / sizeof(invalid_inputs[0]),
            &reset_output) == EXTRACTPDF_ERROR_ARGUMENT);
        CHECK(reset_output == NULL);
    }

    {
        const extractpdf_output *inputs[] = {output_a, output_b};
        CHECK(extractpdf_merge_outputs(inputs, 2, &merged_ab_1) == EXTRACTPDF_OK);
        CHECK(extractpdf_merge_outputs(inputs, 2, &merged_ab_2) == EXTRACTPDF_OK);
        CHECK(merged_ab_1 != NULL);
        CHECK(merged_ab_2 != NULL);
        expect_same_bytes(merged_ab_1, merged_ab_2);
    }

    {
        const extractpdf_output *inputs[] = {output_a};
        CHECK(extractpdf_merge_outputs(inputs, 1, &merged_single) == EXTRACTPDF_OK);
        CHECK(merged_single != NULL);
    }

    {
        const extractpdf_output *inputs[] = {output_b, output_a, output_b};
        CHECK(extractpdf_merge_outputs(inputs, 3, &merged_duplicate) == EXTRACTPDF_OK);
        CHECK(merged_duplicate != NULL);
    }

    extractpdf_drop_output(output_a);
    extractpdf_drop_output(output_b);
    output_a = NULL;
    output_b = NULL;

    reopened = open_output(merged_ab_1, MERGE_OUTPUT_PDF);
    CHECK(extractpdf_page_count(reopened, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 3);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 2, "Hello Caf", 0, 0.0f, 0.0f);
    extractpdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_single, MERGE_OUTPUT_PDF);
    CHECK(extractpdf_page_count(reopened, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    extractpdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_duplicate, MERGE_OUTPUT_PDF);
    CHECK(extractpdf_page_count(reopened, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 4);
    expect_page(reopened, 0, "Hello Caf", 0, 0.0f, 0.0f);
    expect_page(reopened, 1, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 2, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 3, "Hello Caf", 0, 0.0f, 0.0f);
    extractpdf_close(reopened);
    reopened = NULL;

    extractpdf_drop_output(merged_ab_1);
    extractpdf_drop_output(merged_ab_2);
    extractpdf_drop_output(merged_single);
    extractpdf_drop_output(merged_duplicate);
    (void)remove(MERGE_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
