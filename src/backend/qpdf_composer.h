#ifndef QUANTAPDF_BACKEND_QPDF_COMPOSER_H
#define QUANTAPDF_BACKEND_QPDF_COMPOSER_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_composer quantapdf_composer;

quantapdf_status quantapdf_qpdf_compose(
    const quantapdf_composer *composer,
    unsigned char **out_data,
    size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
