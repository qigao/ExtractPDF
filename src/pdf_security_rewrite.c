#include "internal.h"
#include "backend/qpdf_document.h"

#include <stdlib.h>

typedef enum quantapdf_security_operation_internal {
    QUANTAPDF_SECURITY_ENCRYPT_INTERNAL = 1,
    QUANTAPDF_SECURITY_DECRYPT_INTERNAL = 2,
    QUANTAPDF_SECURITY_REENCRYPT_INTERNAL = 3
} quantapdf_security_operation_internal;

static quantapdf_status quantapdf_validate_encryption_options(
    const quantapdf_encryption_options *options)
{
    if (options == NULL ||
        options->struct_size < QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE ||
        options->method != QUANTAPDF_ENCRYPTION_AES_256 ||
        options->user_password_utf8 == NULL ||
        options->owner_password_utf8 == NULL ||
        (options->permissions & ~QUANTAPDF_PERMISSION_ALL) != 0u ||
        (options->encrypt_metadata != 0 && options->encrypt_metadata != 1) ||
        ((options->permissions &
          QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY) != 0u &&
         (options->permissions &
          QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION) == 0u))
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_security_rewrite(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_security_operation_internal operation,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->qpdf_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (operation != QUANTAPDF_SECURITY_DECRYPT_INTERNAL) {
        status = quantapdf_validate_encryption_options(options);
        if (status != QUANTAPDF_OK)
            return status;
    }

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    if (operation == QUANTAPDF_SECURITY_ENCRYPT_INTERNAL) {
        status = quantapdf_qpdf_encrypt_pdf(
            document->qpdf_document, options,
            &output->data, &output->size);
    } else if (operation == QUANTAPDF_SECURITY_DECRYPT_INTERNAL) {
        status = quantapdf_qpdf_decrypt_pdf(
            document->qpdf_document, &output->data, &output->size);
    } else {
        status = quantapdf_qpdf_reencrypt_pdf(
            document->qpdf_document, options,
            &output->data, &output->size);
    }
    if (status != QUANTAPDF_OK) {
        free(output->data);
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_encrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, options, QUANTAPDF_SECURITY_ENCRYPT_INTERNAL, out_output);
}

quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, NULL, QUANTAPDF_SECURITY_DECRYPT_INTERNAL, out_output);
}

quantapdf_status quantapdf_reencrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, options, QUANTAPDF_SECURITY_REENCRYPT_INTERNAL, out_output);
}
