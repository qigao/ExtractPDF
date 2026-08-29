#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

extractpdf_status extractpdf_pdf_edit_form_restore_widget_editing(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles)
{
    extractpdf_status status = EXTRACTPDF_OK;
    size_t i;

    if (edit == NULL || edit->ctx == NULL || handles == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    for (i = 0; i < handles->count; ++i) {
        int caught_code = FZ_ERROR_NONE;
        if (!handles->items[i].editing_active)
            continue;
        fz_var(caught_code);
        fz_try(edit->ctx)
        {
            pdf_set_widget_editing_state(
                edit->ctx,
                handles->items[i].widget,
                handles->items[i].previous_editing);
        }
        fz_catch(edit->ctx)
        {
            caught_code = fz_caught(edit->ctx);
            fz_report_error(edit->ctx);
        }
        handles->items[i].editing_active = 0;
        if (caught_code != FZ_ERROR_NONE && status == EXTRACTPDF_OK)
            status = extractpdf_status_from_mupdf(caught_code);
    }
    return status;
}

void extractpdf_pdf_edit_form_drop_widget_handles(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles)
{
    size_t i;

    if (handles == NULL)
        return;
    if (edit != NULL && edit->ctx != NULL) {
        (void)extractpdf_pdf_edit_form_restore_widget_editing(edit, handles);
        for (i = 0; i < handles->page_count; ++i)
            if (handles->pages[i] != NULL)
                fz_drop_page(edit->ctx, (fz_page *)handles->pages[i]);
    }
    free(handles->page_indices);
    free(handles->pages);
    free(handles->items);
    memset(handles, 0, sizeof(*handles));
}

extractpdf_status extractpdf_pdf_edit_form_prepare_widget_handles(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_live_field *live,
    extractpdf_pdf_edit_form_widget_handles *out_handles)
{
    size_t target;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status = EXTRACTPDF_OK;

    if (out_handles == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    memset(out_handles, 0, sizeof(*out_handles));
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL || live == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (live->widget_count == 0)
        return EXTRACTPDF_OK;
    if (live->widget_count > SIZE_MAX / sizeof(*out_handles->items) ||
        live->widget_count > SIZE_MAX / sizeof(*out_handles->pages) ||
        live->widget_count > SIZE_MAX / sizeof(*out_handles->page_indices))
        return EXTRACTPDF_ERROR_NOMEM;

    out_handles->items = (extractpdf_pdf_edit_form_widget_handle *)calloc(
        live->widget_count, sizeof(*out_handles->items));
    out_handles->pages = (pdf_page **)calloc(
        live->widget_count, sizeof(*out_handles->pages));
    out_handles->page_indices = (int *)calloc(
        live->widget_count, sizeof(*out_handles->page_indices));
    if (out_handles->items == NULL || out_handles->pages == NULL ||
        out_handles->page_indices == NULL) {
        extractpdf_pdf_edit_form_drop_widget_handles(edit, out_handles);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    out_handles->count = live->widget_count;

    fz_var(status);
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        for (target = 0; target < live->widget_count && status == EXTRACTPDF_OK; ++target) {
            int page_index = live->widgets[target].page_index;
            pdf_page *page = NULL;
            pdf_annot *widget;
            size_t pi;
            size_t matches = 0;

            for (pi = 0; pi < out_handles->page_count; ++pi) {
                if (out_handles->page_indices[pi] == page_index) {
                    page = out_handles->pages[pi];
                    break;
                }
            }
            if (page == NULL) {
                page = pdf_load_page(edit->ctx, edit->document, page_index);
                if (page == NULL) {
                    status = EXTRACTPDF_ERROR_STATE;
                    break;
                }
                out_handles->pages[out_handles->page_count] = page;
                out_handles->page_indices[out_handles->page_count] = page_index;
                ++out_handles->page_count;
            }

            for (widget = pdf_first_widget(edit->ctx, page);
                 widget != NULL;
                 widget = pdf_next_widget(edit->ctx, widget)) {
                if (!extractpdf_pdf_form_same_identity(
                        edit->ctx,
                        pdf_annot_obj(edit->ctx, widget),
                        live->widgets[target].object))
                    continue;
                out_handles->items[target].widget = widget;
                ++matches;
            }
            if (matches != 1)
                status = EXTRACTPDF_ERROR_STATE;
        }
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        status = extractpdf_status_from_mupdf(caught_code);
    if (status != EXTRACTPDF_OK)
        extractpdf_pdf_edit_form_drop_widget_handles(edit, out_handles);
    return status;
}

extractpdf_status extractpdf_pdf_edit_form_begin_widget_editing(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles)
{
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status restore_status;
    size_t i;

    if (edit == NULL || edit->ctx == NULL || handles == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    fz_var(caught_code);
    fz_try(edit->ctx)
    {
        for (i = 0; i < handles->count; ++i) {
            if (handles->items[i].widget == NULL)
                fz_throw(edit->ctx, FZ_ERROR_GENERIC,
                    "missing prepared form Widget");
            handles->items[i].previous_editing =
                pdf_get_widget_editing_state(edit->ctx, handles->items[i].widget);
            pdf_set_widget_editing_state(edit->ctx, handles->items[i].widget, 1);
            handles->items[i].editing_active = 1;
        }
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }
    if (caught_code == FZ_ERROR_NONE)
        return EXTRACTPDF_OK;
    restore_status = extractpdf_pdf_edit_form_restore_widget_editing(edit, handles);
    (void)restore_status;
    return extractpdf_status_from_mupdf(caught_code);
}

void extractpdf_pdf_edit_form_refresh_widget_handles(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles)
{
    size_t i;

    for (i = 0; i < handles->count; ++i) {
        pdf_annot_request_resynthesis(edit->ctx, handles->items[i].widget);
        (void)pdf_update_widget(edit->ctx, handles->items[i].widget);
#if defined(EXTRACTPDF_TESTING)
        if (edit->test_fault ==
            EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_FIRST_AP_REFRESH) {
            edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
            fz_throw(edit->ctx, FZ_ERROR_GENERIC,
                "injected form failure after first AP refresh");
        }
#endif
    }
}
