#ifndef EXTRACTPDF_PDF_EDIT_INTERNAL_H
#define EXTRACTPDF_PDF_EDIT_INTERNAL_H

#include "pdf_annotation_common.h"

typedef struct extractpdf_pdf_edit_annotation_entry {
    pdf_obj *object;
    int page_index;
    uint32_t tag;
    int live;
} extractpdf_pdf_edit_annotation_entry;

typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;

typedef struct extractpdf_pdf_edit_form_widget_handle {
    pdf_annot *widget;
    int previous_editing;
} extractpdf_pdf_edit_form_widget_handle;

typedef struct extractpdf_pdf_edit_form_widget_handles {
    pdf_page **pages;
    int *page_indices;
    size_t page_count;
    extractpdf_pdf_edit_form_widget_handle *items;
    size_t count;
} extractpdf_pdf_edit_form_widget_handles;

struct extractpdf_pdf_form_live_field;
struct extractpdf_pdf_form_model;

extractpdf_status extractpdf_pdf_edit_form_prepare_widget_handles(
    extractpdf_pdf_edit *edit,
    const struct extractpdf_pdf_form_live_field *live,
    extractpdf_pdf_edit_form_widget_handles *out_handles);
void extractpdf_pdf_edit_form_drop_widget_handles(
    extractpdf_pdf_edit *edit,
    extractpdf_pdf_edit_form_widget_handles *handles);
extractpdf_status extractpdf_pdf_edit_form_mutation_preflight(
    extractpdf_pdf_edit *edit);
extractpdf_status extractpdf_pdf_edit_form_apply_zero_widget_text(
    extractpdf_pdf_edit *edit,
    const struct extractpdf_pdf_form_model *model,
    size_t field_index,
    const struct extractpdf_pdf_form_live_field *live,
    const extractpdf_form_value_update *update);
extractpdf_status extractpdf_pdf_edit_form_apply_button(
    extractpdf_pdf_edit *edit,
    const struct extractpdf_pdf_form_model *model,
    size_t field_index,
    const struct extractpdf_pdf_form_live_field *live,
    const extractpdf_form_value_update *update);

#if defined(EXTRACTPDF_TESTING)
enum {
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE = 0,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD = 1,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_AFTER_CREATE_MUTATION = 2,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_SNAPSHOT_BEFORE_PUBLISH = 3,
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE = 4
};
#endif

struct extractpdf_pdf_edit {
    fz_context *ctx;
    pdf_document *document;
    extractpdf_output *seed_output;
    uint64_t session_cookie;
    extractpdf_pdf_edit_annotation_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    extractpdf_pdf_edit_form_entry *form_entries;
    size_t form_entry_count;
    size_t form_entry_capacity;
#if defined(EXTRACTPDF_TESTING)
    int test_fault;
#endif
};

#endif
