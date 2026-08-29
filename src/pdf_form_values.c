#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

static pdf_obj *effective_raw(fz_context *ctx, pdf_obj *field, pdf_obj *key)
{
    size_t depth = 0;
    while (field != NULL && !pdf_is_null(ctx, field)) {
        pdf_obj *value = NULL;
        pdf_obj *parent = NULL;
        if (++depth > 257)
            return NULL;
        if (extractpdf_pdf_dict_find(ctx, field, key, &value))
            return value;
        if (!extractpdf_pdf_dict_find(ctx, field, PDF_NAME(Parent), &parent))
            break;
        field = parent;
    }
    return NULL;
}

static pdf_obj *find_field_source(
    fz_context *ctx,
    pdf_obj *fields,
    const extractpdf_pdf_form_model *model,
    size_t field_index)
{
    const extractpdf_pdf_form_string *name = &model->fields[field_index].name;
    if (name->present && name->size != 0)
        return pdf_lookup_field(ctx, fields, model->strings + name->offset);
    if (pdf_is_array(ctx, fields)) {
        int i;
        int n = pdf_array_len(ctx, fields);
        for (i = 0; i < n; ++i) {
            pdf_obj *candidate = pdf_array_get(ctx, fields, i);
            pdf_obj *t = NULL;
            int has_t;
            if (!pdf_is_dict(ctx, candidate))
                continue;
            has_t = extractpdf_pdf_dict_find(ctx, candidate, PDF_NAME(T), &t);
            if (!name->present && !has_t)
                return candidate;
            if (name->present && name->size == 0 && has_t && pdf_is_string(ctx, t)) {
                const char *text = pdf_to_text_string(ctx, t);
                if (text != NULL && text[0] == '\0')
                    return candidate;
            }
        }
    }
    return NULL;
}

static extractpdf_status append_string(
    extractpdf_pdf_form_model *model,
    const char *text,
    size_t size,
    extractpdf_pdf_form_string *out)
{
    size_t required;
    size_t capacity;
    char *grown;

    if (model->string_size > SIZE_MAX - size - 1)
        return EXTRACTPDF_ERROR_NOMEM;
    required = model->string_size + size + 1;
    if (required > model->string_capacity) {
        capacity = model->string_capacity ? model->string_capacity : 64;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        if (capacity < required)
            return EXTRACTPDF_ERROR_NOMEM;
        grown = (char *)realloc(model->strings, capacity);
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        model->strings = grown;
        model->string_capacity = capacity;
    }
    out->offset = model->string_size;
    out->size = size;
    out->present = 1;
    memcpy(model->strings + model->string_size, text, size);
    model->strings[model->string_size + size] = '\0';
    model->string_size = required;
    return EXTRACTPDF_OK;
}

static extractpdf_status reserve_one_value(
    extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_value_internal **out_value)
{
    extractpdf_pdf_form_value_internal *grown;

    *out_value = NULL;
    if (model->value_count == SIZE_MAX ||
        model->value_count + 1 > SIZE_MAX / sizeof(*model->values))
        return EXTRACTPDF_ERROR_NOMEM;
    grown = (extractpdf_pdf_form_value_internal *)realloc(
        model->values, (model->value_count + 1) * sizeof(*model->values));
    if (grown == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    model->values = grown;
    *out_value = &model->values[model->value_count];
    memset(*out_value, 0, sizeof(**out_value));
    return EXTRACTPDF_OK;
}

static extractpdf_status append_utf8_value(
    extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_field_internal *field,
    const char *text)
{
    extractpdf_pdf_form_value_internal *value;
    extractpdf_status status;
    size_t size = strlen(text);

    status = reserve_one_value(model, &value);
    if (status != EXTRACTPDF_OK)
        return status;
    value->kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    status = append_string(model, text, size, &value->utf8);
    if (status != EXTRACTPDF_OK)
        return status;
    field->first_value = model->value_count;
    field->value_count = 1;
    ++model->value_count;
    return EXTRACTPDF_OK;
}

static extractpdf_status append_option_value(
    extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_field_internal *field,
    size_t option_index)
{
    extractpdf_pdf_form_value_internal *value;
    extractpdf_status status;

    status = reserve_one_value(model, &value);
    if (status != EXTRACTPDF_OK)
        return status;
    value->kind = EXTRACTPDF_FORM_VALUE_OPTION;
    value->option_index = option_index;
    field->first_value = model->value_count;
    field->value_count = 1;
    ++model->value_count;
    return EXTRACTPDF_OK;
}

static extractpdf_status materialize_button_value(
    fz_context *ctx,
    extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_field_internal *field,
    pdf_obj *value)
{
    const char *state;
    size_t option_index;

    field->value_count = 0;
    if (value == NULL) {
        field->value_presence = EXTRACTPDF_FORM_VALUE_MISSING;
        return EXTRACTPDF_OK;
    }
    if (!pdf_is_name(ctx, value))
        return EXTRACTPDF_ERROR_FORMAT;

    state = pdf_to_name(ctx, value);
    if (state == NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    field->value_presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    if (strcmp(state, "Off") == 0)
        return EXTRACTPDF_OK;

    for (option_index = 0; option_index < field->option_count; ++option_index) {
        extractpdf_pdf_form_option_internal *option =
            &model->options[field->first_option + option_index];
        if (option->kind != EXTRACTPDF_FORM_OPTION_BUTTON_STATE ||
            option->button_state == NULL)
            return EXTRACTPDF_ERROR_FORMAT;
        if (strcmp(state, option->button_state) == 0)
            return append_option_value(model, field, option_index);
    }
    return EXTRACTPDF_ERROR_FORMAT;
}

extractpdf_status extractpdf_pdf_form_materialize_scalar_values(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model *model)
{
    pdf_obj *fields;
    size_t index;

    if (ctx == NULL || document == NULL || model == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    fields = pdf_dict_getp(ctx, pdf_trailer(ctx, document), "Root/AcroForm/Fields");
    if (!pdf_is_array(ctx, fields))
        return EXTRACTPDF_OK;

    for (index = 0; index < model->field_count; ++index) {
        extractpdf_pdf_form_field_internal *field = &model->fields[index];
        pdf_obj *source = find_field_source(ctx, fields, model, index);
        pdf_obj *v;
        extractpdf_status status;

        if (source == NULL)
            return EXTRACTPDF_ERROR_FORMAT;
        v = effective_raw(ctx, source, PDF_NAME(V));
        switch (field->type) {
        case EXTRACTPDF_FORM_FIELD_TEXT:
            if (v == NULL) {
                field->value_presence = EXTRACTPDF_FORM_VALUE_MISSING;
                field->value_count = 0;
                break;
            }
            if (!pdf_is_string(ctx, v))
                return EXTRACTPDF_ERROR_FORMAT;
            {
                const char *text = pdf_to_text_string(ctx, v);
                if (text == NULL)
                    return EXTRACTPDF_ERROR_FORMAT;
                field->value_presence = EXTRACTPDF_FORM_VALUE_PRESENT;
                status = append_utf8_value(model, field, text);
                if (status != EXTRACTPDF_OK)
                    return status;
            }
            break;
        case EXTRACTPDF_FORM_FIELD_CHECKBOX:
        case EXTRACTPDF_FORM_FIELD_RADIO_BUTTON:
            status = materialize_button_value(ctx, model, field, v);
            if (status != EXTRACTPDF_OK)
                return status;
            break;
        case EXTRACTPDF_FORM_FIELD_SIGNATURE:
            field->value_presence = EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;
            field->value_count = 0;
            if (v == NULL) {
                field->is_signed = 0;
                break;
            }
            if (!pdf_is_dict(ctx, v))
                return EXTRACTPDF_ERROR_FORMAT;
            {
                pdf_obj *type = NULL;
                if (extractpdf_pdf_dict_find(ctx, v, PDF_NAME(Type), &type) &&
                    (!pdf_is_name(ctx, type) ||
                     !pdf_name_eq(ctx, type, PDF_NAME(Sig))))
                    return EXTRACTPDF_ERROR_FORMAT;
                field->is_signed = 1;
            }
            break;
        case EXTRACTPDF_FORM_FIELD_PUSH_BUTTON:
        case EXTRACTPDF_FORM_FIELD_UNKNOWN:
            field->value_presence = EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;
            field->value_count = 0;
            break;
        default:
            break;
        }
    }
    return extractpdf_pdf_form_materialize_choice_values(ctx, document, model);
}
