#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
    return test_state_transitions();
}
