#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

extractpdf_status extractpdf_export_page_range(
    extractpdf_document *document,
    int first_page,
    size_t page_count,
    extractpdf_output **out_output)
{
    int *indices;
    extractpdf_status status;
    size_t i;
    size_t offset;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || first_page < 0 || page_count == 0 ||
        page_count > (size_t)INT_MAX ||
        page_count > SIZE_MAX / sizeof(*indices))
        return EXTRACTPDF_ERROR_ARGUMENT;

    offset = page_count - 1;
    if ((size_t)first_page > (size_t)INT_MAX - offset)
        return EXTRACTPDF_ERROR_ARGUMENT;

    indices = (int *)malloc(page_count * sizeof(*indices));
    if (indices == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    for (i = 0; i < page_count; ++i)
        indices[i] = first_page + (int)i;

    status = extractpdf_export_pages(document, indices, page_count, out_output);
    free(indices);
    return status;
}
