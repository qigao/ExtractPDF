#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extractpdf_status extractpdf_extract_text(
    extractpdf_page *page,
    char **out_utf8,
    size_t *out_size)
{
    fz_context *ctx;
    fz_buffer *buffer = NULL;
    unsigned char *storage = NULL;
    char *copy;
    size_t size = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (page == NULL || out_utf8 == NULL || out_size == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    ctx = page->document->ctx;
    fz_var(buffer);
    fz_var(storage);
    fz_var(size);
    fz_var(caught_code);

    fz_try(ctx)
    {
        buffer = fz_new_buffer_from_page(ctx, page->page, NULL);
        if (buffer != NULL)
            size = fz_buffer_storage(ctx, buffer, &storage);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        fz_drop_buffer(ctx, buffer);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (buffer == NULL) {
        return EXTRACTPDF_ERROR_NOMEM;
    }
    if (size == SIZE_MAX) {
        fz_drop_buffer(ctx, buffer);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    copy = (char *)malloc(size + 1);
    if (copy == NULL) {
        fz_drop_buffer(ctx, buffer);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    if (size != 0)
        memcpy(copy, storage, size);
    copy[size] = '\0';
    fz_drop_buffer(ctx, buffer);

    *out_utf8 = copy;
    *out_size = size;
    return EXTRACTPDF_OK;
}

void extractpdf_free(void *memory)
{
    free(memory);
}
