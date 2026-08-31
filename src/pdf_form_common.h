#ifndef QUANTAPDF_PDF_FORM_COMMON_H
#define QUANTAPDF_PDF_FORM_COMMON_H

#include "pdf_object_common.h"
#include "form_snapshot.h"

typedef struct quantapdf_pdf_form_live_widget {
    pdf_obj *object;
    int page_index;
} quantapdf_pdf_form_live_widget;

typedef struct quantapdf_pdf_form_locator {
    size_t *steps;
    size_t step_count;
} quantapdf_pdf_form_locator;

typedef struct quantapdf_pdf_form_live_field {
    quantapdf_pdf_form_locator locator;
    pdf_obj *group_head;
    pdf_obj **group_nodes;
    size_t group_node_count;
    pdf_obj *effective_v_owner;
    int effective_v_present;
    quantapdf_pdf_form_live_widget *widgets;
    size_t widget_count;
} quantapdf_pdf_form_live_field;

typedef struct quantapdf_pdf_form_provenance {
    quantapdf_pdf_form_live_field *fields;
    size_t field_count;
} quantapdf_pdf_form_provenance;

quantapdf_status quantapdf_pdf_form_parse(fz_context *ctx, pdf_document *document, quantapdf_pdf_form_model **out_model);
quantapdf_status quantapdf_pdf_form_reconcile_widgets(fz_context *ctx, pdf_document *document, quantapdf_pdf_form_model *model);
quantapdf_status quantapdf_pdf_form_capture_provenance_widgets(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_form_model *model,
    quantapdf_pdf_form_provenance *provenance);
quantapdf_status quantapdf_pdf_form_materialize_scalar_values(fz_context *ctx, pdf_document *document, quantapdf_pdf_form_model *model);
quantapdf_status quantapdf_pdf_form_materialize_choice_values(fz_context *ctx, pdf_document *document, quantapdf_pdf_form_model *model);
quantapdf_status quantapdf_pdf_form_build(
    fz_context *ctx,
    pdf_document *document,
    int want_provenance,
    quantapdf_pdf_form_model **out_model,
    quantapdf_pdf_form_provenance **out_provenance);
void quantapdf_pdf_form_drop_model(quantapdf_pdf_form_model *model);
void quantapdf_pdf_form_drop_provenance(
    fz_context *ctx,
    quantapdf_pdf_form_provenance *provenance);
int quantapdf_pdf_form_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right);

#endif
