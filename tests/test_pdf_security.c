#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum pdf_security_fixture_kind {
    PDF_SECURITY_FIXTURE_INTERNAL_GOTO = 1,
    PDF_SECURITY_FIXTURE_JAVASCRIPT = 2,
    PDF_SECURITY_FIXTURE_LAUNCH = 3,
    PDF_SECURITY_FIXTURE_EXTERNAL = 4,
    PDF_SECURITY_FIXTURE_OTHER = 5,
    PDF_SECURITY_FIXTURE_EMBEDDED = 6,
    PDF_SECURITY_FIXTURE_XFA = 7,
    PDF_SECURITY_FIXTURE_RICH_MEDIA = 8,
    PDF_SECURITY_FIXTURE_UNREACHABLE = 9,
    PDF_SECURITY_FIXTURE_MALFORMED_A = 10,
    PDF_SECURITY_FIXTURE_MALFORMED_AA = 11,
    PDF_SECURITY_FIXTURE_MALFORMED_NAMES = 12,
    PDF_SECURITY_FIXTURE_MALFORMED_ANNOTS = 13,
    PDF_SECURITY_FIXTURE_MALFORMED_ANNOT_ENTRY = 14
};

int pdf_security_create_fixture(
    const char *source_path,
    const char *output_path,
    int kind);

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void fixture_path(int kind, char *path, size_t size)
{
    int written = snprintf(
        path, size, "%s/pdf-security-%d.pdf", SECURITY_FIXTURE_DIR, kind);
    CHECK(written > 0 && (size_t)written < size);
}

static void create_fixture(int kind, char *path, size_t size)
{
    fixture_path(kind, path, size);
    CHECK(pdf_security_create_fixture(SECURITY_PDF, path, kind));
}

static uint32_t audit_path(const char *path, const char *password)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(path, password, &document) == QUANTAPDF_OK);
    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_SIZE;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(document, &audit) == QUANTAPDF_OK);
    quantapdf_close(document);
    return audit.findings;
}

static void expect_audit_error(const char *path, quantapdf_status expected)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_SIZE;
    audit.findings = UINT32_MAX;
    CHECK(quantapdf_document_audit(document, &audit) == expected);
    CHECK(audit.findings == 0);
    quantapdf_close(document);
}

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

static void test_clean_internal_and_extended_result(void)
{
    struct extended_audit_result {
        quantapdf_audit_result v1;
        unsigned char suffix[16];
    } audit;
    unsigned char expected_suffix[sizeof(audit.suffix)];
    quantapdf_document *document = NULL;
    char path[512];

    CHECK(audit_path(SECURITY_PDF, NULL) == 0);
    create_fixture(PDF_SECURITY_FIXTURE_INTERNAL_GOTO, path, sizeof(path));
    CHECK(audit_path(path, NULL) == 0);

    memset(&audit, 0xa5, sizeof(audit));
    memcpy(expected_suffix, audit.suffix, sizeof(expected_suffix));
    audit.v1.struct_size = sizeof(audit);
    audit.v1.findings = UINT32_MAX;
    CHECK(quantapdf_open(SECURITY_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_audit(document, &audit.v1) == QUANTAPDF_OK);
    CHECK(audit.v1.struct_size == sizeof(audit));
    CHECK(audit.v1.findings == 0);
    CHECK(memcmp(audit.suffix, expected_suffix, sizeof(audit.suffix)) == 0);
    quantapdf_close(document);
}

static void test_isolated_findings_and_repeatability(void)
{
    static const struct {
        int kind;
        uint32_t finding;
    } cases[] = {
        {PDF_SECURITY_FIXTURE_JAVASCRIPT,
         QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {PDF_SECURITY_FIXTURE_LAUNCH, QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {PDF_SECURITY_FIXTURE_EXTERNAL, QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {PDF_SECURITY_FIXTURE_OTHER, QUANTAPDF_AUDIT_OTHER_ACTION},
        {PDF_SECURITY_FIXTURE_EMBEDDED, QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {PDF_SECURITY_FIXTURE_XFA, QUANTAPDF_AUDIT_XFA},
        {PDF_SECURITY_FIXTURE_RICH_MEDIA, QUANTAPDF_AUDIT_RICH_MEDIA}
    };
    quantapdf_audit_result first = {0};
    quantapdf_audit_result second = {0};
    quantapdf_document *document = NULL;
    size_t index;
    char path[512];

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        create_fixture(cases[index].kind, path, sizeof(path));
        CHECK(audit_path(path, NULL) == cases[index].finding);
    }
    CHECK(audit_path(SECURITY_SIGNED_PDF, NULL) ==
          QUANTAPDF_AUDIT_SIGNATURE);
    CHECK(audit_path(SECURITY_ENCRYPTED_PDF, "user-pass") ==
          QUANTAPDF_AUDIT_ENCRYPTION);

    create_fixture(PDF_SECURITY_FIXTURE_JAVASCRIPT, path, sizeof(path));
    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    first.struct_size = sizeof(first);
    second.struct_size = sizeof(second);
    CHECK(quantapdf_document_audit(document, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_document_audit(document, &second) == QUANTAPDF_OK);
    CHECK(first.findings == QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    CHECK(second.findings == first.findings);
    quantapdf_close(document);
}

static void test_reachability_and_malformed_inputs(void)
{
    static const int malformed[] = {
        PDF_SECURITY_FIXTURE_MALFORMED_A,
        PDF_SECURITY_FIXTURE_MALFORMED_AA,
        PDF_SECURITY_FIXTURE_MALFORMED_NAMES,
        PDF_SECURITY_FIXTURE_MALFORMED_ANNOTS,
        PDF_SECURITY_FIXTURE_MALFORMED_ANNOT_ENTRY
    };
    size_t index;
    char path[512];

    create_fixture(PDF_SECURITY_FIXTURE_UNREACHABLE, path, sizeof(path));
    CHECK(audit_path(path, NULL) == 0);
    for (index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index) {
        create_fixture(malformed[index], path, sizeof(path));
        expect_audit_error(path, QUANTAPDF_ERROR_FORMAT);
    }
}

static void test_sanitize_stays_unsupported(void)
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
    test_clean_internal_and_extended_result();
    test_isolated_findings_and_repeatability();
    test_reachability_and_malformed_inputs();
    test_sanitize_stays_unsupported();
    return EXIT_SUCCESS;
}
