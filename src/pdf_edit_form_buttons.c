#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stddef.h>
#include <string.h>

static int extractpdf_pdf_edit_form_button_live_contains(
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

static extractpdf_status extractpdf_pdf_edit_form_validate_button_update(
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_form_value_update *update,
    size_t *out_option_index,
    int *out_missing,
    int *out_off)
{
    const extractpdf_form_value_input *value;
    size_t update_min;
    size_t value_min;

    *out_option_index = SIZE_MAX;
    *out_missing = 0;
    *out_off = 0;

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
    } else if (update->value_count == 0) {
        *out_off = 1;
    } else {
        if (update->value_count != 1 || update->values == NULL)
            return EXTRACTPDF_ERROR_ARGUMENT;
        value = &update->values[0];
        value_min = offsetof(extractpdf_form_value_input, utf8_size) +
            sizeof(value->utf8_size);
        if (value->struct_size < value_min ||
            value->kind != EXTRACTPDF_FORM_VALUE_OPTION ||
            value->utf8 != NULL || value->utf8_size != 0 ||
            value->option_index >= field->option_count)
            return EXTRACTPDF_ERROR_ARGUMENT;
        *out_option_index = value->option_index;
    }

    if ((field->flags & PDF_FIELD_IS_READ_ONLY) != 0)
        return EXTRACTPDF_ERROR_STATE;
    return EXTRACTPDF_OK;
}

static int extractpdf_pdf_edit_form_button_is_noop(
    const extractpdf_pdf_form_field_internal *field,
    const extractpdf_pdf_form_model *model,
    int missing,
    int off,
    size_t option_index)
{
    const extractpdf_pdf_form_value_internal *value;

    if (missing)
        return field->value_presence == EXTRACTPDF_FORM_VALUE_MISSING;
    if (field->value_presence != EXTRACTPDF_FORM_VALUE_PRESENT)
        return 0;
    if (off)
        return field->value_count == 0;
    if (field->value_count != 1)
        return 0;
    value = &model->values[field->first_value];
    return value->kind == EXTRACTPDF_FORM_VALUE_OPTION &&
        value->option_index == option_index;
}

static extractpdf_status extractpdf_pdf_edit_form_validate_button_widgets(
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    const extractpdf_pdf_form_live_field *live)
{
    const extractpdf_pdf_form_field_internal *field = &model->fields[field_index];
    size_t model_widget_index;
    size_t live_index = 0;

    for (model_widget_index = 0;
         model_widget_index < model->widget_count;
         ++model_widget_index) {
        const extractpdf_pdf_form_widget_internal *widget =
            &model->widgets[model_widget_index];
        const extractpdf_pdf_form_option_internal *option;

        if (widget->field_index != field_index)
            continue;
        if (live_index >= live->widget_count ||
            live->widgets[live_index].object == NULL)
            return EXTRACTPDF_ERROR_STATE;
        if (widget->button_option_index != SIZE_MAX) {
            if (widget->button_option_index >= field->option_count)
                return EXTRACTPDF_ERROR_STATE;
            option = &model->options[
                field->first_option + widget->button_option_index];
            if (option->kind != EXTRACTPDF_FORM_OPTION_BUTTON_STATE ||
                option->button_state == NULL)
                return EXTRACTPDF_ERROR_STATE;
        }
        ++live_index;
    }
    return live_index == live->widget_count ?
        EXTRACTPDF_OK : EXTRACTPDF_ERROR_STATE;
}

extractpdf_status extractpdf_pdf_edit_form_apply_button(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_model *model,
    size_t field_index,
    const extractpdf_pdf_form_live_field *live,
    const extractpdf_form_value_update *update)
{
    const extractpdf_pdf_form_field_internal *field;
    const char *selected_state = NULL;
    size_t option_index = SIZE_MAX;
    size_t i;
    size_t model_widget_index;
    size_t live_index;
    int missing = 0;
    int off = 0;
    int operation_open = 0;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status;

    if (edit == NULL || model == NULL || live == NULL || update == NULL ||
        field_index >= model->field_count)
        return EXTRACTPDF_ERROR_ARGUMENT;
    field = &model->fields[field_index];
    if (field->type != EXTRACTPDF_FORM_FIELD_CHECKBOX &&
        field->type != EXTRACTPDF_FORM_FIELD_RADIO_BUTTON)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_pdf_edit_form_validate_button_update(
        field, update, &option_index, &missing, &off);
    if (status != EXTRACTPDF_OK)
        return status;

    if (!missing && !off) {
        const extractpdf_pdf_form_option_internal *option =
            &model->options[field->first_option + option_index];
        if (option->kind != EXTRACTPDF_FORM_OPTION_BUTTON_STATE ||
            option->button_state == NULL)
            return EXTRACTPDF_ERROR_STATE;
        selected_state = option->button_state;
    }

    if (missing && live->effective_v_present &&
        !extractpdf_pdf_edit_form_button_live_contains(
            edit->ctx, live, live->effective_v_owner))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    if (extractpdf_pdf_edit_form_button_is_noop(
            field, model, missing, off, option_index))
        return EXTRACTPDF_OK;

    status = extractpdf_pdf_edit_form_validate_button_widgets(
        model, field_index, live);
    if (status != EXTRACTPDF_OK)
        return status;

    fz_var(operation_open);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        pdf_begin_operation(
            edit->ctx, edit->document, "ExtractPDF set form value");
        operation_open = 1;

        if (missing) {
            for (i = 0; i < live->group_node_count; ++i)
                pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
        } else {
            if (off)
                pdf_dict_put(edit->ctx, live->group_head,
                    PDF_NAME(V), PDF_NAME(Off));
            else
                pdf_dict_put_name(edit->ctx, live->group_head,
                    PDF_NAME(V), selected_state);
            for (i = 0; i < live->group_node_count; ++i)
                if (!extractpdf_pdf_form_same_identity(
                        edit->ctx, live->group_nodes[i], live->group_head))
                    pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
        }

        live_index = 0;
        for (model_widget_index = 0;
             model_widget_index < model->widget_count;
             ++model_widget_index) {
            const extractpdf_pdf_form_widget_internal *widget =
                &model->widgets[model_widget_index];
            const char *widget_state = NULL;

            if (widget->field_index != field_index)
                continue;
            if (widget->button_option_index != SIZE_MAX)
                widget_state = model->options[
                    field->first_option + widget->button_option_index].button_state;

            if (selected_state != NULL && widget_state != NULL &&
                strcmp(selected_state, widget_state) == 0)
                pdf_dict_put_name(edit->ctx,
                    live->widgets[live_index].object,
                    PDF_NAME(AS), widget_state);
            else
                pdf_dict_put(edit->ctx,
                    live->widgets[live_index].object,
                    PDF_NAME(AS), PDF_NAME(Off));
            ++live_index;
        }

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
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return EXTRACTPDF_OK;
}
