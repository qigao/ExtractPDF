#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pdf_security_create_fixture(
    const char *source_path,
    const char *output_path,
    const char *scenario);

static int failures;
static const char *current_case = "startup";

static void begin_case(const char *name)
{
    current_case = name;
    printf("CASE %s\n", name);
}

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(
            stderr, "%s:%d: [%s] check failed: %s\n",
            __FILE__, line, current_case, expression);
        ++failures;
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int fixture_path(
    const char *scenario,
    char *path,
    size_t size)
{
    int written = snprintf(
        path, size, "%s/pdf-security-%s.pdf",
        SECURITY_FIXTURE_DIR, scenario);
    CHECK(written > 0 && (size_t)written < size);
    return written > 0 && (size_t)written < size;
}

static int create_fixture(
    const char *scenario,
    char *path,
    size_t size)
{
    int created;
    if (!fixture_path(scenario, path, size))
        return 0;
    created = pdf_security_create_fixture(SECURITY_PDF, path, scenario);
    CHECK(created);
    return created;
}

static void expect_document_audit(
    const char *path,
    const char *password,
    quantapdf_status expected_status,
    uint32_t expected_findings)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_status open_status = quantapdf_open(path, password, &document);
    quantapdf_status status;

    CHECK(open_status == QUANTAPDF_OK);
    if (open_status != QUANTAPDF_OK)
        return;
    audit.struct_size = QUANTAPDF_AUDIT_RESULT_V1_SIZE;
    audit.findings = UINT32_MAX;
    status = quantapdf_document_audit(document, &audit);
    if (status != expected_status) {
        fprintf(
            stderr,
            "[%s] audit status: expected %d, got %d\n",
            current_case, (int)expected_status, (int)status);
    }
    if (audit.findings != expected_findings) {
        fprintf(
            stderr,
            "[%s] audit findings: expected 0x%08x, got 0x%08x\n",
            current_case, expected_findings, audit.findings);
    }
    CHECK(status == expected_status);
    CHECK(audit.findings == expected_findings);
    quantapdf_close(document);
}

static void expect_fixture(
    const char *scenario,
    quantapdf_status expected_status,
    uint32_t expected_findings)
{
    char path[512];
    begin_case(scenario);
    if (!create_fixture(scenario, path, sizeof(path)))
        return;
    expect_document_audit(
        path, NULL, expected_status, expected_findings);
}

static void test_public_contract(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    begin_case("public_contract");
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

static void test_valid_audit_matrix(void)
{
    static const struct audit_case {
        const char *scenario;
        uint32_t findings;
    } cases[] = {
        {"internal_goto", 0},
        {"open_destination_array", 0},
        {"open_destination_name", 0},
        {"open_destination_string", 0},
        {"action_javascript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"names_javascript", QUANTAPDF_AUDIT_JAVASCRIPT_ACTION},
        {"action_launch", QUANTAPDF_AUDIT_LAUNCH_ACTION},
        {"external_uri", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_gotor", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_gotoe", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_submitform", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"external_importdata", QUANTAPDF_AUDIT_EXTERNAL_ACTION},
        {"other_unknown", QUANTAPDF_AUDIT_OTHER_ACTION},
        {"goto_next_other", QUANTAPDF_AUDIT_OTHER_ACTION},
        {"names_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"af_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"ef_embedded", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"embedded_stream", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"file_attachment", QUANTAPDF_AUDIT_EMBEDDED_FILE},
        {"xfa", QUANTAPDF_AUDIT_XFA},
        {"rich_richmedia", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_3d", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_movie", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_sound", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"rich_screen", QUANTAPDF_AUDIT_RICH_MEDIA},
        {"inherited_signature", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_docmdp", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_ur", QUANTAPDF_AUDIT_SIGNATURE},
        {"perms_ur3", QUANTAPDF_AUDIT_SIGNATURE},
        {"unreachable_javascript", 0},
        {"nonroot_names", 0},
        {"nonroot_acroform", 0},
        {"action_cycle", 0}
    };
    size_t index;

    begin_case("clean");
    expect_document_audit(SECURITY_PDF, NULL, QUANTAPDF_OK, 0);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        expect_fixture(cases[index].scenario, QUANTAPDF_OK,
                       cases[index].findings);

    begin_case("signed_fixture");
    expect_document_audit(
        SECURITY_SIGNED_PDF, NULL, QUANTAPDF_OK,
        QUANTAPDF_AUDIT_SIGNATURE);
    begin_case("encrypted_fixture");
    expect_document_audit(
        SECURITY_ENCRYPTED_PDF, "user-pass", QUANTAPDF_OK,
        QUANTAPDF_AUDIT_ENCRYPTION);
}

static void test_malformed_and_budget_matrix(void)
{
    static const char *malformed[] = {
        "malformed_open_action",
        "malformed_a",
        "malformed_aa_container",
        "malformed_aa_entry",
        "malformed_next",
        "malformed_names",
        "malformed_annots",
        "malformed_annot_entry",
        "malformed_acroform",
        "malformed_fields",
        "malformed_field",
        "malformed_signature_value",
        "malformed_signature_type",
        "parent_mismatch",
        "malformed_perms",
        "malformed_perms_signature",
        "malformed_perms_type"
    };
    static const char *budget[] = {
        "budget_shared_next",
        "budget_shared_aa",
        "budget_shared_annots"
    };
    size_t index;

    for (index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index)
        expect_fixture(malformed[index], QUANTAPDF_ERROR_FORMAT, 0);
    for (index = 0; index < sizeof(budget) / sizeof(budget[0]); ++index)
        expect_fixture(budget[index], QUANTAPDF_ERROR_UNSUPPORTED, 0);
}

static void test_extended_result_and_repeatability(void)
{
    struct extended_audit_result {
        quantapdf_audit_result v1;
        unsigned char suffix[16];
    } extended;
    unsigned char expected_suffix[sizeof(extended.suffix)];
    quantapdf_audit_result first = {0};
    quantapdf_audit_result second = {0};
    quantapdf_document *document = NULL;
    quantapdf_status status;
    char path[512];

    begin_case("extended_result");
    memset(&extended, 0xa5, sizeof(extended));
    memcpy(expected_suffix, extended.suffix, sizeof(expected_suffix));
    extended.v1.struct_size = sizeof(extended);
    extended.v1.findings = UINT32_MAX;
    status = quantapdf_open(SECURITY_PDF, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status == QUANTAPDF_OK) {
        CHECK(quantapdf_document_audit(document, &extended.v1) ==
              QUANTAPDF_OK);
        CHECK(extended.v1.struct_size == sizeof(extended));
        CHECK(extended.v1.findings == 0);
        CHECK(memcmp(
                  extended.suffix, expected_suffix,
                  sizeof(extended.suffix)) == 0);
        quantapdf_close(document);
    }

    begin_case("repeatability");
    document = NULL;
    if (!create_fixture("action_javascript", path, sizeof(path)))
        return;
    status = quantapdf_open(path, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status != QUANTAPDF_OK)
        return;
    first.struct_size = sizeof(first);
    second.struct_size = sizeof(second);
    CHECK(quantapdf_document_audit(document, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_document_audit(document, &second) == QUANTAPDF_OK);
    CHECK(first.findings == QUANTAPDF_AUDIT_JAVASCRIPT_ACTION);
    CHECK(second.findings == first.findings);
    quantapdf_close(document);
}

static void test_sanitize_stays_unsupported(void)
{
    quantapdf_audit_result audit = {0};
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    quantapdf_status status;

    begin_case("sanitize_placeholder");
    status = quantapdf_open(SECURITY_PDF, NULL, &document);
    CHECK(status == QUANTAPDF_OK);
    if (status != QUANTAPDF_OK)
        return;
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
    test_valid_audit_matrix();
    test_malformed_and_budget_matrix();
    test_extended_result_and_repeatability();
    test_sanitize_stays_unsupported();
    if (failures != 0)
        fprintf(stderr, "pdf_security: %d checks failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
