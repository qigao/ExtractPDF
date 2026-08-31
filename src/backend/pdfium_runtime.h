#ifndef QUANTAPDF_BACKEND_PDFIUM_RUNTIME_H
#define QUANTAPDF_BACKEND_PDFIUM_RUNTIME_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_pdfium_enter(void);
void quantapdf_pdfium_leave(void);

#ifdef __cplusplus
}
#endif

#endif
