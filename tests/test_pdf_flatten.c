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

    CHECK(extractpdf_open(FLATTEN_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
