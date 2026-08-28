#include "pdf_edit_internal.h"

#include <stddef.h>
#include <string.h>

static void extractpdf_pdf_edit_zero_ref(extractpdf_annotation_ref *ref)
{
    memset(ref, 0, sizeof(*ref));
}

static void extractpdf_pdf_edit_zero_rect(extractpdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static extractpdf_status extractpdf_pdf_edit_validate_page(
    extractpdf_pdf_edit *edit,
    int page_index)
{
    int page_count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    if (page_index < 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    fz_var(page_count);
    fz_var(caught_code);

    fz_try(edit->ctx)
    {
        page_count = pdf_count_pages(edit->ctx, edit->document);
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (page_index >= page_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_annotation_count(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t *out_count)
{
    extractpdf_status status;

    if (out_count != NULL)
        *out_count = 0;
    if (out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_validate_page(edit, page_index);
    if (status != EXTRACTPDF_OK)
        return status;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_ref_at(
    extractpdf_pdf_edit *edit,
    int page_index,
    size_t index,
    extractpdf_annotation_ref *out_ref)
{
    extractpdf_status status;

    (void)index;

    if (out_ref != NULL)
        extractpdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_validate_page(edit, page_index);
    if (status != EXTRACTPDF_OK)
        return status;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_get_info(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    extractpdf_annotation_info *out_info)
{
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_info, flags) +
        sizeof(out_info->flags);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->type = EXTRACTPDF_ANNOTATION_UNKNOWN;
    extractpdf_pdf_edit_zero_rect(&out_info->bounds);
    out_info->flags = 0;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL ||
        ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_contents(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (out_utf8 == NULL || out_size == NULL || edit == NULL ||
        edit->ctx == NULL || edit->document == NULL || ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_create(
    extractpdf_pdf_edit *edit,
    int page_index,
    const extractpdf_annotation_create_options *options,
    extractpdf_annotation_ref *out_ref)
{
    size_t minimum_size;
    extractpdf_status status;

    if (out_ref != NULL)
        extractpdf_pdf_edit_zero_ref(out_ref);
    if (out_ref == NULL || options == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_create_options, contents_size) +
        sizeof(options->contents_size);
    if (options->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_validate_page(edit, page_index);
    if (status != EXTRACTPDF_OK)
        return status;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_update(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref,
    const extractpdf_annotation_update *update)
{
    const uint32_t known_fields =
        EXTRACTPDF_ANNOTATION_UPDATE_BOUNDS |
        EXTRACTPDF_ANNOTATION_UPDATE_FLAGS |
        EXTRACTPDF_ANNOTATION_UPDATE_CONTENTS;
    size_t minimum_size;

    if (update == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(extractpdf_annotation_update, contents_size) +
        sizeof(update->contents_size);
    if (update->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if ((update->fields & ~known_fields) != 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL ||
        ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_pdf_edit_annotation_delete(
    extractpdf_pdf_edit *edit,
    const extractpdf_annotation_ref *ref)
{
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL ||
        ref == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
