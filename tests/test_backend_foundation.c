#include <fpdfview.h>

#include "backend/pdfium_runtime.h"
#include "backend/qpdf_document.h"

#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static unsigned char *read_fixture(const char *path, size_t *out_size)
{
    FILE *file;
    long length;
    unsigned char *data;

    *out_size = 0;
#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0)
        return NULL;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
#endif
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)length;
    return data;
}

int main(void)
{
    unsigned char *data;
    size_t size;
    FPDF_DOCUMENT pdfium_document;
    quantapdf_qpdf_document *qpdf_document = NULL;
    int qpdf_page_count = 0;

    data = read_fixture(ONE_PAGE_PDF, &size);
    CHECK(data != NULL);

    CHECK(quantapdf_pdfium_enter() == QUANTAPDF_OK);
    pdfium_document = FPDF_LoadMemDocument64(data, size, NULL);
    CHECK(pdfium_document != NULL);
    CHECK(FPDF_GetPageCount(pdfium_document) == 1);
    FPDF_CloseDocument(pdfium_document);
    quantapdf_pdfium_leave();

    CHECK(quantapdf_qpdf_open_memory(
        data, size, NULL, &qpdf_document) == QUANTAPDF_OK);
    CHECK(qpdf_document != NULL);
    CHECK(quantapdf_qpdf_page_count(
        qpdf_document, &qpdf_page_count) == QUANTAPDF_OK);
    CHECK(qpdf_page_count == 1);
    quantapdf_qpdf_close(qpdf_document);

    free(data);
    fprintf(stderr, "[quantapdf.backend_foundation] complete\n");
    return EXIT_SUCCESS;
}
