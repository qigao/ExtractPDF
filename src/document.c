#include "internal.h"

#include <stdlib.h>

static void quantapdf_discard_log(void *user, const char *message)
{
    (void)user;
    (void)message;
}

static void quantapdf_dispose_document(quantapdf_document *document)
{
    if (document == NULL)
        return;

    if (document->doc != NULL)
        fz_drop_document(document->ctx, document->doc);
    if (document->ctx != NULL)
        fz_drop_context(document->ctx);
    free(document);
}

quantapdf_status quantapdf_status_from_mupdf(int code)
{
    switch (code) {
    case FZ_ERROR_ARGUMENT:
        return QUANTAPDF_ERROR_ARGUMENT;
    case FZ_ERROR_UNSUPPORTED:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    case FZ_ERROR_FORMAT:
    case FZ_ERROR_SYNTAX:
        return QUANTAPDF_ERROR_FORMAT;
    case FZ_ERROR_SYSTEM:
        return QUANTAPDF_ERROR_IO;
    default:
        return QUANTAPDF_ERROR_MUPDF;
    }
}

quantapdf_status quantapdf_open(
    const char *filename,
    const char *password,
    quantapdf_document **out_document)
{
    quantapdf_document *document;
    int password_ok = 1;
    int caught_code = FZ_ERROR_NONE;

    if (out_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = NULL;

    if (filename == NULL || filename[0] == '\0')
        return QUANTAPDF_ERROR_ARGUMENT;

    document = (quantapdf_document *)calloc(1, sizeof(*document));
    if (document == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    document->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (document->ctx == NULL) {
        free(document);
        return QUANTAPDF_ERROR_NOMEM;
    }

    fz_set_error_callback(document->ctx, quantapdf_discard_log, NULL);
    fz_set_warning_callback(document->ctx, quantapdf_discard_log, NULL);

    fz_var(password_ok);
    fz_var(caught_code);

    fz_try(document->ctx)
    {
        fz_register_document_handlers(document->ctx);
        document->doc = fz_open_document(document->ctx, filename);
        if (fz_needs_password(document->ctx, document->doc))
            password_ok = fz_authenticate_password(
                document->ctx,
                document->doc,
                password != NULL ? password : "");
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
        fz_report_error(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        quantapdf_status status = quantapdf_status_from_mupdf(caught_code);
        quantapdf_dispose_document(document);
        return status;
    }

    if (!password_ok) {
        quantapdf_dispose_document(document);
        return QUANTAPDF_ERROR_PASSWORD;
    }

    *out_document = document;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_count(
    quantapdf_document *document,
    int *out_page_count)
{
    int count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (document == NULL || out_page_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(count);
    fz_var(caught_code);

    fz_try(document->ctx)
    {
        count = fz_count_pages(document->ctx, document->doc);
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
        fz_report_error(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);

    *out_page_count = count;
    return QUANTAPDF_OK;
}

void quantapdf_close(quantapdf_document *document)
{
    quantapdf_dispose_document(document);
}
