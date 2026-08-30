#ifndef EXTRACTPDF_PDF_FLATTEN_INTERNAL_H
#define EXTRACTPDF_PDF_FLATTEN_INTERNAL_H

#include "pdf_appearance_common.h"

typedef enum extractpdf_pdf_flatten_target_kind {
    EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION = 1,
    EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET = 2
} extractpdf_pdf_flatten_target_kind;

typedef struct extractpdf_pdf_flatten_target_plan {
    int page_index;
    size_t annot_ordinal;
    extractpdf_pdf_flatten_target_kind kind;
    extractpdf_annotation_type annotation_type;
    uint32_t flags;
    fz_rect rect;
    int appearance_stateful;
    char *appearance_state;
    size_t appearance_state_size;
    fz_rect bbox;
    fz_matrix appearance_matrix;
    fz_matrix placement;
    size_t appearance_slot;
} extractpdf_pdf_flatten_target_plan;

typedef struct extractpdf_pdf_flatten_page_plan {
    int page_index;
    size_t first_target;
    size_t target_count;
    size_t appearance_slot_count;
} extractpdf_pdf_flatten_page_plan;

typedef struct extractpdf_pdf_flatten_form_plan
    extractpdf_pdf_flatten_form_plan;

typedef struct extractpdf_pdf_flatten_plan {
    uint32_t flags;
    int source_page_count;
    extractpdf_pdf_flatten_target_plan *targets;
    size_t target_count;
    extractpdf_pdf_flatten_page_plan *pages;
    size_t page_count;
    int any_changed;
    int policy_complete;
    extractpdf_pdf_flatten_form_plan *form;
} extractpdf_pdf_flatten_plan;

extractpdf_status extractpdf_pdf_flatten_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_flatten_build_plan(
    fz_context *ctx,
    pdf_document *document,
    uint32_t flags,
    extractpdf_pdf_flatten_plan **out_plan);

int extractpdf_pdf_flatten_plan_equivalent(
    const extractpdf_pdf_flatten_plan *left,
    const extractpdf_pdf_flatten_plan *right);

void extractpdf_pdf_flatten_drop_plan(
    extractpdf_pdf_flatten_plan *plan);

#endif
