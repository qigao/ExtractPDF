#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int expect_flatten_status(
    const char *path,
    uint32_t flags,
    extractpdf_status expected)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(extractpdf_flatten_interactive(
        document,
        flags,
        &output) == expected);
    if (expected == EXTRACTPDF_OK) {
        CHECK(output != NULL);
        extractpdf_drop_output(output);
    } else {
        CHECK(output == NULL);
    }
    extractpdf_close(document);
    return 0;
}

int extractpdf_test_pdf_flatten_policy(void)
{
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_DEFAULT_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BS0_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_OK) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BORDER0_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_OK) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BS0_BORDER4_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_OK) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BS4_BORDER0_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BS_MISSING_W_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_APN_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_LINK_BS_MALFORMED_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_FORMAT) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_ANNOTATION_WITH_WIDGET_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_ANNOTATION_WITH_WIDGET_PDF,
        EXTRACTPDF_FLATTEN_WIDGETS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_POPUP_PARENT_REVERSE_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_POPUP_DIRECT_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_POLICY_IRT_REVERSE_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_TAGGED_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_ERROR_UNSUPPORTED) == 0);
    CHECK(expect_flatten_status(
        FLATTEN_TAGGED_NOOP_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        EXTRACTPDF_OK) == 0);
    return 0;
}
