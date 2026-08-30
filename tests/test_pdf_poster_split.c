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

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    extractpdf_page_poster_split split;

    CHECK(extractpdf_open(POSTER_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);

    split.struct_size = sizeof(split);
    split.page_index = 1;
    split.columns = 2;
    split.rows = 2;

    CHECK(extractpdf_poster_split_pages(document, &split, 1, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
