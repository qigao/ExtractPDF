#ifndef QUANTAPDF_PDF_FORM_COMMON_H
#define QUANTAPDF_PDF_FORM_COMMON_H

#include "pdf_object_common.h"

typedef struct quantapdf_pdf_form_string { size_t offset; size_t size; int present; } quantapdf_pdf_form_string;
typedef struct quantapdf_pdf_form_value_internal { quantapdf_form_value_kind kind; size_t option_index; quantapdf_pdf_form_string utf8; } quantapdf_pdf_form_value_internal;
typedef struct quantapdf_pdf_form_option_internal { quantapdf_form_option_kind kind; quantapdf_pdf_form_string export_text; quantapdf_pdf_form_string display_text; char *button_state; } quantapdf_pdf_form_option_internal;
typedef struct quantapdf_pdf_form_field_internal { quantapdf_form_field_type type; uint32_t flags; quantapdf_form_value_presence value_presence; size_t first_value; size_t value_count; size_t first_option; size_t option_count; size_t widget_count; int is_multiselect; int is_signed; quantapdf_pdf_form_string name; quantapdf_pdf_form_string label; } quantapdf_pdf_form_field_internal;
typedef struct quantapdf_pdf_form_widget_internal { size_t field_index; int page_index; quantapdf_rect bounds; uint32_t flags; size_t button_option_index; } quantapdf_pdf_form_widget_internal;
typedef struct quantapdf_pdf_form_model { quantapdf_pdf_form_field_internal *fields; size_t field_count; quantapdf_pdf_form_value_internal *values; size_t value_count; quantapdf_pdf_form_option_internal *options; size_t option_count; quantapdf_pdf_form_widget_internal *widgets; size_t widget_count; char *strings; size_t string_size; size_t string_capacity; } quantapdf_pdf_form_model;

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
