#include "pdf_edit_internal.h"
#include "pdf_form_common.h"

#include <stdlib.h>
#include <string.h>

void extractpdf_pdf_edit_form_drop_widget_handles(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles)
{
    size_t i;

    if (handles == NULL)
        return;
    if (edit != NULL && edit->ctx != NULL) {
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
