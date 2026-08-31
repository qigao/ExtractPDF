#include "internal.h"

#include "backend/pdfium_document.h"
#include "backend/qpdf_document.h"
#include "input_file.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    quantapdf_pdfium_close(document->pdfium_document);
    quantapdf_qpdf_close(document->qpdf_document);
    free(document->source_data);
    if (document->password != NULL) {
        volatile char *cursor = document->password;
        while (*cursor != '\0') {
            *cursor = '\0';
            ++cursor;
        }
        free(document->password);
    }
    free(document);
}

static quantapdf_status quantapdf_copy_password(
    const char *password,
    char **out_password)
{
    const char *source = password != NULL ? password : "";
    size_t size = strlen(source);
    char *copy;

    if (size == SIZE_MAX)
        return QUANTAPDF_ERROR_NOMEM;
    copy = (char *)malloc(size + 1);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(copy, source, size + 1);
    *out_password = copy;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_status_from_backend(int code)
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
        return QUANTAPDF_ERROR_BACKEND;
    }
}

quantapdf_status quantapdf_open(
    const char *filename,
    const char *password,
    quantapdf_document **out_document)
{
    quantapdf_document *document;
    quantapdf_status status;
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

    status = quantapdf_copy_password(password, &document->password);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }

    status = quantapdf_read_file(
        filename, &document->source_data, &document->source_size);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }
    status = quantapdf_pdfium_open_memory(
        document->source_data,
        document->source_size,
        password,
        &document->pdfium_document);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }

    document->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (document->ctx == NULL) {
        quantapdf_dispose_document(document);
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
        quantapdf_status backend_status =
            quantapdf_status_from_backend(caught_code);
        quantapdf_dispose_document(document);
        return backend_status;
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
    if (document == NULL || out_page_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return quantapdf_pdfium_page_count(
        document->pdfium_document, out_page_count);
}

quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit)
{
    quantapdf_status status;

    if (document == NULL || out_user_unit == NULL || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (document->qpdf_document == NULL) {
        status = quantapdf_qpdf_open_memory(
            document->source_data,
            document->source_size,
            document->password,
            &document->qpdf_document);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return quantapdf_qpdf_page_user_unit(
        document->qpdf_document, page_index, out_user_unit);
}

void quantapdf_close(quantapdf_document *document)
{
    quantapdf_dispose_document(document);
}
