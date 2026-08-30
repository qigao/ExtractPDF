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
    extractpdf_output *sentinel = (extractpdf_output *)(uintptr_t)1;

    CHECK(extractpdf_open(FLATTEN_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);

    CHECK(extractpdf_flatten_interactive(
        NULL,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &sentinel) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    sentinel = (extractpdf_output *)(uintptr_t)1;
    CHECK(extractpdf_flatten_interactive(document, 0, &sentinel) ==
        EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    sentinel = (extractpdf_output *)(uintptr_t)1;
    CHECK(extractpdf_flatten_interactive(document, 1u << 31, &sentinel) ==
        EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
