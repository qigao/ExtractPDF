#include <extractpdf/extractpdf.h>

#include <math.h>
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

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static void check_rect_close(extractpdf_rect actual, extractpdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
}

static extractpdf_page_trim make_trim(
    int page_index,
    float x0,
    float y0,
    float x1,
    float y1)
{
    extractpdf_page_trim trim;

    trim.struct_size = sizeof(trim);
    trim.page_index = page_index;
    trim.bounds.x0 = x0;
    trim.bounds.y0 = y0;
    trim.bounds.x1 = x1;
    trim.bounds.y1 = y1;
    return trim;
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(TRIM_INTERACTIVE_PDF, '/');
    const char *backslash = strrchr(TRIM_INTERACTIVE_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - TRIM_INTERACTIVE_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, TRIM_INTERACTIVE_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path, const char *password)
{
    extractpdf_document *document = NULL;

    CHECK(extractpdf_open(path, password, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_trim_error(
    extractpdf_document *document,
    const extractpdf_page_trim *trims,
    size_t trim_count,
    extractpdf_status expected)
{
    extractpdf_output *output = output_sentinel();

    CHECK(extractpdf_trim_pages(document, trims, trim_count, &output) == expected);
    CHECK(output == NULL);
}

static extractpdf_rect page_box(
    extractpdf_document *document,
    int page_index,
    extractpdf_page_box box)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_box_bounds(page, box, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static void test_noop_determinism(
    extractpdf_document *document,
    extractpdf_rect source_media)
{
    extractpdf_page_trim full = make_trim(
        0,
        source_media.x0,
        source_media.y0,
        source_media.x1,
        source_media.y1);
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    extractpdf_rect after;

    CHECK(extractpdf_trim_pages(document, &full, 1, &first) == EXTRACTPDF_OK);
    CHECK(first != NULL);
    CHECK(extractpdf_trim_pages(document, &full, 1, &second) == EXTRACTPDF_OK);
    CHECK(second != NULL);
    CHECK(extractpdf_output_data(first, &first_data, &first_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(second, &second_data, &second_size) == EXTRACTPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0);
    CHECK(second_size == first_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    after = page_box(document, 0, EXTRACTPDF_PAGE_BOX_MEDIA);
    check_rect_close(after, source_media);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_document *other = NULL;
    extractpdf_output *output = output_sentinel();
    extractpdf_rect source_media;
    extractpdf_page_trim trim;
    extractpdf_page_trim bad;
    extractpdf_page_trim pair[2];
    char non_pdf[1024];
    char encrypted_pdf[1024];
    char signed_pdf[1024];
    char malformed_box_pdf[1024];
    char malformed_rotate_pdf[1024];
    char malformed_userunit_pdf[1024];

    sibling_fixture_path("composition-non-pdf.txt", non_pdf, sizeof(non_pdf));
    sibling_fixture_path("encrypted-one-page.pdf", encrypted_pdf, sizeof(encrypted_pdf));
    sibling_fixture_path("annotation-mutation-signed.pdf", signed_pdf, sizeof(signed_pdf));
    sibling_fixture_path("trim-malformed-box.pdf", malformed_box_pdf, sizeof(malformed_box_pdf));
    sibling_fixture_path("trim-malformed-rotate.pdf", malformed_rotate_pdf, sizeof(malformed_rotate_pdf));
    sibling_fixture_path("trim-malformed-userunit.pdf", malformed_userunit_pdf, sizeof(malformed_userunit_pdf));

    document = open_document(TRIM_INTERACTIVE_PDF, NULL);
    source_media = page_box(document, 0, EXTRACTPDF_PAGE_BOX_MEDIA);
    check_rect_close(
        source_media,
        (extractpdf_rect){0.0f, 0.0f, 400.0f, 300.0f});
    trim = make_trim(0, 40.0f, 30.0f, 360.0f, 270.0f);

    CHECK(extractpdf_trim_pages(document, &trim, 1, NULL) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    expect_trim_error(NULL, &trim, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_trim_error(document, NULL, 1, EXTRACTPDF_ERROR_ARGUMENT);
    expect_trim_error(document, &trim, 0, EXTRACTPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.struct_size =
        offsetof(extractpdf_page_trim, bounds) + sizeof(extractpdf_rect) - 1;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.page_index = -1;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.page_index = 2;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    pair[0] = trim;
    pair[1] = make_trim(0, 50.0f, 40.0f, 350.0f, 260.0f);
    expect_trim_error(document, pair, 2, EXTRACTPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x0 = NAN;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.y0 = INFINITY;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x1 = -INFINITY;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x1 = bad.bounds.x0;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.y1 = bad.bounds.y0;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x0 = 370.0f;
    bad.bounds.x1 = 360.0f;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x0 = source_media.x0 - 1.0f;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x1 = source_media.x1 + 1.0f;
    expect_trim_error(document, &bad, 1, EXTRACTPDF_ERROR_ARGUMENT);

    other = open_document(non_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(encrypted_pdf, "user-pass");
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(signed_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_box_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    bad = make_trim(1, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_rotate_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    other = open_document(malformed_userunit_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, EXTRACTPDF_ERROR_FORMAT);
    extractpdf_close(other);
    other = NULL;

    test_noop_determinism(document, source_media);

    output = output_sentinel();
    if (extractpdf_trim_pages(document, &trim, 1, &output) != EXTRACTPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid trim failed\n");
        CHECK(output == NULL);
        extractpdf_close(document);
        return EXIT_FAILURE;
    }

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return EXIT_SUCCESS;
}
