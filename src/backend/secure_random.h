#ifndef QUANTAPDF_BACKEND_SECURE_RANDOM_H
#define QUANTAPDF_BACKEND_SECURE_RANDOM_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_secure_random(
    unsigned char *data,
    size_t size);

#ifdef __cplusplus
}
#endif

#endif
