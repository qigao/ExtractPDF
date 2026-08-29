#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int extractpdf_pdf_edit_form_utf8_valid(
    const char *text,
    size_t size)
{
    const unsigned char *s = (const unsigned char *)text;
    size_t i = 0;

    while (i < size) {
        unsigned char c = s[i++];
        uint32_t code;
        size_t need;

        if (c == 0)
            return 0;
        if (c < 0x80)
            continue;
        if (c >= 0xC2 && c <= 0xDF) {
            code = c & 0x1F;
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            code = c & 0x0F;
            need = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            code = c & 0x07;
            need = 3;
        } else {
            return 0;
        }
        if (need > size - i)
            return 0;
        while (need-- != 0) {
            unsigned char tail = s[i++];
            if ((tail & 0xC0) != 0x80)
                return 0;
            code = (code << 6) | (tail & 0x3F);
        }
        if ((code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
            return 0;
        if ((code < 0x80) ||
            (code < 0x800 && c >= 0xE0) ||
            (code < 0x10000 && c >= 0xF0))
            return 0;
    }
    return 1;
}

static int extractpdf_pdf_edit_form_live_contains(
    fz_context *ctx,
    const extractpdf_pdf_form_live_field *live,
    pdf_obj *object)
{
    size_t i;

    if (object == NULL)
        return 0;
    for (i = 0; i < live->group_node_count; ++i)
        if (extractpdf_pdf_form_same_identity(ctx, live->group_nodes[i], object))
            return 1;
    return 0;
}

static extractpdf_status extractpdf_pdf_edit_form_text_preflight(
    extractpdf_pdf_edit *edit)
{
    pdf_obj *root;
    pdf_obj *acroform;
    pdf_obj *value = NULL;

    root = pdf_dict_get(edit->ctx, pdf_trailer(edit->ctx, edit->document),
        PDF_NAME(Root));
    if (!pdf_is_dict(edit->ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    acroform = pdf_dict_get(edit->ctx, root, PDF_NAME(AcroForm));
    if (!pdf_is_dict(edit->ctx, acroform))
        return EXTRACTPDF_ERROR_FORMAT;
    if (extractpdf_pdf_dict_find(edit->ctx, acroform, PDF_NAME(XFA), &value))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (extractpdf_pdf_dict_find(
            edit->ctx, acroform, PDF_NAME(NeedAppearances), &value)) {
        if (!pdf_is_bool(edit->ctx, value))
            return EXTRACTPDF_ERROR_FORMAT;
        if (pdf_to_bool(edit->ctx, value))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_form_validate_text_update(
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_form_value_update *update,
    char **out_text,
    size_t *out_size,
    int *out_missing)
{
    const extractpdf_form_value_input *value;
    size_t update_min;
    size_t value_min;
    char *copy;

    *out_text = NULL;
    *out_size = 0;
    *out_missing = 0;
    update_min = offsetof(extractpdf_form_value_update, value_count) +
        sizeof(update->value_count);
    if (update->struct_size < update_min)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (update->presence != EXTRACTPDF_FORM_VALUE_MISSING &&
        update->presence != EXTRACTPDF_FORM_VALUE_PRESENT)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (update->value_count > 0 && update->values == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    if (update->presence == EXTRACTPDF_FORM_VALUE_MISSING) {
        if (update->value_count != 0)
            return EXTRACTPDF_ERROR_ARGUMENT;
        *out_missing = 1;
    } else {
        if (update->value_count != 1 || update->values == NULL)
            return EXTRACTPDF_ERROR_ARGUMENT;
        value = &update->values[0];
        value_min = offsetof(extractpdf_form_value_input, utf8_size) +
            sizeof(value->utf8_size);
        if (value->struct_size < value_min ||
            value->kind != EXTRACTPDF_FORM_VALUE_UTF8 ||
            value->option_index != SIZE_MAX || value->utf8 == NULL)
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (!extractpdf_pdf_edit_form_utf8_valid(value->utf8, value->utf8_size))
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (value->utf8_size == SIZE_MAX)
            return EXTRACTPDF_ERROR_NOMEM;
        copy = (char *)malloc(value->utf8_size + 1);
        if (copy == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        memcpy(copy, value->utf8, value->utf8_size);
        copy[value->utf8_size] = '\0';
        *out_text = copy;
        *out_size = value->utf8_size;
    }

    if ((field->flags & PDF_FIELD_IS_READ_ONLY) != 0) {
        free(*out_text);
        *out_text = NULL;
        *out_size = 0;
        return EXTRACTPDF_ERROR_STATE;
    }
    if ((field->flags & PDF_TX_FIELD_IS_FILE_SELECT) != 0 ||
        (field->flags & PDF_TX_FIELD_IS_RICH_TEXT) != 0) {
        free(*out_text);
        *out_text = NULL;
        *out_size = 0;
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_form_text_is_noop(
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    int missing,
    const char *text,
    size_t text_size)
{
    const extractpdf_pdf_form_field_internal *field = &model->fields[field_index];
    const extractpdf_pdf_form_value_internal *value;
    const char *current;

    if (missing)
        return field->value_presence == EXTRACTPDF_FORM_VALUE_MISSING;
    if (field->value_presence != EXTRACTPDF_FORM_VALUE_PRESENT ||
        field->value_count != 1)
        return 0;
    value = &model->values[field->first_value];
    if (value->kind != EXTRACTPDF_FORM_VALUE_UTF8 ||
        value->utf8.size != text_size)
        return 0;
    current = model->strings + value->utf8.offset;
    return memcmp(current, text, text_size) == 0;
}

static void extractpdf_pdf_edit_form_write_text(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_live_field *live,
    int missing,
    const char *text)
{
    size_t i;

    if (missing) {
        for (i = 0; i < live->group_node_count; ++i)
            pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
        return;
    }
    pdf_dict_put_text_string(edit->ctx, live->group_head, PDF_NAME(V), text);
    for (i = 0; i < live->group_node_count; ++i)
        if (!extractpdf_pdf_form_same_identity(
                edit->ctx, live->group_nodes[i], live->group_head))
            pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
}

extractpdf_status extractpdf_pdf_edit_form_apply_text(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    const extractpdf_pdf_form_live_field *live,
    const extractpdf_form_value_update *update)
{
    const extractpdf_pdf_form_field_internal *field;
    extractpdf_pdf_edit_form_widget_handles handles;
    char *text = NULL;
    size_t text_size = 0;
    int missing = 0;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status;

    memset(&handles, 0, sizeof(handles));
    if (edit == NULL || model == NULL || live == NULL || update == NULL ||
        field_index >= model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    field = &model->fields[field_index];
    if (field->type != EXTRACTPDF_FORM_FIELD_TEXT)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_edit_form_text_preflight(edit);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_edit_form_validate_text_update(
        field, update, &text, &text_size, &missing);
    if (status != EXTRACTPDF_OK)
        return status;
    if (missing && live->effective_v_present &&
        !extractpdf_pdf_edit_form_live_contains(
            edit->ctx, live, live->effective_v_owner)) {
        free(text);
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    if (extractpdf_pdf_edit_form_text_is_noop(
            model, field_index, missing, text, text_size)) {
        free(text);
        return EXTRACTPDF_OK;
    }

    status = extractpdf_pdf_edit_form_prepare_widget_handles(edit, live, &handles);
    if (status != EXTRACTPDF_OK) {
        free(text);
        return status;
    }
#if defined(EXTRACTPDF_TESTING)
    if (edit->test_fault ==
        EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE) {
        edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
        extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
        free(text);
        return EXTRACTPDF_ERROR_MUPDF;
    }
#endif
    status = extractpdf_pdf_edit_form_begin_widget_editing(edit, &handles);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
        free(text);
        return status;
    }

    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        pdf_begin_operation(
            edit->ctx, edit->document, "ExtractPDF set form value");
        operation_open = 1;
        extractpdf_pdf_edit_form_write_text(edit, live, missing, text);
        extractpdf_pdf_edit_form_refresh_widget_handles(edit, &handles);
        if (extractpdf_pdf_edit_form_restore_widget_editing(edit, &handles) !=
            EXTRACTPDF_OK)
            fz_throw(edit->ctx, FZ_ERROR_GENERIC,
                "failed to restore form Widget editing state");
        pdf_end_operation(edit->ctx, edit->document);
        operation_open = 0;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        if (operation_open) {
            pdf_abandon_operation(edit->ctx, edit->document);
            operation_open = 0;
        }
        (void)extractpdf_pdf_edit_form_restore_widget_editing(edit, &handles);
        fz_report_error(edit->ctx);
    }

    extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
    free(text);
    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return EXTRACTPDF_OK;
}
