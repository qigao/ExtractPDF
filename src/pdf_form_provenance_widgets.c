#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

static int quantapdf_pdf_form_live_is_widget(fz_context *ctx, pdf_obj *obj)
{
    pdf_obj *subtype = NULL;

    return pdf_is_dict(ctx, obj) &&
        quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(Subtype), &subtype) &&
        pdf_is_name(ctx, subtype) &&
        pdf_name_eq(ctx, subtype, PDF_NAME(Widget));
}

static quantapdf_status quantapdf_pdf_form_live_build_name(
    fz_context *ctx,
    pdf_obj *obj,
    char **out_name,
    size_t *out_size,
    int *out_present)
{
    const char *parts[257];
    size_t sizes[257];
    size_t count = 0;
    size_t total = 0;
    char *name;
    size_t i;

    *out_name = NULL;
    *out_size = 0;
    *out_present = 0;
    while (obj != NULL && !pdf_is_null(ctx, obj)) {
        pdf_obj *t = NULL;
        pdf_obj *parent = NULL;

        if (count > 256)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(T), &t)) {
            const char *text;
            size_t size;

            if (!pdf_is_string(ctx, t))
                return QUANTAPDF_ERROR_FORMAT;
            text = pdf_to_text_string(ctx, t);
            if (text == NULL || strchr(text, '.') != NULL)
                return QUANTAPDF_ERROR_FORMAT;
            size = strlen(text);
            parts[count] = text;
            sizes[count] = size;
            ++count;
            if (total > SIZE_MAX - size - 1)
                return QUANTAPDF_ERROR_NOMEM;
            total += size + 1;
        }
        if (!quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(Parent), &parent))
            break;
        obj = parent;
    }
    if (count == 0)
        return QUANTAPDF_OK;

    --total;
    name = (char *)malloc(total + 1);
    if (name == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    {
        size_t at = 0;
        for (i = count; i-- > 0;) {
            if (at != 0)
                name[at++] = '.';
            memcpy(name + at, parts[i], sizes[i]);
            at += sizes[i];
        }
        name[at] = '\0';
    }
    *out_name = name;
    *out_size = total;
    *out_present = 1;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_form_live_find_field(
    const quantapdf_pdf_form_model *model,
    const char *name,
    size_t size,
    int present,
    size_t *out_index)
{
    size_t i;
    size_t match = SIZE_MAX;

    for (i = 0; i < model->field_count; ++i) {
        const quantapdf_pdf_form_string *candidate = &model->fields[i].name;

        if (candidate->present != present)
            continue;
        if (!present) {
            if (match != SIZE_MAX)
                return QUANTAPDF_ERROR_FORMAT;
            match = i;
            continue;
        }
        if (candidate->size == size &&
            memcmp(model->strings + candidate->offset, name, size) == 0) {
            if (match != SIZE_MAX)
                return QUANTAPDF_ERROR_FORMAT;
            match = i;
        }
    }
    if (match == SIZE_MAX)
        return QUANTAPDF_ERROR_FORMAT;
    *out_index = match;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_pdf_form_live_append(
    fz_context *ctx,
    quantapdf_pdf_form_live_field *field,
    pdf_obj *object,
    int page_index)
{
    quantapdf_pdf_form_live_widget *grown;
    size_t next;

    if (field->widget_count == SIZE_MAX ||
        field->widget_count + 1 > SIZE_MAX / sizeof(*field->widgets))
        return QUANTAPDF_ERROR_NOMEM;
    next = field->widget_count + 1;
    grown = (quantapdf_pdf_form_live_widget *)realloc(
        field->widgets, next * sizeof(*field->widgets));
    if (grown == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    field->widgets = grown;
    field->widgets[field->widget_count].object = pdf_keep_obj(ctx, object);
    field->widgets[field->widget_count].page_index = page_index;
    field->widget_count = next;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_form_capture_provenance_widgets(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_provenance *provenance)
{
    size_t widget_index = 0;
    int page_count;
    int page_index;

    if (ctx == NULL || document == NULL || model == NULL || provenance == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (provenance->field_count != model->field_count)
        return QUANTAPDF_ERROR_FORMAT;
    for (widget_index = 0; widget_index < provenance->field_count; ++widget_index)
        if (provenance->fields[widget_index].widget_count != 0 ||
            provenance->fields[widget_index].widgets != NULL)
            return QUANTAPDF_ERROR_STATE;

    widget_index = 0;
    page_count = pdf_count_pages(ctx, document);
    for (page_index = 0; page_index < page_count; ++page_index) {
        pdf_obj *page_obj = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = NULL;
        int ai;
        int count = 0;

        if (!pdf_is_dict(ctx, page_obj))
            return QUANTAPDF_ERROR_FORMAT;
        if (quantapdf_pdf_dict_find(ctx, page_obj, PDF_NAME(Annots), &annots) &&
            pdf_is_array(ctx, annots))
            count = pdf_array_len(ctx, annots);
        for (ai = 0; ai < count; ++ai) {
            pdf_obj *obj = pdf_array_get(ctx, annots, ai);
            const quantapdf_pdf_form_widget_internal *expected;
            char *name = NULL;
            size_t name_size = 0;
            size_t field_index = SIZE_MAX;
            int name_present = 0;
            quantapdf_status status;

            if (!pdf_is_dict(ctx, obj) || !quantapdf_pdf_form_live_is_widget(ctx, obj))
                continue;
            if (!pdf_is_indirect(ctx, obj))
                return QUANTAPDF_ERROR_FORMAT;
            if (widget_index >= model->widget_count)
                return QUANTAPDF_ERROR_STATE;

            status = quantapdf_pdf_form_live_build_name(
                ctx, obj, &name, &name_size, &name_present);
            if (status == QUANTAPDF_OK)
                status = quantapdf_pdf_form_live_find_field(
                    model, name, name_size, name_present, &field_index);
            free(name);
            if (status != QUANTAPDF_OK)
                return status;

            expected = &model->widgets[widget_index];
            if (expected->field_index != field_index ||
                expected->page_index != page_index)
                return QUANTAPDF_ERROR_STATE;
            status = quantapdf_pdf_form_live_append(
                ctx, &provenance->fields[field_index], obj, page_index);
            if (status != QUANTAPDF_OK)
                return status;
            ++widget_index;
        }
    }
    if (widget_index != model->widget_count)
        return QUANTAPDF_ERROR_STATE;
    return QUANTAPDF_OK;
}
