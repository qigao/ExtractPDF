#include <extractpdf/extractpdf.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_OK), "ok") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_ARGUMENT), "invalid argument") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_IO), "I/O error") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_PASSWORD), "password required or invalid") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_FORMAT), "invalid document format") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_UNSUPPORTED), "unsupported operation or content") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_NOMEM), "out of memory") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_MUPDF), "MuPDF error") == 0);
    assert(strcmp(extractpdf_status_string((extractpdf_status)999), "unknown error") == 0);
    return 0;
}
