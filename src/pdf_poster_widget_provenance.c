#include "pdf_poster_internal.h"

#include "pdf_form_common.h"

#include <stddef.h>

static int poster_has_planned_widget(const quantapdf_pdf_poster_plan *plan)
{
    size_t split_index;

    for (split_index = 0; split_index < plan->split_count; ++split_index) {
        const quantapdf_pdf_poster_split_plan *split = &plan->splits[split_index];
        size_t annot_index;
        for (annot_index = 0; annot_index < split->annot_count; ++annot_index) {
            if (split->annots[annot_index].kind ==
                QUANTAPDF_PDF_POSTER_ANNOT_WIDGET)
                return 1;
        }
    }
    return 0;
}

static quantapdf_status poster_assign_widget_locator(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_provenance *provenance,
    int page_index,
    quantapdf_pdf_poster_annot_plan *annot_plan)
{
    pdf_obj *page;
    pdf_obj *annots;
    pdf_obj *widget;
    size_t matched_field = SIZE_MAX;
    size_t matched_widget = SIZE_MAX;
    size_t match_count = 0;
    size_t field_index;

    if (model == NULL || provenance == NULL ||
        model->field_count != provenance->field_count)
        return QUANTAPDF_ERROR_FORMAT;

    page = pdf_lookup_page_obj(ctx, document, page_index);
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    if (!pdf_is_array(ctx, annots) ||
        annot_plan->source_annot_index >= (size_t)pdf_array_len(ctx, annots))
        return QUANTAPDF_ERROR_FORMAT;
    widget = pdf_array_get(ctx, annots, (int)annot_plan->source_annot_index);
    if (!pdf_is_indirect(ctx, widget) || !pdf_is_dict(ctx, widget) ||
        !pdf_name_eq(
            ctx, pdf_dict_get(ctx, widget, PDF_NAME(Subtype)), PDF_NAME(Widget)))
        return QUANTAPDF_ERROR_FORMAT;

    for (field_index = 0; field_index < provenance->field_count; ++field_index) {
        const quantapdf_pdf_form_live_field *live_field =
            &provenance->fields[field_index];
        size_t widget_index;

        for (widget_index = 0; widget_index < live_field->widget_count;
             ++widget_index) {
            const quantapdf_pdf_form_live_widget *live_widget =
                &live_field->widgets[widget_index];
            if (!quantapdf_pdf_form_same_identity(
                    ctx, live_widget->object, widget))
                continue;
            if (live_widget->page_index != page_index)
                return QUANTAPDF_ERROR_FORMAT;
            matched_field = field_index;
            matched_widget = widget_index;
            ++match_count;
        }
    }

    if (match_count != 1)
        return QUANTAPDF_ERROR_FORMAT;

    annot_plan->form_field_index = matched_field;
    annot_plan->form_widget_index = matched_widget;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_poster_widget_provenance_preflight(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_poster_plan *plan)
{
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_pdf_form_provenance *provenance = NULL;
    quantapdf_status status;
    size_t split_index;

    if (ctx == NULL || document == NULL || plan == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!poster_has_planned_widget(plan))
        return QUANTAPDF_OK;

    fz_var(model);
    fz_var(provenance);
    fz_var(status);
    fz_try(ctx)
    {
        status = quantapdf_pdf_form_build(
            ctx, document, 1, &model, &provenance);
        if (status == QUANTAPDF_OK &&
            (model == NULL || provenance == NULL ||
             model->field_count != provenance->field_count))
            status = QUANTAPDF_ERROR_FORMAT;

        if (status == QUANTAPDF_OK)
            status = quantapdf_pdf_form_capture_provenance_widgets(
                ctx, document, model, provenance);

        for (split_index = 0;
             status == QUANTAPDF_OK && split_index < plan->split_count;
             ++split_index) {
            quantapdf_pdf_poster_split_plan *split = &plan->splits[split_index];
            size_t annot_index;
            for (annot_index = 0;
                 status == QUANTAPDF_OK && annot_index < split->annot_count;
                 ++annot_index) {
                quantapdf_pdf_poster_annot_plan *annot_plan =
                    &split->annots[annot_index];
                if (annot_plan->kind != QUANTAPDF_PDF_POSTER_ANNOT_WIDGET)
                    continue;
                status = poster_assign_widget_locator(
                    ctx,
                    document,
                    model,
                    provenance,
                    split->page_index,
                    annot_plan);
            }
        }
    }
    fz_always(ctx)
    {
        quantapdf_pdf_form_drop_provenance(ctx, provenance);
        quantapdf_pdf_form_drop_model(model);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
    return status;
}
