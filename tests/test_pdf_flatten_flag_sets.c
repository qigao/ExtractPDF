#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int check_repeated_bytes(const char *path, uint32_t flags)
{
    extractpdf_document *document = NULL;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    const unsigned char *first_bytes = NULL;
    const unsigned char *second_bytes = NULL;
    size_t first_size = 0;
    size_t second_size = 0;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(extractpdf_flatten_interactive(document, flags, &first) ==
        EXTRACTPDF_OK);
    CHECK(first != NULL);
    CHECK(extractpdf_flatten_interactive(document, flags, &second) ==
        EXTRACTPDF_OK);
    CHECK(second != NULL);
    CHECK(extractpdf_output_data(first, &first_bytes, &first_size) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(second, &second_bytes, &second_size) ==
        EXTRACTPDF_OK);
    CHECK(first_bytes != NULL);
    CHECK(second_bytes != NULL);
    CHECK(first_size != 0);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_bytes, second_bytes, first_size) == 0);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(document);
    return 0;
}

int extractpdf_test_pdf_flatten_flag_sets(void)
{
    if (check_repeated_bytes(
            FLATTEN_CONTENTS_PDF,
            EXTRACTPDF_FLATTEN_ANNOTATIONS) != 0)
        return 1;
    return check_repeated_bytes(
        FLATTEN_WIDGET_AS_PDF,
        EXTRACTPDF_FLATTEN_WIDGETS);
}
