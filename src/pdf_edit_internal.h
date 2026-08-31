#ifndef QUANTAPDF_PDF_EDIT_INTERNAL_H
#define QUANTAPDF_PDF_EDIT_INTERNAL_H

#include "pdf_annotation_common.h"

typedef struct quantapdf_pdf_edit_annotation_entry {
    pdf_obj *object;
    int page_index;
    uint32_t tag;
    int live;
} quantapdf_pdf_edit_annotation_entry;

typedef struct quantapdf_pdf_edit_form_entry {
    size_t *locator_steps;
    size_t locator_step_count;
    uint32_t tag;
} quantapdf_pdf_edit_form_entry;

typedef struct quantapdf_pdf_edit_form_widget_handle {
    pdf_annot *widget;
    int previous_editing;
    int editing_active;
} quantapdf_pdf_edit_form_widget_handle;

typedef struct quantapdf_pdf_edit_form_widget_handles {
    pdf_page **pages;
    int *page_indices;
    size_t page_count;
    quantapdf_pdf_edit_form_widget_handle *items;
    size_t count;
} quantapdf_pdf_edit_form_widget_handles;

struct quantapdf_pdf_form_live_field;
struct quantapdf_pdf_form_model;

quantapdf_status quantapdf_pdf_edit_form_prepare_widget_handles(
    quantapdf_pdf_edit *edit,
    const struct quantapdf_pdf_form_live_field *live,
    quantapdf_pdf_edit_form_widget_handles *out_handles);
quantapdf_status quantapdf_pdf_edit_form_begin_widget_editing(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_form_widget_handles *handles);
quantapdf_status quantapdf_pdf_edit_form_restore_widget_editing(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_form_widget_handles *handles);
void quantapdf_pdf_edit_form_refresh_widget_handles(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_form_widget_handles *handles);
void quantapdf_pdf_edit_form_drop_widget_handles(
    quantapdf_pdf_edit *edit,
    quantapdf_pdf_edit_form_widget_handles *handles);
quantapdf_status quantapdf_pdf_edit_form_mutation_preflight(
    quantapdf_pdf_edit *edit);
quantapdf_status quantapdf_pdf_edit_form_apply_text(
    quantapdf_pdf_edit *edit,
    const struct quantapdf_pdf_form_model *model,
    size_t field_index,
    const struct quantapdf_pdf_form_live_field *live,
    const quantapdf_form_value_update *update);
quantapdf_status quantapdf_pdf_edit_form_apply_button(
    quantapdf_pdf_edit *edit,
    const struct quantapdf_pdf_form_model *model,
    size_t field_index,
    const struct quantapdf_pdf_form_live_field *live,
    const quantapdf_form_value_update *update);
quantapdf_status quantapdf_pdf_edit_form_apply_choice(
    quantapdf_pdf_edit *edit,
    const struct quantapdf_pdf_form_model *model,
    size_t field_index,
    const struct quantapdf_pdf_form_live_field *live,
    const quantapdf_form_value_update *update);

#if defined(QUANTAPDF_TESTING)
enum {
    QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE = 0,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_FIRST_UPDATE_FIELD = 1,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_AFTER_CREATE_MUTATION = 2,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_SNAPSHOT_BEFORE_PUBLISH = 3,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE = 4,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_SEMANTIC_WRITE = 5,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_FIRST_WIDGET_STATE = 6,
    QUANTAPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_FIRST_AP_REFRESH = 7
};
#endif

struct quantapdf_pdf_edit {
    fz_context *ctx;
    pdf_document *document;
    quantapdf_output *seed_output;
    uint64_t session_cookie;
    quantapdf_pdf_edit_annotation_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    quantapdf_pdf_edit_form_entry *form_entries;
    size_t form_entry_count;
    size_t form_entry_capacity;
#if defined(QUANTAPDF_TESTING)
    int test_fault;
#endif
};

#endif
