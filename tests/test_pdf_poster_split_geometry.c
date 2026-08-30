#include "test_pdf_poster_split_internal.h"

#include <extractpdf/extractpdf.h>

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

static void run_changed_case(const char *path, size_t columns, size_t rows)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    extractpdf_page_poster_split split;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = columns;
    split.rows = rows;
    CHECK(extractpdf_poster_split_pages(document, &split, 1, &output) ==
          EXTRACTPDF_OK);
    CHECK(output != NULL);
    extractpdf_drop_output(output);
    extractpdf_close(document);
}

int poster_run_geometry_tests(void)
{
    run_changed_case(POSTER_ROTATE_90_PDF, 2, 2);
    run_changed_case(POSTER_USERUNIT_PDF, 2, 2);
    return 0;
}
