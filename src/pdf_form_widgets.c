#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

typedef struct quantapdf_expected_widget {
    int num;
    int gen;
    size_t field_index;
    size_t seen;
} quantapdf_expected_widget;

typedef struct quantapdf_widget_state {
    char *name;
} quantapdf_widget_state;

static int is_widget(fz_context *ctx, pdf_obj *obj)
{
    pdf_obj *subtype = NULL;
    return pdf_is_dict(ctx, obj) &&
        quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(Subtype), &subtype) &&
        pdf_is_name(ctx, subtype) && pdf_name_eq(ctx, subtype, PDF_NAME(Widget));
}

static quantapdf_status build_field_name(
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

static quantapdf_status find_field_index(
    quantapdf_pdf_form_model *model,
    const char *name,
    size_t size,
    int present,
    size_t *out_index)
{
    size_t i;
    size_t match = SIZE_MAX;
    for (i = 0; i < model->field_count; ++i) {
        const quantapdf_pdf_form_string *field_name = &model->fields[i].name;
        if (field_name->present != present)
            continue;
        if (!present) {
            if (match != SIZE_MAX)
                return QUANTAPDF_ERROR_FORMAT;
            match = i;
            continue;
        }
        if (field_name->size == size &&
            memcmp(model->strings + field_name->offset, name, size) == 0) {
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

static quantapdf_status append_expected(
    quantapdf_expected_widget **items,
    size_t *count,
    size_t *capacity,
    int num,
    int gen,
    size_t field_index)
{
    quantapdf_expected_widget *grown;
    size_t cap;
    size_t i;
    for (i = 0; i < *count; ++i)
        if ((*items)[i].num == num && (*items)[i].gen == gen)
            return QUANTAPDF_ERROR_FORMAT;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2 : 16;
        if (cap < *capacity || cap > SIZE_MAX / sizeof(**items))
            return QUANTAPDF_ERROR_NOMEM;
        grown = (quantapdf_expected_widget *)realloc(*items, cap * sizeof(**items));
        if (grown == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        *items = grown;
        *capacity = cap;
    }
    (*items)[*count].num = num;
    (*items)[*count].gen = gen;
    (*items)[*count].field_index = field_index;
    (*items)[*count].seen = 0;
    ++*count;
    return QUANTAPDF_OK;
}

static quantapdf_status collect_expected_node(
    fz_context *ctx,
    pdf_obj *obj,
    quantapdf_pdf_form_model *model,
    quantapdf_expected_widget **items,
    size_t *count,
    size_t *capacity,
    size_t depth)
{
    pdf_obj *kids = NULL;
    int i, n;
    if (depth > 256)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (!pdf_is_dict(ctx, obj))
        return QUANTAPDF_ERROR_FORMAT;
    if (is_widget(ctx, obj)) {
        char *name = NULL;
        size_t size = 0;
        int present = 0;
        size_t field_index;
        quantapdf_status status;
        if (!pdf_is_indirect(ctx, obj))
            return QUANTAPDF_ERROR_FORMAT;
        status = build_field_name(ctx, obj, &name, &size, &present);
        if (status == QUANTAPDF_OK)
            status = find_field_index(model, name, size, present, &field_index);
        if (status == QUANTAPDF_OK)
            status = append_expected(items, count, capacity,
                pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj), field_index);
        free(name);
        return status;
    }
    if (!quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(Kids), &kids))
        return QUANTAPDF_OK;
    if (!pdf_is_array(ctx, kids))
        return QUANTAPDF_ERROR_FORMAT;
    n = pdf_array_len(ctx, kids);
    for (i = 0; i < n; ++i) {
        quantapdf_status status = collect_expected_node(
            ctx, pdf_array_get(ctx, kids, i), model, items, count, capacity, depth + 1);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status collect_expected(
    fz_context *ctx,
    pdf_document *doc,
    quantapdf_pdf_form_model *model,
    quantapdf_expected_widget **items,
    size_t *count)
{
    pdf_obj *fields = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/AcroForm/Fields");
    size_t capacity = 0;
    int i, n;
    *items = NULL;
    *count = 0;
    if (!pdf_is_array(ctx, fields))
        return QUANTAPDF_OK;
    n = pdf_array_len(ctx, fields);
    for (i = 0; i < n; ++i) {
        quantapdf_status status = collect_expected_node(
            ctx, pdf_array_get(ctx, fields, i), model, items, count, &capacity, 1);
        if (status != QUANTAPDF_OK) {
            free(*items);
            *items = NULL;
            *count = 0;
            return status;
        }
    }
    return QUANTAPDF_OK;
}

static quantapdf_expected_widget *find_expected(
    quantapdf_expected_widget *items, size_t count, int num, int gen)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (items[i].num == num && items[i].gen == gen)
            return &items[i];
    return NULL;
}

static quantapdf_status read_button_state(
    fz_context *ctx,
    pdf_obj *widget,
    quantapdf_form_field_type type,
    char **out_state)
{
    pdf_obj *ap = NULL;
    pdf_obj *normal = NULL;
    int found = 0;
    int i, n;
    *out_state = NULL;
    if (type != QUANTAPDF_FORM_FIELD_CHECKBOX &&
        type != QUANTAPDF_FORM_FIELD_RADIO_BUTTON)
        return QUANTAPDF_OK;
    if (!quantapdf_pdf_dict_find(ctx, widget, PDF_NAME(AP), &ap))
        return QUANTAPDF_OK;
    if (!pdf_is_dict(ctx, ap))
        return QUANTAPDF_ERROR_FORMAT;
    if (!quantapdf_pdf_dict_find(ctx, ap, PDF_NAME(N), &normal))
        return QUANTAPDF_OK;
    if (!pdf_is_dict(ctx, normal))
        return QUANTAPDF_ERROR_FORMAT;
    n = pdf_dict_len(ctx, normal);
    for (i = 0; i < n; ++i) {
        pdf_obj *key = pdf_dict_get_key(ctx, normal, i);
        const char *name;
        if (!pdf_is_name(ctx, key))
            return QUANTAPDF_ERROR_FORMAT;
        name = pdf_to_name(ctx, key);
        if (strcmp(name, "Off") == 0)
            continue;
        if (found)
            return QUANTAPDF_ERROR_FORMAT;
        *out_state = (char *)malloc(strlen(name) + 1);
        if (*out_state == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        strcpy(*out_state, name);
        found = 1;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status append_widget(
    quantapdf_pdf_form_model *model,
    const quantapdf_pdf_form_widget_internal *widget,
    const char *state,
    quantapdf_widget_state **states,
    size_t *capacity)
{
    quantapdf_pdf_form_widget_internal *grown_widgets;
    quantapdf_widget_state *grown_states;
    size_t cap;
    if (model->widget_count == *capacity) {
        cap = *capacity ? *capacity * 2 : 16;
        if (cap < *capacity || cap > SIZE_MAX / sizeof(*model->widgets))
            return QUANTAPDF_ERROR_NOMEM;
        grown_widgets = (quantapdf_pdf_form_widget_internal *)realloc(
            model->widgets, cap * sizeof(*model->widgets));
        if (grown_widgets == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        model->widgets = grown_widgets;
        grown_states = (quantapdf_widget_state *)realloc(*states, cap * sizeof(**states));
        if (grown_states == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        *states = grown_states;
        *capacity = cap;
    }
    model->widgets[model->widget_count] = *widget;
    (*states)[model->widget_count].name = NULL;
    if (state != NULL) {
        (*states)[model->widget_count].name = (char *)malloc(strlen(state) + 1);
        if ((*states)[model->widget_count].name == NULL)
            return QUANTAPDF_ERROR_NOMEM;
        strcpy((*states)[model->widget_count].name, state);
    }
    ++model->widget_count;
    return QUANTAPDF_OK;
}

static quantapdf_status add_button_options(
    quantapdf_pdf_form_model *model,
    quantapdf_widget_state *states)
{
    size_t field_index;
    for (field_index = 0; field_index < model->field_count; ++field_index) {
        quantapdf_pdf_form_field_internal *field = &model->fields[field_index];
        size_t wi;
        if (field->type != QUANTAPDF_FORM_FIELD_CHECKBOX &&
            field->type != QUANTAPDF_FORM_FIELD_RADIO_BUTTON)
            continue;
        field->first_option = model->option_count;
        for (wi = 0; wi < model->widget_count; ++wi) {
            size_t oi;
            quantapdf_pdf_form_option_internal *grown;
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
                    return QUANTAPDF_ERROR_NOMEM;
                grown = (quantapdf_pdf_form_option_internal *)realloc(
                    model->options, (model->option_count + 1) * sizeof(*model->options));
                if (grown == NULL)
                    return QUANTAPDF_ERROR_NOMEM;
                model->options = grown;
                memset(&model->options[model->option_count], 0,
                    sizeof(model->options[model->option_count]));
                model->options[model->option_count].kind = QUANTAPDF_FORM_OPTION_BUTTON_STATE;
                model->options[model->option_count].button_state =
                    (char *)malloc(strlen(states[wi].name) + 1);
                if (model->options[model->option_count].button_state == NULL)
                    return QUANTAPDF_ERROR_NOMEM;
                strcpy(model->options[model->option_count].button_state, states[wi].name);
                ++model->option_count;
                ++field->option_count;
            }
            model->widgets[wi].button_option_index = oi;
        }
    }
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_form_reconcile_widgets(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_form_model *model)
{
    quantapdf_expected_widget *expected = NULL;
    quantapdf_widget_state *states = NULL;
    size_t expected_count = 0;
    size_t state_capacity = 0;
    quantapdf_status status;
    int page_count;
    int page_index;
    size_t i;

    if (ctx == NULL || document == NULL || model == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = collect_expected(ctx, document, model, &expected, &expected_count);
    if (status != QUANTAPDF_OK)
        return status;
    page_count = pdf_count_pages(ctx, document);
    for (page_index = 0; page_index < page_count && status == QUANTAPDF_OK; ++page_index) {
        pdf_obj *page_obj = pdf_lookup_page_obj(ctx, document, page_index);
        pdf_obj *annots = NULL;
        fz_matrix page_ctm;
        int ai, acount = 0;
        if (!pdf_is_dict(ctx, page_obj)) {
            status = QUANTAPDF_ERROR_FORMAT;
            break;
        }
        pdf_page_obj_transform(ctx, page_obj, NULL, &page_ctm);
        if (quantapdf_pdf_dict_find(ctx, page_obj, PDF_NAME(Annots), &annots) &&
            pdf_is_array(ctx, annots))
            acount = pdf_array_len(ctx, annots);
        for (ai = 0; ai < acount && status == QUANTAPDF_OK; ++ai) {
            pdf_obj *obj = pdf_array_get(ctx, annots, ai);
            quantapdf_expected_widget *match;
            quantapdf_pdf_form_widget_internal widget;
            pdf_obj *p = NULL;
            char *state = NULL;
            if (!pdf_is_dict(ctx, obj) || !is_widget(ctx, obj))
                continue;
            if (!pdf_is_indirect(ctx, obj)) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            match = find_expected(expected, expected_count,
                pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
            if (match == NULL || match->seen != 0) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            if (quantapdf_pdf_dict_find(ctx, obj, PDF_NAME(P), &p) &&
                !pdf_is_null(ctx, p) && pdf_objcmp_resolve(ctx, p, page_obj) != 0) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
            memset(&widget, 0, sizeof(widget));
            widget.field_index = match->field_index;
            widget.page_index = page_index;
            widget.button_option_index = SIZE_MAX;
            status = quantapdf_pdf_read_rect(
                ctx, obj, PDF_NAME(Rect), page_ctm, &widget.bounds);
            if (status == QUANTAPDF_OK)
                status = quantapdf_pdf_read_optional_uint32(
                    ctx, obj, PDF_NAME(F), 0, &widget.flags);
            if (status == QUANTAPDF_OK)
                status = read_button_state(ctx, obj,
                    model->fields[match->field_index].type, &state);
            if (status == QUANTAPDF_OK)
                status = append_widget(model, &widget, state, &states, &state_capacity);
            free(state);
            if (status == QUANTAPDF_OK) {
                ++match->seen;
                ++model->fields[match->field_index].widget_count;
            }
        }
    }
    if (status == QUANTAPDF_OK)
        for (i = 0; i < expected_count; ++i)
            if (expected[i].seen != 1) {
                status = QUANTAPDF_ERROR_FORMAT;
                break;
            }
    if (status == QUANTAPDF_OK)
        status = add_button_options(model, states);
    if (states != NULL) {
        for (i = 0; i < model->widget_count; ++i)
            free(states[i].name);
    }
    free(states);
    free(expected);
    return status;
}
