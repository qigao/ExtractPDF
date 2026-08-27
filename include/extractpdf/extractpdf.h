#ifndef EXTRACTPDF_EXTRACTPDF_H
#define EXTRACTPDF_EXTRACTPDF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(EXTRACTPDF_SHARED)
#  if defined(EXTRACTPDF_BUILDING_LIBRARY)
#    define EXTRACTPDF_API __declspec(dllexport)
#  else
#    define EXTRACTPDF_API __declspec(dllimport)
#  endif
#else
#  define EXTRACTPDF_API
#endif

typedef struct extractpdf_document extractpdf_document;
typedef struct extractpdf_page extractpdf_page;

typedef struct extractpdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} extractpdf_rect;

typedef enum extractpdf_status {
    EXTRACTPDF_OK = 0,
    EXTRACTPDF_ERROR_ARGUMENT = 1,
    EXTRACTPDF_ERROR_IO = 2,
    EXTRACTPDF_ERROR_PASSWORD = 3,
    EXTRACTPDF_ERROR_FORMAT = 4,
    EXTRACTPDF_ERROR_UNSUPPORTED = 5,
    EXTRACTPDF_ERROR_NOMEM = 6,
    EXTRACTPDF_ERROR_MUPDF = 7
} extractpdf_status;

EXTRACTPDF_API extractpdf_status extractpdf_open(
    const char *filename,
    const char *password,
    extractpdf_document **out_document);

EXTRACTPDF_API extractpdf_status extractpdf_page_count(
    extractpdf_document *document,
    int *out_page_count);

EXTRACTPDF_API extractpdf_status extractpdf_load_page(
    extractpdf_document *document,
    int page_index,
    extractpdf_page **out_page);

EXTRACTPDF_API extractpdf_status extractpdf_page_bounds(
    extractpdf_page *page,
    extractpdf_rect *out_bounds);

EXTRACTPDF_API const char *extractpdf_status_string(
    extractpdf_status status);

EXTRACTPDF_API void extractpdf_drop_page(
    extractpdf_page *page);

EXTRACTPDF_API void extractpdf_close(
    extractpdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
