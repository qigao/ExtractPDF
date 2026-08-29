#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_pdf_edit_choice_assignment {
    int missing;
    int custom_utf8;
    char *utf8;
    size_t utf8_size;
    size_t *option_indices;
    size_t value_count;
} extractpdf_pdf_edit_choice_assignment;

static void extractpdf_pdf_edit_choice_assignment_drop(
    extractpdf_pdf_edit_choice_assignment *assignment)
{
    if (assignment == NULL)
        return;
    free(assignment->utf8);
    free(assignment->option_indices);
    memset(assignment, 0, sizeof(*assignment));
}

static int extractpdf_pdf_edit_choice_utf8_valid(const char *text, size_t size)
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
        if (code < 0x80 ||
            (code < 0x800 && c >= 0xE0) ||
            (code < 0x10000 && c >= 0xF0))
            return 0;
    }
    return 1;
}

static int extractpdf_pdf_edit_choice_live_contains(
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

static extractpdf_status extractpdf_pdf_edit_choice_validate_option(
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_form_value_input *value,
    size_t *out_index)
{
    size_t value_min = offsetof(extractpdf_form_value_input, utf8_size) +
        sizeof(value->utf8_size);

    if (value->struct_size < value_min ||
        value->kind != EXTRACTPDF_FORM_VALUE_OPTION ||
        value->utf8 != NULL || value->utf8_size != 0 ||
        value->option_index >= field->option_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_index = value->option_index;
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_choice_validate_utf8(
    const extractpdf_form_value_input *value,
    char **out_text,
    size_t *out_size)
{
    size_t value_min = offsetof(extractpdf_form_value_input, utf8_size) +
        sizeof(value->utf8_size);
    char *copy;

    if (value->struct_size < value_min ||
        value->kind != EXTRACTPDF_FORM_VALUE_UTF8 ||
        value->option_index != SIZE_MAX || value->utf8 == NULL ||
        !extractpdf_pdf_edit_choice_utf8_valid(value->utf8, value->utf8_size))
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
    return EXTRACTPDF_OK;
}

static extractpdf_status extractpdf_pdf_edit_choice_validate_update(
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_form_value_update *update,
    extractpdf_pdf_edit_choice_assignment *assignment)
{
    size_t update_min = offsetof(extractpdf_form_value_update, value_count) +
        sizeof(update->value_count);
    size_t i;
    extractpdf_status status;

    memset(assignment, 0, sizeof(*assignment));
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
        assignment->missing = 1;
    } else if (field->type == EXTRACTPDF_FORM_FIELD_COMBO_BOX) {
        if (update->value_count != 1 || update->values == NULL)
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (update->values[0].kind == EXTRACTPDF_FORM_VALUE_OPTION) {
            assignment->option_indices = (size_t *)calloc(1,
                sizeof(*assignment->option_indices));
            if (assignment->option_indices == NULL)
                return EXTRACTPDF_ERROR_NOMEM;
            status = extractpdf_pdf_edit_choice_validate_option(
                field, &update->values[0], &assignment->option_indices[0]);
            if (status != EXTRACTPDF_OK)
                goto fail;
            assignment->value_count = 1;
        } else if (update->values[0].kind == EXTRACTPDF_FORM_VALUE_UTF8 &&
            (field->flags & PDF_CH_FIELD_IS_EDIT) != 0) {
            status = extractpdf_pdf_edit_choice_validate_utf8(
                &update->values[0], &assignment->utf8, &assignment->utf8_size);
            if (status != EXTRACTPDF_OK)
                goto fail;
            assignment->custom_utf8 = 1;
            assignment->value_count = 1;
        } else {
            return EXTRACTPDF_ERROR_ARGUMENT;
        }
    } else if (field->type == EXTRACTPDF_FORM_FIELD_LIST_BOX) {
        if (!field->is_multiselect && update->value_count != 1)
            return EXTRACTPDF_ERROR_ARGUMENT;
        if (update->value_count != 0) {
            if (update->value_count > SIZE_MAX / sizeof(*assignment->option_indices))
                return EXTRACTPDF_ERROR_NOMEM;
            assignment->option_indices = (size_t *)calloc(
                update->value_count, sizeof(*assignment->option_indices));
            if (assignment->option_indices == NULL)
                return EXTRACTPDF_ERROR_NOMEM;
        }
        for (i = 0; i < update->value_count; ++i) {
            size_t j;
            status = extractpdf_pdf_edit_choice_validate_option(
                field, &update->values[i], &assignment->option_indices[i]);
            if (status != EXTRACTPDF_OK)
                goto fail;
            for (j = 0; j < i; ++j)
                if (assignment->option_indices[j] == assignment->option_indices[i]) {
                    status = EXTRACTPDF_ERROR_ARGUMENT;
                    goto fail;
                }
        }
        assignment->value_count = update->value_count;
    } else {
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    if ((field->flags & PDF_FIELD_IS_READ_ONLY) != 0) {
        status = EXTRACTPDF_ERROR_STATE;
        goto fail;
    }
    return EXTRACTPDF_OK;

fail:
    extractpdf_pdf_edit_choice_assignment_drop(assignment);
    return status;
}

static extractpdf_status extractpdf_pdf_edit_choice_validate_exports(
    const extractpdf_pdf_form_model *model,
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_pdf_edit_choice_assignment *assignment)
{
    size_t i;

    if (assignment->missing || assignment->custom_utf8)
        return EXTRACTPDF_OK;
    for (i = 0; i < assignment->value_count; ++i) {
        const extractpdf_pdf_form_option_internal *option = &model->options[
            field->first_option + assignment->option_indices[i]];
        if (option->kind != EXTRACTPDF_FORM_OPTION_CHOICE ||
            !option->export_text.present)
            return EXTRACTPDF_ERROR_STATE;
    }
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_choice_is_noop(
    const extractpdf_pdf_form_model *model,
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_pdf_edit_choice_assignment *assignment)
{
    size_t i;

    if (assignment->missing)
        return field->value_presence == EXTRACTPDF_FORM_VALUE_MISSING;
    if (field->value_presence != EXTRACTPDF_FORM_VALUE_PRESENT ||
        field->value_count != assignment->value_count)
        return 0;
    if (assignment->custom_utf8) {
        const extractpdf_pdf_form_value_internal *value;
        const char *current;
        if (field->value_count != 1)
            return 0;
        value = &model->values[field->first_value];
        if (value->kind != EXTRACTPDF_FORM_VALUE_UTF8 ||
            value->utf8.size != assignment->utf8_size)
            return 0;
        current = model->strings + value->utf8.offset;
        return memcmp(current, assignment->utf8, assignment->utf8_size) == 0;
    }
    for (i = 0; i < assignment->value_count; ++i) {
        const extractpdf_pdf_form_value_internal *value =
            &model->values[field->first_value + i];
        if (value->kind != EXTRACTPDF_FORM_VALUE_OPTION ||
            value->option_index != assignment->option_indices[i])
            return 0;
    }
    return 1;
}

static void extractpdf_pdf_edit_choice_delete_descendant_values(
    fz_context *ctx,
    const extractpdf_pdf_form_live_field *live)
{
    size_t i;

    for (i = 0; i < live->group_node_count; ++i) {
        if (extractpdf_pdf_form_same_identity(
                ctx, live->group_nodes[i], live->group_head))
            continue;
        pdf_dict_del(ctx, live->group_nodes[i], PDF_NAME(V));
        pdf_dict_del(ctx, live->group_nodes[i], PDF_NAME(I));
    }
}

static const char *extractpdf_pdf_edit_choice_export(
    const extractpdf_pdf_form_model *model,
    const extractpdf_pdf_form_field_internal *field,
    size_t option_index)
{
    const extractpdf_pdf_form_option_internal *option =
        &model->options[field->first_option + option_index];
    return model->strings + option->export_text.offset;
}

static void extractpdf_pdf_edit_choice_write(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_model *model,
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_pdf_form_live_field *live,
    const extractpdf_pdf_edit_choice_assignment *assignment)
{
    size_t i;

    if (assignment->missing) {
        for (i = 0; i < live->group_node_count; ++i) {
            pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
            pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(I));
        }
        return;
    }

    if (assignment->custom_utf8) {
        pdf_dict_put_text_string(edit->ctx, live->group_head,
            PDF_NAME(V), assignment->utf8);
        pdf_dict_del(edit->ctx, live->group_head, PDF_NAME(I));
        extractpdf_pdf_edit_choice_delete_descendant_values(edit->ctx, live);
        return;
    }

    if (field->type == EXTRACTPDF_FORM_FIELD_LIST_BOX &&
        field->is_multiselect) {
        pdf_obj *values = pdf_dict_put_array(edit->ctx, live->group_head,
            PDF_NAME(V), (int)assignment->value_count);
        pdf_obj *indices = pdf_dict_put_array(edit->ctx, live->group_head,
            PDF_NAME(I), (int)assignment->value_count);
        for (i = 0; i < assignment->value_count; ++i) {
            const char *text = extractpdf_pdf_edit_choice_export(
                model, field, assignment->option_indices[i]);
            pdf_array_push_drop(edit->ctx, values,
                pdf_new_text_string(edit->ctx, text));
            pdf_array_push_int(edit->ctx, indices,
                (int64_t)assignment->option_indices[i]);
        }
    } else {
        const char *text = extractpdf_pdf_edit_choice_export(
            model, field, assignment->option_indices[0]);
        pdf_obj *indices;
        pdf_dict_put_text_string(edit->ctx, live->group_head,
            PDF_NAME(V), text);
        indices = pdf_dict_put_array(edit->ctx, live->group_head,
            PDF_NAME(I), 1);
        pdf_array_push_int(edit->ctx, indices,
            (int64_t)assignment->option_indices[0]);
    }
    extractpdf_pdf_edit_choice_delete_descendant_values(edit->ctx, live);
}

extractpdf_status extractpdf_pdf_edit_form_apply_choice(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    const extractpdf_pdf_form_live_field *live,
    const extractpdf_form_value_update *update)
{
    const extractpdf_pdf_form_field_internal *field;
    extractpdf_pdf_edit_choice_assignment assignment;
    extractpdf_pdf_edit_form_widget_handles handles;
    extractpdf_status status;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;

    memset(&handles, 0, sizeof(handles));
    if (edit == NULL || model == NULL || live == NULL || update == NULL ||
        field_index >= model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    field = &model->fields[field_index];
    if (field->type != EXTRACTPDF_FORM_FIELD_COMBO_BOX &&
        field->type != EXTRACTPDF_FORM_FIELD_LIST_BOX)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_edit_choice_validate_update(field, update, &assignment);
    if (status != EXTRACTPDF_OK)
        return status;
    status = extractpdf_pdf_edit_choice_validate_exports(model, field, &assignment);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_edit_choice_assignment_drop(&assignment);
        return status;
    }
    if (assignment.missing && live->effective_v_present &&
        !extractpdf_pdf_edit_choice_live_contains(
            edit->ctx, live, live->effective_v_owner)) {
        extractpdf_pdf_edit_choice_assignment_drop(&assignment);
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    }
    if (extractpdf_pdf_edit_choice_is_noop(model, field, &assignment)) {
        extractpdf_pdf_edit_choice_assignment_drop(&assignment);
        return EXTRACTPDF_OK;
    }

    status = extractpdf_pdf_edit_form_prepare_widget_handles(edit, live, &handles);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_edit_choice_assignment_drop(&assignment);
        return status;
    }
    status = extractpdf_pdf_edit_form_begin_widget_editing(edit, &handles);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
        extractpdf_pdf_edit_choice_assignment_drop(&assignment);
        return status;
    }

    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        pdf_begin_operation(edit->ctx, edit->document,
            "ExtractPDF set form value");
        operation_open = 1;
        extractpdf_pdf_edit_choice_write(
            edit, model, field, live, &assignment);
#if defined(EXTRACTPDF_TESTING)
        if (edit->test_fault ==
            EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_SEMANTIC_WRITE) {
            edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
            fz_throw(edit->ctx, FZ_ERROR_GENERIC,
                "injected form failure after semantic write");
        }
#endif
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
    extractpdf_pdf_edit_choice_assignment_drop(&assignment);
    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return EXTRACTPDF_OK;
}
