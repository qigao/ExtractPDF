#include "internal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <stdint.h>
#include <windows.h>
#include <wchar.h>
#endif

static extractpdf_status extractpdf_open_output_file(
    const char *filename,
    FILE **out_file)
{
    *out_file = NULL;

#ifdef _WIN32
    {
        wchar_t *wide_filename = NULL;
        int wide_count;
        int converted;
        errno_t open_error;

        wide_count = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            filename,
            -1,
            NULL,
            0);
        if (wide_count <= 0)
            return EXTRACTPDF_ERROR_ARGUMENT;

        if ((size_t)wide_count > SIZE_MAX / sizeof(*wide_filename))
            return EXTRACTPDF_ERROR_NOMEM;

        wide_filename = (wchar_t *)malloc(
            (size_t)wide_count * sizeof(*wide_filename));
        if (wide_filename == NULL)
            return EXTRACTPDF_ERROR_NOMEM;

        converted = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            filename,
            -1,
            wide_filename,
            wide_count);
        if (converted <= 0) {
            free(wide_filename);
            return EXTRACTPDF_ERROR_ARGUMENT;
        }

        open_error = _wfopen_s(out_file, wide_filename, L"wb");
        free(wide_filename);

        if (open_error != 0 || *out_file == NULL)
            return EXTRACTPDF_ERROR_IO;
    }
#else
    *out_file = fopen(filename, "wb");
    if (*out_file == NULL)
        return EXTRACTPDF_ERROR_IO;
#endif

    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename)
{
    FILE *file = NULL;
    extractpdf_status status;
    size_t offset = 0;

    if (output == NULL || filename == NULL || filename[0] == '\0')
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_open_output_file(filename, &file);
    if (status != EXTRACTPDF_OK)
        return status;

    while (offset < output->size) {
        size_t written = fwrite(
            output->data + offset,
            1,
            output->size - offset,
            file);

        if (written == 0) {
            status = EXTRACTPDF_ERROR_IO;
            break;
        }
        offset += written;
    }

    if (status == EXTRACTPDF_OK && fflush(file) != 0)
        status = EXTRACTPDF_ERROR_IO;

    if (fclose(file) != 0 && status == EXTRACTPDF_OK)
        status = EXTRACTPDF_ERROR_IO;

    return status;
}
