#include <extractpdf/extractpdf.h>

const char *extractpdf_status_string(extractpdf_status status)
{
    switch (status) {
    case EXTRACTPDF_OK:
        return "ok";
    case EXTRACTPDF_ERROR_ARGUMENT:
        return "invalid argument";
    case EXTRACTPDF_ERROR_IO:
        return "I/O error";
    case EXTRACTPDF_ERROR_PASSWORD:
        return "password required or invalid";
    case EXTRACTPDF_ERROR_FORMAT:
        return "invalid document format";
    case EXTRACTPDF_ERROR_UNSUPPORTED:
        return "unsupported operation or content";
    case EXTRACTPDF_ERROR_NOMEM:
        return "out of memory";
    case EXTRACTPDF_ERROR_MUPDF:
        return "MuPDF error";
    case EXTRACTPDF_ERROR_STATE:
        return "invalid state";
    default:
        return "unknown error";
    }
}
