#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

quantapdf_status quantapdf_extract_text(
    quantapdf_page *page,
    char **out_utf8,
    size_t *out_size)
{
    fz_context *ctx;
    fz_buffer *buffer = NULL;
    unsigned char *storage = NULL;
    char *copy;
    size_t size = 0;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (page == NULL || out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_page_ensure_mupdf(page);
    if (status != QUANTAPDF_OK)
        return status;

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
        return quantapdf_status_from_backend(caught_code);
    }
    if (buffer == NULL) {
        return QUANTAPDF_ERROR_NOMEM;
    }
    if (size == SIZE_MAX) {
        fz_drop_buffer(ctx, buffer);
        return QUANTAPDF_ERROR_NOMEM;
    }

    copy = (char *)malloc(size + 1);
    if (copy == NULL) {
        fz_drop_buffer(ctx, buffer);
        return QUANTAPDF_ERROR_NOMEM;
    }

    if (size != 0)
        memcpy(copy, storage, size);
    copy[size] = '\0';
    fz_drop_buffer(ctx, buffer);

    *out_utf8 = copy;
    *out_size = size;
    return QUANTAPDF_OK;
}

void quantapdf_free(void *memory)
{
    free(memory);
}
