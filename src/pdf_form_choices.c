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
        if (quantapdf_pdf_dict_find(ctx, field, key, &value))
            return value;
        if (!quantapdf_pdf_dict_find(ctx, field, PDF_NAME(Parent), &parent))
            break;
        field = parent;
    }
    return NULL;
}

static pdf_obj *find_field_source(
    fz_context *ctx,
    pdf_obj *fields,
    const quantapdf_pdf_form_model *model,
    size_t field_index)
{
    const quantapdf_pdf_form_string *name = &model->fields[field_index].name;
    if (name->present && name->size != 0)
        return pdf_lookup_field(ctx, fields, model->strings + name->offset);
    if (pdf_is_array(ctx, fields)) {
        int i;
        int count = pdf_array_len(ctx, fields);
        for (i = 0; i < count; ++i) {
            pdf_obj *candidate = pdf_array_get(ctx, fields, i);
            pdf_obj *t = NULL;
            int has_t;
            if (!pdf_is_dict(ctx, candidate))
                continue;
            has_t = quantapdf_pdf_dict_find(ctx, candidate, PDF_NAME(T), &t);
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

static quantapdf_status append_string(
    quantapdf_pdf_form_model *model,
    const char *text,
    quantapdf_pdf_form_string *out)
{
    size_t size = strlen(text);
    size_t required;
    size_t capacity;
    char *grown;

    if (model->string_size > SIZE_MAX - size - 1)
        return QUANTAPDF_ERROR_NOMEM;
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
            return QUANTAPDF_ERROR_NOMEM;
        grown = (char *)realloc(model->strings, capacity);
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        model->strings = grown;
        model->string_capacity = capacity;
    }
    out->offset = model->string_size;
    out->size = size;
    out->present = 1;
    memcpy(model->strings + model->string_size, text, size + 1);
    model->string_size = required;
    return QUANTAPDF_OK;
}

static quantapdf_status append_choice_option(
    fz_context *ctx,
    quantapdf_pdf_form_model *model,
    pdf_obj *entry)
{
    const char *export_text;
    const char *display_text;
    quantapdf_pdf_form_option_internal *grown;
    quantapdf_pdf_form_option_internal *option;

    if (pdf_is_string(ctx, entry)) {
        export_text = pdf_to_text_string(ctx, entry);
        display_text = export_text;
    } else if (pdf_is_array(ctx, entry) && pdf_array_len(ctx, entry) == 2 &&
               pdf_is_string(ctx, pdf_array_get(ctx, entry, 0)) &&
               pdf_is_string(ctx, pdf_array_get(ctx, entry, 1))) {
        export_text = pdf_to_text_string(ctx, pdf_array_get(ctx, entry, 0));
        display_text = pdf_to_text_string(ctx, pdf_array_get(ctx, entry, 1));
    } else {
        return QUANTAPDF_ERROR_FORMAT;
    }
    if (export_text == NULL || display_text == NULL)
        return QUANTAPDF_ERROR_FORMAT;
    if (model->option_count == SIZE_MAX ||
        model->option_count + 1 > SIZE_MAX / sizeof(*model->options))
        return QUANTAPDF_ERROR_NOMEM;
    grown = (quantapdf_pdf_form_option_internal *)realloc(
        model->options, (model->option_count + 1) * sizeof(*model->options));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    model->options = grown;
    option = &model->options[model->option_count];
    memset(option, 0, sizeof(*option));
    option->kind = QUANTAPDF_FORM_OPTION_CHOICE;
    if (append_string(model, export_text, &option->export_text) != QUANTAPDF_OK)
        return QUANTAPDF_ERROR_NOMEM;
    if (append_string(model, display_text, &option->display_text) != QUANTAPDF_OK)
        return QUANTAPDF_ERROR_NOMEM;
    ++model->option_count;
    return QUANTAPDF_OK;
}

static const char *option_export(
    const quantapdf_pdf_form_model *model,
    const quantapdf_pdf_form_field_internal *field,
    size_t option_index)
{
    const quantapdf_pdf_form_option_internal *option =
        &model->options[field->first_option + option_index];
    return model->strings + option->export_text.offset;
}

static quantapdf_status append_option_value(
    quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_field_internal *field,
    size_t option_index)
{
    quantapdf_pdf_form_value_internal *grown;
    quantapdf_pdf_form_value_internal *value;
    if (model->value_count == SIZE_MAX ||
        model->value_count + 1 > SIZE_MAX / sizeof(*model->values))
        return QUANTAPDF_ERROR_NOMEM;
    grown = (quantapdf_pdf_form_value_internal *)realloc(
        model->values, (model->value_count + 1) * sizeof(*model->values));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    model->values = grown;
    value = &model->values[model->value_count];
    memset(value, 0, sizeof(*value));
    value->kind = QUANTAPDF_FORM_VALUE_OPTION;
    value->option_index = option_index;
    if (field->value_count == 0)
        field->first_value = model->value_count;
    ++field->value_count;
    ++model->value_count;
    return QUANTAPDF_OK;
}

static quantapdf_status append_utf8_value(
    quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_field_internal *field,
    const char *text)
{
    quantapdf_pdf_form_value_internal *grown;
    quantapdf_pdf_form_value_internal *value;
    if (model->value_count == SIZE_MAX ||
        model->value_count + 1 > SIZE_MAX / sizeof(*model->values))
        return QUANTAPDF_ERROR_NOMEM;
    grown = (quantapdf_pdf_form_value_internal *)realloc(
        model->values, (model->value_count + 1) * sizeof(*model->values));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    model->values = grown;
    value = &model->values[model->value_count];
    memset(value, 0, sizeof(*value));
    value->kind = QUANTAPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    if (append_string(model, text, &value->utf8) != QUANTAPDF_OK)
        return QUANTAPDF_ERROR_NOMEM;
    field->first_value = model->value_count;
    field->value_count = 1;
    ++model->value_count;
    return QUANTAPDF_OK;
}

static quantapdf_status parse_options(
    fz_context *ctx,
    quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_field_internal *field,
    pdf_obj *opt)
{
    int i;
    int count;
    field->first_option = model->option_count;
    field->option_count = 0;
    if (opt == NULL)
        return QUANTAPDF_OK;
    if (!pdf_is_array(ctx, opt))
        return QUANTAPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, opt);
    for (i = 0; i < count; ++i) {
        quantapdf_status status = append_choice_option(ctx, model, pdf_array_get(ctx, opt, i));
        if (status != QUANTAPDF_OK)
            return status;
        ++field->option_count;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status validate_i(
    fz_context *ctx,
    pdf_obj *indices,
    size_t option_count,
    size_t **out_indices,
    size_t *out_count)
{
    size_t *values = NULL;
    int i;
    int count;
    *out_indices = NULL;
    *out_count = 0;
    if (indices == NULL)
        return QUANTAPDF_OK;
    if (!pdf_is_array(ctx, indices))
        return QUANTAPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, indices);
    if (count != 0) {
        values = (size_t *)calloc((size_t)count, sizeof(*values));
        if (values == NULL)
            return QUANTAPDF_ERROR_NOMEM;
    }
    for (i = 0; i < count; ++i) {
        pdf_obj *item = pdf_array_get(ctx, indices, i);
        int64_t raw;
        int j;
        if (!pdf_is_int(ctx, item)) {
            free(values);
            return QUANTAPDF_ERROR_FORMAT;
        }
        raw = pdf_to_int64(ctx, item);
        if (raw < 0 || (uint64_t)raw >= option_count) {
            free(values);
            return QUANTAPDF_ERROR_FORMAT;
        }
        for (j = 0; j < i; ++j)
            if (values[j] == (size_t)raw) {
                free(values);
                return QUANTAPDF_ERROR_FORMAT;
            }
        values[i] = (size_t)raw;
    }
    *out_indices = values;
    *out_count = (size_t)count;
    return QUANTAPDF_OK;
}

static quantapdf_status collect_v_strings(
    fz_context *ctx,
    pdf_obj *v,
    const char ***out_values,
    size_t *out_count)
{
    const char **values = NULL;
    int i;
    int count;
    *out_values = NULL;
    *out_count = 0;
    if (v == NULL)
        return QUANTAPDF_OK;
    if (pdf_is_string(ctx, v)) {
        values = (const char **)calloc(1, sizeof(*values));
        if (values == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        values[0] = pdf_to_text_string(ctx, v);
        if (values[0] == NULL) {
            free(values);
            return QUANTAPDF_ERROR_FORMAT;
        }
        *out_values = values;
        *out_count = 1;
        return QUANTAPDF_OK;
    }
    if (!pdf_is_array(ctx, v))
        return QUANTAPDF_ERROR_FORMAT;
    count = pdf_array_len(ctx, v);
    if (count != 0) {
        values = (const char **)calloc((size_t)count, sizeof(*values));
        if (values == NULL)
            return QUANTAPDF_ERROR_NOMEM;
    }
    for (i = 0; i < count; ++i) {
        pdf_obj *item = pdf_array_get(ctx, v, i);
        if (!pdf_is_string(ctx, item)) {
            free(values);
            return QUANTAPDF_ERROR_FORMAT;
        }
        values[i] = pdf_to_text_string(ctx, item);
        if (values[i] == NULL) {
            free(values);
            return QUANTAPDF_ERROR_FORMAT;
        }
    }
    *out_values = values;
    *out_count = (size_t)count;
    return QUANTAPDF_OK;
}

static quantapdf_status materialize_choice_value(
    fz_context *ctx,
    quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_field_internal *field,
    pdf_obj *v,
    pdf_obj *indices,
    int editable)
{
    size_t *selected = NULL;
    size_t selected_count = 0;
    const char **v_values = NULL;
    size_t v_count = 0;
    quantapdf_status status;
    size_t i;

    field->value_count = 0;
    if (v == NULL) {
        if (indices != NULL)
            return QUANTAPDF_ERROR_FORMAT;
        field->value_presence = QUANTAPDF_FORM_VALUE_MISSING;
        return QUANTAPDF_OK;
    }
    field->value_presence = QUANTAPDF_FORM_VALUE_PRESENT;
    status = validate_i(ctx, indices, field->option_count, &selected, &selected_count);
    if (status != QUANTAPDF_OK)
        return status;
    status = collect_v_strings(ctx, v, &v_values, &v_count);
    if (status != QUANTAPDF_OK) {
        free(selected);
        return status;
    }

    if (indices != NULL) {
        if (selected_count != v_count) {
            status = QUANTAPDF_ERROR_FORMAT;
            goto done;
        }
        for (i = 0; i < selected_count; ++i) {
            if (strcmp(option_export(model, field, selected[i]), v_values[i]) != 0) {
                status = QUANTAPDF_ERROR_FORMAT;
                goto done;
            }
            status = append_option_value(model, field, selected[i]);
            if (status != QUANTAPDF_OK)
                goto done;
        }
        status = QUANTAPDF_OK;
        goto done;
    }

    if (v_count == 0) {
        status = QUANTAPDF_OK;
        goto done;
    }
    for (i = 0; i < v_count; ++i) {
        size_t oi;
        size_t match = SIZE_MAX;
        for (oi = 0; oi < field->option_count; ++oi) {
            if (strcmp(option_export(model, field, oi), v_values[i]) == 0) {
                if (match != SIZE_MAX) {
                    status = QUANTAPDF_ERROR_FORMAT;
                    goto done;
                }
                match = oi;
            }
        }
        if (match == SIZE_MAX) {
            if (editable && v_count == 1) {
                status = append_utf8_value(model, field, v_values[0]);
                goto done;
            }
            status = QUANTAPDF_ERROR_FORMAT;
            goto done;
        }
        status = append_option_value(model, field, match);
        if (status != QUANTAPDF_OK)
            goto done;
    }
    status = QUANTAPDF_OK;

done:
    free(selected);
    free(v_values);
    return status;
}

quantapdf_status quantapdf_pdf_form_materialize_choice_values(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_form_model *model)
{
    pdf_obj *fields;
    size_t index;
    if (ctx == NULL || document == NULL || model == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    fields = pdf_dict_getp(ctx, pdf_trailer(ctx, document), "Root/AcroForm/Fields");
    if (!pdf_is_array(ctx, fields))
        return QUANTAPDF_OK;

    for (index = 0; index < model->field_count; ++index) {
        quantapdf_pdf_form_field_internal *field = &model->fields[index];
        pdf_obj *source;
        pdf_obj *opt;
        pdf_obj *v;
        pdf_obj *indices;
        int editable;
        quantapdf_status status;
        if (field->type != QUANTAPDF_FORM_FIELD_COMBO_BOX &&
            field->type != QUANTAPDF_FORM_FIELD_LIST_BOX)
            continue;
        source = find_field_source(ctx, fields, model, index);
        if (source == NULL)
            return QUANTAPDF_ERROR_FORMAT;
        opt = effective_raw(ctx, source, PDF_NAME(Opt));
        v = effective_raw(ctx, source, PDF_NAME(V));
        indices = effective_raw(ctx, source, PDF_NAME(I));
        field->is_multiselect = field->type == QUANTAPDF_FORM_FIELD_LIST_BOX &&
            (field->flags & (UINT32_C(1) << 21)) != 0;
        editable = field->type == QUANTAPDF_FORM_FIELD_COMBO_BOX &&
            (field->flags & (UINT32_C(1) << 18)) != 0;
        status = parse_options(ctx, model, field, opt);
        if (status != QUANTAPDF_OK)
            return status;
        status = materialize_choice_value(ctx, model, field, v, indices, editable);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}
