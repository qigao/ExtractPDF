#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void expect_value(
    extractpdf_document *document,
    extractpdf_metadata_field field,
    const char *expected,
    size_t expected_size)
{
    char *value = (char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(extractpdf_document_metadata(document, field, &value, &size) ==
          EXTRACTPDF_OK);
    CHECK(value != NULL);
    CHECK(size == expected_size);
    CHECK(memcmp(value, expected, expected_size) == 0);
    CHECK(value[expected_size] == '\0');
    extractpdf_free(value);
}

static void expect_missing(
    extractpdf_document *document,
    extractpdf_metadata_field field)
{
    char *value = (char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(extractpdf_document_metadata(document, field, &value, &size) ==
          EXTRACTPDF_OK);
    CHECK(value == NULL);
    CHECK(size == 0);
}

int main(void)
{
    static const char title[] = "Phase 5 Caf\xC3\xA9";
    static const char creation[] = "D:20260828123456+09'00'";
    static const char modification[] = "D:20260828124500+09'00'";
    extractpdf_document *document = NULL;
    extractpdf_document *non_pdf = NULL;
    char *value;
    size_t size;

    CHECK(extractpdf_open(METADATA_PDF, NULL, &document) == EXTRACTPDF_OK);

    expect_value(document, EXTRACTPDF_METADATA_TITLE, title, sizeof(title) - 1);
    CHECK(sizeof(title) - 1 == 13);
    expect_value(document, EXTRACTPDF_METADATA_AUTHOR,
                 "ExtractPDF Test", sizeof("ExtractPDF Test") - 1);
    expect_value(document, EXTRACTPDF_METADATA_CREATOR,
                 "Metadata Fixture", sizeof("Metadata Fixture") - 1);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(document, EXTRACTPDF_METADATA_SUBJECT,
                                       &value, &size) == EXTRACTPDF_OK);
    CHECK(value != NULL);
    CHECK(size == 0);
    CHECK(value[0] == '\0');
    extractpdf_free(value);

    expect_missing(document, EXTRACTPDF_METADATA_KEYWORDS);
    expect_value(document, EXTRACTPDF_METADATA_CREATION_DATE,
                 creation, sizeof(creation) - 1);
    expect_value(document, EXTRACTPDF_METADATA_MODIFICATION_DATE,
                 modification, sizeof(modification) - 1);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(document, EXTRACTPDF_METADATA_PRODUCER,
                                       &value, &size) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(document, (extractpdf_metadata_field)0,
                                       &value, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(document, (extractpdf_metadata_field)99,
                                       &value, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(NULL, EXTRACTPDF_METADATA_TITLE,
                                       &value, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);
    CHECK(size == 0);

    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(document, EXTRACTPDF_METADATA_TITLE,
                                       NULL, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(size == 0);

    value = (char *)(uintptr_t)1;
    CHECK(extractpdf_document_metadata(document, EXTRACTPDF_METADATA_TITLE,
                                       &value, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(value == NULL);

    CHECK(extractpdf_open(COMPOSITION_NON_PDF, NULL, &non_pdf) == EXTRACTPDF_OK);
    value = (char *)(uintptr_t)1;
    size = (size_t)-1;
    CHECK(extractpdf_document_metadata(non_pdf, EXTRACTPDF_METADATA_TITLE,
                                       &value, &size) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(value == NULL);
    CHECK(size == 0);
    extractpdf_close(non_pdf);

    value = NULL;
    size = 0;
    CHECK(extractpdf_document_metadata(document, EXTRACTPDF_METADATA_TITLE,
                                       &value, &size) == EXTRACTPDF_OK);
    CHECK(value != NULL);
    CHECK(size == sizeof(title) - 1);
    extractpdf_close(document);
    CHECK(memcmp(value, title, sizeof(title) - 1) == 0);
    CHECK(value[sizeof(title) - 1] == '\0');
    extractpdf_free(value);

    return EXIT_SUCCESS;
}
