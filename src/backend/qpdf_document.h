#ifndef QUANTAPDF_BACKEND_QPDF_DOCUMENT_H
#define QUANTAPDF_BACKEND_QPDF_DOCUMENT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;

quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document);

quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count);

void quantapdf_qpdf_close(quantapdf_qpdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
