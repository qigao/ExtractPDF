#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "check failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

typedef struct quantapdf_security_inspection {
    int encrypted;
    int revision;
    int version;
    int stream_aesv3;
    int string_aesv3;
    int file_aesv3;
    int extension_level;
    int encrypt_metadata;
    int allow_accessibility;
    int allow_copy;
    int allow_assemble;
    int allow_fill_forms;
    int allow_annotate_and_fill_forms;
    int allow_modify_other;
    int allow_print_low_resolution;
    int allow_print_high_quality;
} quantapdf_security_inspection;

int quantapdf_security_inspect_pdf(
    const unsigned char *data,
    size_t size,
    const char *password,
    quantapdf_security_inspection *out);

_Static_assert(
    QUANTAPDF_ENCRYPTION_AES_256 == 1,
    "stable encryption method value");
_Static_assert(
    QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION == (1u << 0),
    "stable low-resolution print permission");
_Static_assert(
    QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY == (1u << 6),
    "stable high-quality print permission");
_Static_assert(
    QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE ==
        sizeof(quantapdf_encryption_options),
    "V1 options prefix must be complete");

static void compile_public_surface(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    quantapdf_encryption_options options = {0};

    if (0) {
        (void)quantapdf_encrypt_pdf(document, &options, &output);
        (void)quantapdf_decrypt_pdf(document, &output);
        (void)quantapdf_reencrypt_pdf(document, &options, &output);
    }
}

static int inspect_output(
    const quantapdf_output *output,
    const char *password,
    quantapdf_security_inspection *inspection)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    return quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK &&
        quantapdf_security_inspect_pdf(data, size, password, inspection);
}

static int test_state_transitions(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_document *authenticated = NULL;
    quantapdf_output *encrypted = NULL;
    quantapdf_output *decrypted = NULL;
    quantapdf_output *reencrypted = NULL;
    quantapdf_output *sentinel = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_encryption_options options = {
        QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE,
        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        QUANTAPDF_PERMISSION_ALL,
        1};
    quantapdf_encryption_options replacement = options;
    quantapdf_security_inspection inspection = {0};

    replacement.user_password_utf8 = "new-user";
    replacement.owner_password_utf8 = "new-owner";

    CHECK(quantapdf_open(SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &encrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(encrypted, "user", &inspection));
    CHECK(inspection.encrypted && inspection.revision == 6 &&
          inspection.version == 5 && inspection.stream_aesv3 &&
          inspection.string_aesv3 && inspection.file_aesv3);
    CHECK(inspection.extension_level == 8);
    CHECK(quantapdf_output_save_file(encrypted, SECURITY_ENCRYPTED_OUTPUT) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(
              SECURITY_ENCRYPTED_OUTPUT, "user", &authenticated) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_decrypt_pdf(authenticated, &decrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(decrypted, NULL, &inspection));
    CHECK(!inspection.encrypted);

    CHECK(quantapdf_reencrypt_pdf(
              authenticated, &replacement, &reencrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(reencrypted, "new-user", &inspection));
    CHECK(inspection.encrypted && inspection.revision == 6);

    CHECK(quantapdf_encrypt_pdf(authenticated, &options, &sentinel) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);
    sentinel = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_decrypt_pdf(plain, &sentinel) == QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);
    sentinel = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_reencrypt_pdf(plain, &options, &sentinel) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);

    quantapdf_drop_output(reencrypted);
    quantapdf_drop_output(decrypted);
    quantapdf_drop_output(encrypted);
    quantapdf_close(authenticated);
    quantapdf_close(plain);
    return 0;
}

static int expect_encrypt_status(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_status expected)
{
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_status const status =
        quantapdf_encrypt_pdf(document, options, &output);
    if (status != expected)
        return 0;
    if (status == QUANTAPDF_OK) {
        if (output == NULL)
            return 0;
        quantapdf_drop_output(output);
        return 1;
    }
    return output == NULL;
}

static int inspect_permissions(
    quantapdf_document *plain,
    uint32_t permissions,
    int encrypt_metadata,
    quantapdf_security_inspection *inspection)
{
    quantapdf_encryption_options options = {
        QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE,
        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        permissions,
        encrypt_metadata};
    quantapdf_output *output = NULL;
    int result;

    if (quantapdf_encrypt_pdf(plain, &options, &output) != QUANTAPDF_OK)
        return 0;
    result = inspect_output(output, "user", inspection);
    quantapdf_drop_output(output);
    return result;
}

static int test_validation_and_permissions(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_encryption_options options = {
        QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE,
        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    quantapdf_security_inspection inspection = {0};
    char password_127[128];
    char password_128[129];
    static const char control_password[] = {'a', '\n', 'b', 0};
    static const char del_password[] = {'a', 0x7f, 0};
    static const char non_ascii_password[] =
        {'m', (char)0xc3, (char)0xb6, 't', 0};
    struct extended_options {
        quantapdf_encryption_options options;
        uint64_t suffix;
    } extended = {0};

    memset(password_127, 'a', sizeof(password_127) - 1u);
    password_127[sizeof(password_127) - 1u] = '\0';
    memset(password_128, 'a', sizeof(password_128) - 1u);
    password_128[sizeof(password_128) - 1u] = '\0';
    CHECK(quantapdf_open(SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);

    CHECK(expect_encrypt_status(
        plain, NULL, QUANTAPDF_ERROR_ARGUMENT));
    options.struct_size = QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE - 1u;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.struct_size = QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE;
    options.method = (quantapdf_encryption_method)99;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.method = QUANTAPDF_ENCRYPTION_AES_256;
    options.permissions = (1u << 31);
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.permissions = QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.permissions = 0u;
    options.encrypt_metadata = -1;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.encrypt_metadata = 2;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.encrypt_metadata = 1;
    options.owner_password_utf8 = "";
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.owner_password_utf8 = "user";
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.owner_password_utf8 = "owner";
    options.user_password_utf8 = control_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = del_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = non_ascii_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = password_128;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = password_127;
    CHECK(expect_encrypt_status(plain, &options, QUANTAPDF_OK));
    options.user_password_utf8 = "";
    CHECK(expect_encrypt_status(plain, &options, QUANTAPDF_OK));

    extended.options = options;
    extended.options.struct_size = sizeof(extended);
    extended.suffix = UINT64_C(0x8877665544332211);
    CHECK(expect_encrypt_status(
        plain, &extended.options, QUANTAPDF_OK));
    CHECK(extended.suffix == UINT64_C(0x8877665544332211));

    CHECK(inspect_permissions(plain, 0u, 1, &inspection));
    CHECK(inspection.allow_accessibility && !inspection.allow_copy &&
          !inspection.allow_assemble && !inspection.allow_fill_forms &&
          !inspection.allow_annotate_and_fill_forms &&
          !inspection.allow_modify_other &&
          !inspection.allow_print_low_resolution &&
          !inspection.allow_print_high_quality &&
          inspection.encrypt_metadata);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_COPY, 0, &inspection));
    CHECK(inspection.allow_copy && !inspection.encrypt_metadata);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_ASSEMBLE, 1, &inspection));
    CHECK(inspection.allow_assemble);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_FILL_FORMS, 1, &inspection));
    CHECK(inspection.allow_fill_forms &&
          !inspection.allow_annotate_and_fill_forms);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS,
        1, &inspection));
    CHECK(!inspection.allow_fill_forms &&
          inspection.allow_annotate_and_fill_forms);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_MODIFY_OTHER, 1, &inspection));
    CHECK(inspection.allow_modify_other);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION, 1, &inspection));
    CHECK(inspection.allow_print_low_resolution &&
          !inspection.allow_print_high_quality);
    CHECK(inspect_permissions(
        plain,
        QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION |
            QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY,
        1,
        &inspection));
    CHECK(inspection.allow_print_low_resolution &&
          inspection.allow_print_high_quality);

    quantapdf_close(plain);
    return 0;
}

int main(void)
{
    quantapdf_output *sentinel = (quantapdf_output *)(uintptr_t)1u;

    compile_public_surface();
    if (quantapdf_decrypt_pdf(NULL, &sentinel) != QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "decrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_encrypt_pdf(NULL, NULL, &sentinel) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "encrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_reencrypt_pdf(NULL, NULL, &sentinel) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "reencrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_decrypt_pdf(NULL, NULL) != QUANTAPDF_ERROR_ARGUMENT) {
        fprintf(stderr, "decrypt accepted NULL output address\n");
        return 1;
    }
    CHECK(test_state_transitions() == 0);
    return test_validation_and_permissions();
}
