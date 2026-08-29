#ifndef EXTRACTPDF_PDF_FORM_COMMON_H
#define EXTRACTPDF_PDF_FORM_COMMON_H

#include "pdf_object_common.h"

typedef struct extractpdf_pdf_form_string { size_t offset; size_t size; int present; } extractpdf_pdf_form_string;
typedef struct extractpdf_pdf_form_value_internal { extractpdf_form_value_kind kind; size_t option_index; extractpdf_pdf_form_string utf8; } extractpdf_pdf_form_value_internal;
typedef struct extractpdf_pdf_form_option_internal { extractpdf_form_option_kind kind; extractpdf_pdf_form_string export_text; extractpdf_pdf_form_string display_text; char *button_state; } extractpdf_pdf_form_option_internal;
typedef struct extractpdf_pdf_form_field_internal { extractpdf_form_field_type type; uint32_t flags; extractpdf_form_value_presence value_presence; size_t first_value; size_t value_count; size_t first_option; size_t option_count; size_t widget_count; int is_multiselect; int is_signed; extractpdf_pdf_form_string name; extractpdf_pdf_form_string label; } extractpdf_pdf_form_field_internal;
typedef struct extractpdf_pdf_form_widget_internal { size_t field_index; int page_index; extractpdf_rect bounds; uint32_t flags; size_t button_option_index; } extractpdf_pdf_form_widget_internal;
typedef struct extractpdf_pdf_form_model { extractpdf_pdf_form_field_internal *fields; size_t field_count; extractpdf_pdf_form_value_internal *values; size_t value_count; extractpdf_pdf_form_option_internal *options; size_t option_count; extractpdf_pdf_form_widget_internal *widgets; size_t widget_count; char *strings; size_t string_size; size_t string_capacity; } extractpdf_pdf_form_model;

extractpdf_status extractpdf_pdf_form_parse(fz_context *ctx, pdf_document *document, extractpdf_pdf_form_model **out_model);
extractpdf_status extractpdf_pdf_form_reconcile_widgets(fz_context *ctx, pdf_document *document, extractpdf_pdf_form_model *model);
extractpdf_status extractpdf_pdf_form_materialize_scalar_values(fz_context *ctx, pdf_document *document, extractpdf_pdf_form_model *model);
void extractpdf_pdf_form_drop_model(extractpdf_pdf_form_model *model);

#endif
