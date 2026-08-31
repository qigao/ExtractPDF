#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void test_public_contract(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(QUANTAPDF_AUDIT_JAVASCRIPT_ACTION == (1u << 0));
    CHECK(QUANTAPDF_AUDIT_LAUNCH_ACTION == (1u << 1));
    CHECK(QUANTAPDF_AUDIT_EXTERNAL_ACTION == (1u << 2));
    CHECK(QUANTAPDF_AUDIT_OTHER_ACTION == (1u << 3));
    CHECK(QUANTAPDF_AUDIT_EMBEDDED_FILE == (1u << 4));
    CHECK(QUANTAPDF_AUDIT_XFA == (1u << 5));
    CHECK(QUANTAPDF_AUDIT_RICH_MEDIA == (1u << 6));
    CHECK(QUANTAPDF_AUDIT_SIGNATURE == (1u << 7));
    CHECK(QUANTAPDF_AUDIT_ENCRYPTION == (1u << 8));
    CHECK(QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS == (1u << 0));
    CHECK(QUANTAPDF_SANITIZE_LAUNCH_ACTIONS == (1u << 1));
    CHECK(QUANTAPDF_SANITIZE_EXTERNAL_ACTIONS == (1u << 2));
    CHECK(QUANTAPDF_SANITIZE_OTHER_ACTIONS == (1u << 3));
    CHECK(QUANTAPDF_SANITIZE_EMBEDDED_FILES == (1u << 4));
    CHECK(QUANTAPDF_SANITIZE_XFA == (1u << 5));
    CHECK(QUANTAPDF_SANITIZE_RICH_MEDIA == (1u << 6));
    CHECK(QUANTAPDF_SANITIZE_ALL == ((1u << 7) - 1u));
    CHECK(QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE ==
          offsetof(quantapdf_audit_result, findings) + sizeof(audit.findings));
    CHECK(QUANTAPDF_AUDIT_RESULT_V1_SIZE == sizeof(audit));

    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_SIZE;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(NULL, &audit) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(audit.findings == 0);
    CHECK(quantapdf_document_audit(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE - 1u;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(NULL, &audit) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(audit.findings == UINT32_MAX);

    CHECK(quantapdf_sanitize(NULL, QUANTAPDF_SANITIZE_ALL, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_sanitize(NULL, QUANTAPDF_SANITIZE_ALL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
}

static void test_placeholder_contract(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_open(SECURITY_PDF, NULL, &document) == QUANTAPDF_OK);

    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE - 1u;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(document, &audit) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(audit.findings == UINT32_MAX);

    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_SIZE;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(document, &audit) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(audit.findings == 0);

    CHECK(quantapdf_sanitize(document, 0, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);

    output = (quantapdf_output *)(uintptr_t)1;
    CHECK(quantapdf_sanitize(document, UINT32_C(1) << 31, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);

    output = (quantapdf_output *)(uintptr_t)1;
    CHECK(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_ALL, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    quantapdf_close(document);
}

int main(void)
{
    test_public_contract();
    test_placeholder_contract();
    return EXIT_SUCCESS;
}
