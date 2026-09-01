#include "internal.h"

static quantapdf_status quantapdf_security_rewrite_stub(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    int options_required,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL ||
        (options_required &&
         (options == NULL || options->user_password_utf8 == NULL ||
          options->owner_password_utf8 == NULL)))
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

quantapdf_status quantapdf_encrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite_stub(
        document, options, 1, out_output);
}

quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite_stub(
        document, NULL, 0, out_output);
}

quantapdf_status quantapdf_reencrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite_stub(
        document, options, 1, out_output);
}
