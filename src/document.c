#include "internal.h"

#include <stdlib.h>

static void extractpdf_dispose_document(extractpdf_document *document)
{
    if (document == NULL)
        return;

    if (document->doc != NULL)
        fz_drop_document(document->ctx, document->doc);
    if (document->ctx != NULL)
        fz_drop_context(document->ctx);
    free(document);
}

extractpdf_status extractpdf_status_from_mupdf(int code)
{
    (void)code;
    return EXTRACTPDF_ERROR_MUPDF;
}

extractpdf_status extractpdf_open(
    const char *filename,
    const char *password,
    extractpdf_document **out_document)
{
    extractpdf_document *document;
    int password_ok = 1;
    int caught_code = FZ_ERROR_NONE;

    if (out_document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_document = NULL;

    if (filename == NULL || filename[0] == '\0')
        return EXTRACTPDF_ERROR_ARGUMENT;

    document = (extractpdf_document *)calloc(1, sizeof(*document));
    if (document == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    document->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (document->ctx == NULL) {
        free(document);
        return EXTRACTPDF_ERROR_NOMEM;
    }

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
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_status status = extractpdf_status_from_mupdf(caught_code);
        extractpdf_dispose_document(document);
        return status;
    }

    if (!password_ok) {
        extractpdf_dispose_document(document);
        return EXTRACTPDF_ERROR_PASSWORD;
    }

    *out_document = document;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_page_count(
    extractpdf_document *document,
    int *out_page_count)
{
    int count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (document == NULL || out_page_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(count);
    fz_var(caught_code);

    fz_try(document->ctx)
    {
        count = fz_count_pages(document->ctx, document->doc);
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);

    *out_page_count = count;
    return EXTRACTPDF_OK;
}

void extractpdf_close(extractpdf_document *document)
{
    extractpdf_dispose_document(document);
}
