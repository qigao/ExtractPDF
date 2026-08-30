#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

int extractpdf_test_pdf_flatten_policy(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;

    CHECK(extractpdf_open(
        FLATTEN_POLICY_LINK_DEFAULT_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);

    extractpdf_close(document);
    return 0;
}
