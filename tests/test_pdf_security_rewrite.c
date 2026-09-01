#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
    return 0;
}
