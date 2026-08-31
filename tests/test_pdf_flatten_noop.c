#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '/');
    const char *backslash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - FLATTEN_COMBINED_ORDER_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, FLATTEN_COMBINED_ORDER_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void check_equal_outputs(
    const extractpdf_output *left,
    const extractpdf_output *right)
{
    const unsigned char *left_bytes = NULL;
    const unsigned char *right_bytes = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(left != NULL);
    CHECK(right != NULL);
    CHECK(extractpdf_output_data(left, &left_bytes, &left_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(right, &right_bytes, &right_size) == EXTRACTPDF_OK);
    CHECK(left_bytes != NULL);
    CHECK(right_bytes != NULL);
    CHECK(left_size != 0);
    CHECK(left_size == right_size);
    CHECK(memcmp(left_bytes, right_bytes, left_size) == 0);
}

static void check_neutral_link_noop(void)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    int page_count = 0;

    sibling_fixture_path(
        "flatten-noop-neutral-link.pdf", path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &first) == EXTRACTPDF_OK);
    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &second) == EXTRACTPDF_OK);
    check_equal_outputs(first, second);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(document);
}

static void check_zero_widget_noop(void)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_form *form = NULL;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    size_t field_count = 0;
    size_t widget_count = 0;

    sibling_fixture_path(
        "flatten-noop-zero-widget.pdf", path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == 1);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == 0);
    extractpdf_drop_form(form);
    form = NULL;

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_WIDGETS, &first) == EXTRACTPDF_OK);
    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_WIDGETS, &second) == EXTRACTPDF_OK);
    check_equal_outputs(first, second);

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == 1);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == 0);
    extractpdf_drop_form(form);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(document);
}

static void expect_format(const char *filename, uint32_t flags)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;
    int page_count = 0;

    sibling_fixture_path(filename, path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);
    CHECK(extractpdf_flatten_interactive(document, flags, &output) ==
        EXTRACTPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);
    extractpdf_close(document);
}

int extractpdf_test_pdf_flatten_noop(void)
{
    check_neutral_link_noop();
    check_zero_widget_noop();
    expect_format(
        "flatten-noop-malformed-annots.pdf",
        EXTRACTPDF_FLATTEN_ANNOTATIONS);
    expect_format(
        "flatten-noop-malformed-acroform.pdf",
        EXTRACTPDF_FLATTEN_WIDGETS);
    return 0;
}
