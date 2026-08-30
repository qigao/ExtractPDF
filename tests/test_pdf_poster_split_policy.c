#include "test_pdf_poster_split_internal.h"

#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static void expect_policy_status(
    const char *path,
    size_t columns,
    size_t rows,
    extractpdf_status expected)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = output_sentinel();
    extractpdf_page_poster_split split;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = columns;
    split.rows = rows;
    CHECK(extractpdf_poster_split_pages(document, &split, 1, &output) == expected);
    if (expected == EXTRACTPDF_OK) {
        CHECK(output != NULL && output != output_sentinel());
        extractpdf_drop_output(output);
    } else {
        CHECK(output == NULL);
    }
    extractpdf_close(document);
}

int poster_run_policy_tests(void)
{
    expect_policy_status(
        POSTER_PRODUCTION_BOX_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_TAGGED_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_ROOT_POLICY_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_UNSELECTED_ACTIONS_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_FORM_ACTIONS_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_MALFORMED_ANNOTS_PDF, 2, 1, EXTRACTPDF_ERROR_FORMAT);
    expect_policy_status(
        POSTER_CROSSING_ANNOTATION_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_CROSSING_WIDGET_PDF, 2, 1, EXTRACTPDF_ERROR_UNSUPPORTED);

    /* Expansion-only restrictions do not apply to a pure semantic no-op. */
    expect_policy_status(POSTER_TAGGED_PDF, 1, 1, EXTRACTPDF_OK);
    return 0;
}
