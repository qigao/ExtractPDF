#include "pdf_form_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct extractpdf_form {
    extractpdf_pdf_form_model *model;
};

static void extractpdf_form_reset_rect(extractpdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

extractpdf_status extractpdf_document_form(
    extractpdf_document *document,
    extractpdf_form **out_form)
{
    pdf_document *pdf;
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_form *form;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (out_form == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_form = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    pdf = pdf_document_from_fz_document(document->ctx, document->doc);
    if (pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    fz_var(model);
    fz_var(status);
    fz_var(caught_code);

    fz_try(document->ctx)
    {
        status = extractpdf_pdf_form_parse(document->ctx, pdf, &model);
    }
    fz_catch(document->ctx)
    {
        caught_code = fz_caught(document->ctx);
        fz_report_error(document->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_pdf_form_drop_model(model);
        return extractpdf_status_from_mupdf(caught_code);
    }
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_form_drop_model(model);
        return status;
    }

    form = (extractpdf_form *)calloc(1, sizeof(*form));
    if (form == NULL) {
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    form->model = model;
    *out_form = form;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_form_field_count(
    const extractpdf_form *form,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (form == NULL || form->model == NULL || out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_count = form->model->field_count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_form_field_get_info(
    const extractpdf_form *form,
    size_t field_index,
    extractpdf_form_field_info *out_info)
{
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_form_field_info, is_signed) +
        sizeof(out_info->is_signed);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->type = EXTRACTPDF_FORM_FIELD_UNKNOWN;
    out_info->flags = 0;
    out_info->value_presence = EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;
    out_info->value_count = 0;
    out_info->option_count = 0;
    out_info->widget_count = 0;
    out_info->is_multiselect = 0;
    out_info->is_signed = 0;

    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

static extractpdf_status extractpdf_form_string_unavailable(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (form == NULL || form->model == NULL || out_utf8 == NULL ||
        out_size == NULL || field_index >= form->model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_form_field_name(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size)
{
    return extractpdf_form_string_unavailable(
        form, field_index, out_utf8, out_size);
}

extractpdf_status extractpdf_form_field_label(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size)
{
    return extractpdf_form_string_unavailable(
        form, field_index, out_utf8, out_size);
}

extractpdf_status extractpdf_form_field_value_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    extractpdf_form_value_info *out_info)
{
    size_t minimum_size;
    (void)value_index;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_form_value_info, option_index) +
        sizeof(out_info->option_index);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;
    out_info->kind = EXTRACTPDF_FORM_VALUE_UTF8;
    out_info->option_index = SIZE_MAX;
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_form_field_value_utf8(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size)
{
    (void)value_index;
    return extractpdf_form_string_unavailable(
        form, field_index, out_utf8, out_size);
}

extractpdf_status extractpdf_form_field_option_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    extractpdf_form_option_info *out_info)
{
    size_t minimum_size;
    (void)option_index;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_form_option_info, kind) +
        sizeof(out_info->kind);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;
    out_info->kind = EXTRACTPDF_FORM_OPTION_BUTTON_STATE;
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

extractpdf_status extractpdf_form_field_option_export(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size)
{
    (void)option_index;
    return extractpdf_form_string_unavailable(
        form, field_index, out_utf8, out_size);
}

extractpdf_status extractpdf_form_field_option_display(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size)
{
    (void)option_index;
    return extractpdf_form_string_unavailable(
        form, field_index, out_utf8, out_size);
}

extractpdf_status extractpdf_form_widget_count(
    const extractpdf_form *form,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (form == NULL || form->model == NULL || out_count == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_count = form->model->widget_count;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_form_widget_get_info(
    const extractpdf_form *form,
    size_t widget_index,
    extractpdf_form_widget_info *out_info)
{
    size_t minimum_size;

    if (out_info == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(extractpdf_form_widget_info, button_option_index) +
        sizeof(out_info->button_option_index);
    if (out_info->struct_size < minimum_size)
        return EXTRACTPDF_ERROR_ARGUMENT;

    out_info->field_index = SIZE_MAX;
    out_info->page_index = -1;
    extractpdf_form_reset_rect(&out_info->bounds);
    out_info->flags = 0;
    out_info->button_option_index = SIZE_MAX;

    if (form == NULL || form->model == NULL ||
        widget_index >= form->model->widget_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

void extractpdf_drop_form(extractpdf_form *form)
{
    if (form == NULL)
        return;
    extractpdf_pdf_form_drop_model(form->model);
    free(form);
}
