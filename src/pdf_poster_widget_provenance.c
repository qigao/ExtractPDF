#include "pdf_poster_internal.h"

#include "pdf_form_common.h"

#include <stddef.h>
#ifdef EXTRACTPDF_TESTING
#include <stdio.h>
#endif

static int poster_has_planned_widget(const extractpdf_pdf_poster_plan *plan)
{
    size_t split_index;

    for (split_index = 0; split_index < plan->split_count; ++split_index) {
        const extractpdf_pdf_poster_split_plan *split = &plan->splits[split_index];
        size_t annot_index;
        for (annot_index = 0; annot_index < split->annot_count; ++annot_index) {
            if (split->annots[annot_index].kind ==
                EXTRACTPDF_PDF_POSTER_ANNOT_WIDGET)
                return 1;
        }
    }
    return 0;
}

static extractpdf_status poster_assign_widget_locator(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model *model,
    extractpdf_pdf_form_provenance *provenance,
    int page_index,
    extractpdf_pdf_poster_annot_plan *annot_plan)
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
        return EXTRACTPDF_ERROR_FORMAT;

    page = pdf_lookup_page_obj(ctx, document, page_index);
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    if (!pdf_is_array(ctx, annots) ||
        annot_plan->source_annot_index >= (size_t)pdf_array_len(ctx, annots))
        return EXTRACTPDF_ERROR_FORMAT;
    widget = pdf_array_get(ctx, annots, (int)annot_plan->source_annot_index);
    if (!pdf_is_indirect(ctx, widget) || !pdf_is_dict(ctx, widget) ||
        !pdf_name_eq(
            ctx, pdf_dict_get(ctx, widget, PDF_NAME(Subtype)), PDF_NAME(Widget)))
        return EXTRACTPDF_ERROR_FORMAT;

#ifdef EXTRACTPDF_TESTING
    fprintf(stderr,
        "poster widget target: page=%d annot=%zu indirect=%d num=%d gen=%d\n",
        page_index,
        annot_plan->source_annot_index,
        pdf_is_indirect(ctx, widget),
        pdf_to_num(ctx, widget),
        pdf_to_gen(ctx, widget));
#endif

    for (field_index = 0; field_index < provenance->field_count; ++field_index) {
        const extractpdf_pdf_form_live_field *live_field =
            &provenance->fields[field_index];
        size_t widget_index;

        for (widget_index = 0; widget_index < live_field->widget_count;
             ++widget_index) {
            const extractpdf_pdf_form_live_widget *live_widget =
                &live_field->widgets[widget_index];
#ifdef EXTRACTPDF_TESTING
            fprintf(stderr,
                "poster widget live: field=%zu widget=%zu page=%d indirect=%d num=%d gen=%d\n",
                field_index,
                widget_index,
                live_widget->page_index,
                pdf_is_indirect(ctx, live_widget->object),
                pdf_to_num(ctx, live_widget->object),
                pdf_to_gen(ctx, live_widget->object));
#endif
            if (!extractpdf_pdf_form_same_identity(
                    ctx, live_widget->object, widget))
                continue;
            if (live_widget->page_index != page_index)
                return EXTRACTPDF_ERROR_FORMAT;
            matched_field = field_index;
            matched_widget = widget_index;
            ++match_count;
        }
    }

#ifdef EXTRACTPDF_TESTING
    if (match_count != 1)
        fprintf(stderr,
            "poster widget provenance locator: page=%d annot=%zu matches=%zu fields=%zu widgets=%zu\n",
            page_index,
            annot_plan->source_annot_index,
            match_count,
            model->field_count,
            model->widget_count);
#endif
    if (match_count != 1)
        return EXTRACTPDF_ERROR_FORMAT;

    annot_plan->form_field_index = matched_field;
    annot_plan->form_widget_index = matched_widget;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_poster_widget_provenance_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan)
{
    extractpdf_pdf_form_model *model = NULL;
    extractpdf_pdf_form_provenance *provenance = NULL;
    extractpdf_status status;
    size_t split_index;

    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (!poster_has_planned_widget(plan))
        return EXTRACTPDF_OK;

#ifdef EXTRACTPDF_TESTING
    fprintf(stderr, "poster widget provenance: start\n");
#endif
    status = extractpdf_pdf_form_build(
        ctx, document, 1, &model, &provenance);
#ifdef EXTRACTPDF_TESTING
    if (status != EXTRACTPDF_OK)
        fprintf(stderr, "poster widget provenance: form_build status=%d\n", (int)status);
#endif
    if (status != EXTRACTPDF_OK)
        return status;
    if (model == NULL || provenance == NULL ||
        model->field_count != provenance->field_count) {
        status = EXTRACTPDF_ERROR_FORMAT;
        goto cleanup;
    }

    for (split_index = 0; split_index < plan->split_count; ++split_index) {
        extractpdf_pdf_poster_split_plan *split = &plan->splits[split_index];
        size_t annot_index;
        for (annot_index = 0; annot_index < split->annot_count; ++annot_index) {
            extractpdf_pdf_poster_annot_plan *annot_plan =
                &split->annots[annot_index];
            if (annot_plan->kind != EXTRACTPDF_PDF_POSTER_ANNOT_WIDGET)
                continue;
            status = poster_assign_widget_locator(
                ctx,
                document,
                model,
                provenance,
                split->page_index,
                annot_plan);
            if (status != EXTRACTPDF_OK)
                goto cleanup;
        }
    }

#ifdef EXTRACTPDF_TESTING
    fprintf(stderr, "poster widget provenance: ok fields=%zu widgets=%zu\n",
        model->field_count, model->widget_count);
#endif
cleanup:
    extractpdf_pdf_form_drop_provenance(ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return status;
}
