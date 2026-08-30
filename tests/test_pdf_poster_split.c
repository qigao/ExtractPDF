#include <extractpdf/extractpdf.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
}

static extractpdf_page_poster_split make_split(
    int page_index, size_t columns, size_t rows)
{
    extractpdf_page_poster_split split;
    split.struct_size = sizeof(split);
    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    return split;
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(POSTER_BASIC_PDF, '/');
    const char *backslash = strrchr(POSTER_BASIC_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - POSTER_BASIC_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, POSTER_BASIC_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path, const char *password)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, password, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_split_error(
    extractpdf_document *document,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    extractpdf_status expected)
{
    extractpdf_output *output = output_sentinel();
    CHECK(extractpdf_poster_split_pages(
              document, splits, split_count, &output) == expected);
    CHECK(output == NULL);
}

static extractpdf_rect page_bounds(extractpdf_document *document, int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};
    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static void test_noop(extractpdf_document *document)
{
    extractpdf_page_poster_split split = make_split(1, 1, 1);
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    int count = 0;
    extractpdf_rect before = page_bounds(document, 1);
    extractpdf_rect after;

    CHECK(extractpdf_poster_split_pages(document, &split, 1, &first) ==
          EXTRACTPDF_OK);
    CHECK(first != NULL);
    CHECK(extractpdf_poster_split_pages(document, &split, 1, &second) ==
          EXTRACTPDF_OK);
    CHECK(second != NULL);
    CHECK(extractpdf_output_data(first, &first_data, &first_size) ==
          EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(second, &second_data, &second_size) ==
          EXTRACTPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(extractpdf_page_count(document, &count) == EXTRACTPDF_OK);
    CHECK(count == 3);
    after = page_bounds(document, 1);
    CHECK(before.x0 == after.x0 && before.y0 == after.y0 &&
          before.x1 == after.x1 && before.y1 == after.y1);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_document *other = NULL;
    extractpdf_output *output = output_sentinel();
    extractpdf_page_poster_split split;
    extractpdf_page_poster_split bad;
    extractpdf_page_poster_split pair[2];
    char non_pdf[1024];
    char encrypted_pdf[1024];
    char signed_pdf[1024];
    char malformed_box_pdf[1024];
    char malformed_rotate_pdf[1024];
    char malformed_userunit_pdf[1024];

    sibling_fixture_path("composition-non-pdf.txt", non_pdf, sizeof(non_pdf));
    sibling_fixture_path("encrypted-one-page.pdf", encrypted_pdf, sizeof(encrypted_pdf));
    sibling_fixture_path("annotation-mutation-signed.pdf", signed_pdf, sizeof(signed_pdf));
    sibling_fixture_path("crop-malformed-box.pdf", malformed_box_pdf, sizeof(malformed_box_pdf));
    sibling_fixture_path("crop-malformed-rotate.pdf", malformed_rotate_pdf, sizeof(malformed_rotate_pdf));
    sibling_fixture_path("crop-malformed-userunit.pdf", malformed_userunit_pdf, sizeof(malformed_userunit_pdf));

    document = open_document(POSTER_BASIC_PDF, NULL);
    split = make_split(1, 2, 2);

    CHECK(extractpdf_poster_split_pages(document, &split, 1, NULL) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    expect_split_error(NULL, &split, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_split_error(document, NULL, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_split_error(document, &split, 0, EXTRACTPDF_ERROR_ARGUMENT);

    bad = split;
    bad.struct_size = offsetof(extractpdf_page_poster_split, rows) +
        sizeof(size_t) - 1;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = split;
    bad.page_index = -1;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = split;
    bad.page_index = 3;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    pair[0] = split;
    pair[1] = make_split(1, 3, 1);
    expect_split_error(document, pair, 2, EXTRACTPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = 0;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = split;
    bad.rows = 0;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = SIZE_MAX;
    bad.rows = 2;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = (size_t)INT_MAX;
    bad.rows = 2;
    expect_split_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    other = open_document(non_pdf, NULL);
    bad = make_split(0, 1, 1);
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(encrypted_pdf, "user-pass");
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(signed_pdf, NULL);
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_box_pdf, NULL);
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_rotate_pdf, NULL);
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_userunit_pdf, NULL);
    expect_split_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    test_noop(document);

    output = output_sentinel();
    CHECK(extractpdf_poster_split_pages(document, &split, 1, &output) ==
          EXTRACTPDF_OK);
    CHECK(output != NULL && output != output_sentinel());
    extractpdf_drop_output(output);

    extractpdf_close(document);
    return 0;
}
