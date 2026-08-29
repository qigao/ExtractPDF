#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

typedef struct extractpdf_expected_widget {
    int num;
    int gen;
    size_t field_index;
    size_t seen;
} extractpdf_expected_widget;

typedef struct extractpdf_widget_state {
    char *name;
} extractpdf_widget_state;

static int is_widget(fz_context *ctx, pdf_obj *obj)
{
    pdf_obj *subtype = NULL;
    return pdf_is_dict(ctx, obj) &&
        extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(Subtype), &subtype) &&
        pdf_is_name(ctx, subtype) && pdf_name_eq(ctx, subtype, PDF_NAME(Widget));
}

static extractpdf_status build_field_name(
    fz_context *ctx, pdf_obj *obj, char **out_name, size_t *out_size, int *out_present)
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
            return EXTRACTPDF_ERROR_UNSUPPORTED;
        if (extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(T), &t)) {
            const char *text;
            size_t size;
            if (!pdf_is_string(ctx, t))
                return EXTRACTPDF_ERROR_FORMAT;
            text = pdf_to_text_string(ctx, t);
            if (text == NULL || strchr(text, '.') != NULL)
                return EXTRACTPDF_ERROR_FORMAT;
            size = strlen(text);
            parts[count] = text;
            sizes[count] = size;
            ++count;
            if (total > SIZE_MAX - size - 1)
                return EXTRACTPDF_ERROR_NOMEM;
            total += size + 1;
        }
        if (!extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(Parent), &parent))
            break;
        obj = parent;
    }
    if (count == 0)
        return EXTRACTPDF_OK;
    --total;
    name = (char *)malloc(total + 1);
    if (name == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
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
    return EXTRACTPDF_OK;
}

static extractpdf_status find_field_index(
    extractpdf_pdf_form_model *model,
    const char *name,
    size_t size,
    int present,
    size_t *out_index)
{
    size_t i;
    size_t match = SIZE_MAX;
    for (i = 0; i < model->field_count; ++i) {
        const extractpdf_pdf_form_string *field_name = &model->fields[i].name;
        if (field_name->present != present)
            continue;
        if (!present) {
            if (match != SIZE_MAX)
                return EXTRACTPDF_ERROR_FORMAT;
            match = i;
            continue;
        }
        if (field_name->size == size &&
            memcmp(model->strings + field_name->offset, name, size) == 0) {
            if (match != SIZE_MAX)
                return EXTRACTPDF_ERROR_FORMAT;
            match = i;
        }
    }
    if (match == SIZE_MAX)
        return EXTRACTPDF_ERROR_FORMAT;
    *out_index = match;
    return EXTRACTPDF_OK;
}

static extractpdf_status append_expected(
    extractpdf_expected_widget **items,
    size_t *count,
    size_t *capacity,
    int num,
    int gen,
    size_t field_index)
{
    extractpdf_expected_widget *grown;
    size_t cap;
    size_t i;
    for (i = 0; i < *count; ++i)
        if ((*items)[i].num == num && (*items)[i].gen == gen)
            return EXTRACTPDF_ERROR_FORMAT;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2 : 16;
        if (cap < *capacity || cap > SIZE_MAX / sizeof(**items))
            return EXTRACTPDF_ERROR_NOMEM;
        grown = (extractpdf_expected_widget *)realloc(*items, cap * sizeof(**items));
        if (grown == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        *items = grown;
        *capacity = cap;
    }
    (*items)[*count].num = num;
    (*items)[*count].gen = gen;
    (*items)[*count].field_index = field_index;
    (*items)[*count].seen = 0;
    ++*count;
    return EXTRACTPDF_OK;
}

static extractpdf_status collect_expected_node(
    fz_context *ctx,
    pdf_obj *obj,
    extractpdf_pdf_form_model *model,
    extractpdf_expected_widget **items,
    size_t *count,
    size_t *capacity,
    size_t depth)
{
    pdf_obj *kids = NULL;
    int i, n;
    if (depth > 256)
        return EXTRACTPDF_ERROR_UNSUPPORTED;
    if (!pdf_is_dict(ctx, obj))
        return EXTRACTPDF_ERROR_FORMAT;
    if (is_widget(ctx, obj)) {
        char *name = NULL;
        size_t size = 0;
        int present = 0;
        size_t field_index;
        extractpdf_status status;
        if (!pdf_is_indirect(ctx, obj))
            return EXTRACTPDF_ERROR_FORMAT;
        status = build_field_name(ctx, obj, &name, &size, &present);
        if (status == EXTRACTPDF_OK)
            status = find_field_index(model, name, size, present, &field_index);
        if (status == EXTRACTPDF_OK)
            status = append_expected(items, count, capacity,
                pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj), field_index);
        free(name);
        return status;
    }
    if (!extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(Kids), &kids))
        return EXTRACTPDF_OK;
    if (!pdf_is_array(ctx, kids))
        return EXTRACTPDF_ERROR_FORMAT;
    n = pdf_array_len(ctx, kids);
    for (i = 0; i < n; ++i) {
        extractpdf_status status = collect_expected_node(
            ctx, pdf_array_get(ctx, kids, i), model, items, count, capacity, depth + 1);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status collect_expected(
    fz_context *ctx,
    pdf_document *doc,
    extractpdf_pdf_form_model *model,
    extractpdf_expected_widget **items,
    size_t *count)
{
    pdf_obj *fields = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/AcroForm/Fields");
    size_t capacity = 0;
    int i, n;
    *items = NULL;
    *count = 0;
    if (!pdf_is_array(ctx, fields))
        return EXTRACTPDF_OK;
    n = pdf_array_len(ctx, fields);
    for (i = 0; i < n; ++i) {
        extractpdf_status status = collect_expected_node(
            ctx, pdf_array_get(ctx, fields, i), model, items, count, &capacity, 1);
        if (status != EXTRACTPDF_OK) {
            free(*items);
            *items = NULL;
            *count = 0;
            return status;
        }
    }
    return EXTRACTPDF_OK;
}

static extractpdf_expected_widget *find_expected(
    extractpdf_expected_widget *items, size_t count, int num, int gen)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (items[i].num == num && items[i].gen == gen)
            return &items[i];
    return NULL;
}

static extractpdf_status read_button_state(
    fz_context *ctx,
    pdf_obj *widget,
    extractpdf_form_field_type type,
    char **out_state)
{
    pdf_obj *ap = NULL;
    pdf_obj *normal = NULL;
    int found = 0;
    int i, n;
    *out_state = NULL;
    if (type != EXTRACTPDF_FORM_FIELD_CHECKBOX &&
        type != EXTRACTPDF_FORM_FIELD_RADIO_BUTTON)
        return EXTRACTPDF_OK;
    if (!extractpdf_pdf_dict_find(ctx, widget, PDF_NAME(AP), &ap))
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, ap))
        return EXTRACTPDF_ERROR_FORMAT;
    if (!extractpdf_pdf_dict_find(ctx, ap, PDF_NAME(N), &normal))
        return EXTRACTPDF_OK;
    if (!pdf_is_dict(ctx, normal))
        return EXTRACTPDF_ERROR_FORMAT;
    n = pdf_dict_len(ctx, normal);
    for (i = 0; i < n; ++i) {
        pdf_obj *key = pdf_dict_get_key(ctx, normal, i);
        const char *name;
        if (!pdf_is_name(ctx, key))
            return EXTRACTPDF_ERROR_FORMAT;
        name = pdf_to_name(ctx, key);
        if (strcmp(name, "Off") == 0)
            continue;
        if (found)
            return EXTRACTPDF_ERROR_FORMAT;
        *out_state = (char *)malloc(strlen(name) + 1);
        if (*out_state == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        strcpy(*out_state, name);
        found = 1;
    }
    return EXTRACTPDF_OK;
}

static extractpdf_status append_widget(
    extractpdf_pdf_form_model *model,
    const extractpdf_pdf_form_widget_internal *widget,
    const char *state,
    extractpdf_widget_state **states,
    size_t *capacity)
{
    extractpdf_pdf_form_widget_internal *grown_widgets;
    extractpdf_widget_state *grown_states;
    size_t cap;
    if (model->widget_count == *capacity) {
        cap = *capacity ? *capacity * 2 : 16;
        if (cap < *capacity || cap > SIZE_MAX / sizeof(*model->widgets))
            return EXTRACTPDF_ERROR_NOMEM;
        grown_widgets = (extractpdf_pdf_form_widget_internal *)realloc(
            model->widgets, cap * sizeof(*model->widgets));
        if (grown_widgets == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        model->widgets = grown_widgets;
        grown_states = (extractpdf_widget_state *)realloc(*states, cap * sizeof(**states));
        if (grown_states == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        *states = grown_states;
        *capacity = cap;
    }
    model->widgets[model->widget_count] = *widget;
    (*states)[model->widget_count].name = NULL;
    if (state != NULL) {
        (*states)[model->widget_count].name = (char *)malloc(strlen(state) + 1);
        if ((*states)[model->widget_count].name == NULL)
            return EXTRACTPDF_ERROR_NOMEM;
        strcpy((*states)[model->widget_count].name, state);
    }
    ++model->widget_count;
    return EXTRACTPDF_OK;
}

static extractpdf_status add_button_options(
    extractpdf_pdf_form_model *model,
    extractpdf_widget_state *states)
{
    size_t field_index;
    for (field_index = 0; field_index < model->field_count; ++field_index) {
        extractpdf_pdf_form_field_internal *field = &model->fields[field_index];
        size_t wi;
        if (field->type != EXTRACTPDF_FORM_FIELD_CHECKBOX &&
            field->type != EXTRACTPDF_FORM_FIELD_RADIO_BUTTON)
            continue;
        field->first_option = model->option_count;
        for (wi = 0; wi < model->widget_count; ++wi) {
            size_t oi;
            extractpdf_pdf_form_option_internal *grown;
            if (model->widgets[wi].field_index != field_index || states[wi].name == NULL)
                continue;
            for (oi = 0; oi < field->option_count; ++oi) {
                const char *existing = model->options[field->first_option + oi].button_state;
                if (existing != NULL && strcmp(existing, states[wi].name) == 0)
                    break;
            }
            if (oi == field->option_count) {
                if (model->option_count == SIZE_MAX ||
                    model->option_count + 1 > SIZE_MAX / sizeof(*model->options))
                    return EXTRACTPDF_ERROR_NOMEM;
                grown = (extractpdf_pdf_form_option_internal *)realloc(
                    model->options, (model->option_count + 1) * sizeof(*model->options));
                if (grown == NULL)
                    return EXTRACTPDF_ERROR_NOMEM;
                model->options = grown;
                memset(&model->options[model->option_count], 0,
                    sizeof(model->options[model->option_count]));
                model->options[model->option_count].kind = EXTRACTPDF_FORM_OPTION_BUTTON_STATE;
                model->options[model->option_count].button_state =
                    (char *)malloc(strlen(states[wi].name) + 1);
                if (model->options[model->option_count].button_state == NULL)
                    return EXTRACTPDF_ERROR_NOMEM;
                strcpy(model->options[model->option_count].button_state, states[wi].name);
                ++model->option_count;
                ++field->option_count;
            }
            model->widgets[wi].button_option_index = oi;
        }
    }
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_form_reconcile_widgets(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model *model)
{
    extractpdf_expected_widget *expected = NULL;
    extractpdf_widget_state *states = NULL;
    size_t expected_count = 0;
    size_t state_capacity = 0;
    extractpdf_status status;
    int page_count;
    int page_index;
    size_t i;

    if (ctx == NULL || document == NULL || model == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    status = collect_expected(ctx, document, model, &expected, &expected_count);
    if (status != EXTRACTPDF_OK)
        return status;
    page_count = pdf_count_pages(ctx, document);
    for (page_index = 0; page_index < page_count && status == EXTRACTPDF_OK; ++page_index) {
        pdf_page *page = pdf_load_page(ctx, document, page_index);
        pdf_obj *annots = NULL;
        int ai, acount = 0;
        if (page == NULL) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        if (extractpdf_pdf_dict_find(ctx, page->obj, PDF_NAME(Annots), &annots) &&
            pdf_is_array(ctx, annots))
            acount = pdf_array_len(ctx, annots);
        for (ai = 0; ai < acount && status == EXTRACTPDF_OK; ++ai) {
            pdf_obj *obj = pdf_array_get(ctx, annots, ai);
            extractpdf_expected_widget *match;
            extractpdf_pdf_form_widget_internal widget;
            pdf_obj *p = NULL;
            fz_matrix page_ctm;
            char *state = NULL;
            if (!pdf_is_dict(ctx, obj) || !is_widget(ctx, obj))
                continue;
            if (!pdf_is_indirect(ctx, obj)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            match = find_expected(expected, expected_count,
                pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
            if (match == NULL || match->seen != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            if (extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(P), &p) &&
                !pdf_is_null(ctx, p) && pdf_objcmp_resolve(ctx, p, page->obj) != 0) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            memset(&widget, 0, sizeof(widget));
            widget.field_index = match->field_index;
            widget.page_index = page_index;
            widget.button_option_index = SIZE_MAX;
            pdf_page_transform(ctx, page, NULL, &page_ctm);
            status = extractpdf_pdf_read_rect(
                ctx, obj, PDF_NAME(Rect), page_ctm, &widget.bounds);
            if (status == EXTRACTPDF_OK)
                status = extractpdf_pdf_read_optional_uint32(
                    ctx, obj, PDF_NAME(F), 0, &widget.flags);
            if (status == EXTRACTPDF_OK)
                status = read_button_state(ctx, obj,
                    model->fields[match->field_index].type, &state);
            if (status == EXTRACTPDF_OK)
                status = append_widget(model, &widget, state, &states, &state_capacity);
            free(state);
            if (status == EXTRACTPDF_OK) {
                ++match->seen;
                ++model->fields[match->field_index].widget_count;
            }
        }
        fz_drop_page(ctx, (fz_page *)page);
    }
    if (status == EXTRACTPDF_OK)
        for (i = 0; i < expected_count; ++i)
            if (expected[i].seen != 1) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
    if (status == EXTRACTPDF_OK)
        status = add_button_options(model, states);
    if (states != NULL) {
        for (i = 0; i < model->widget_count; ++i)
            free(states[i].name);
    }
    free(states);
    free(expected);
    return status;
}
