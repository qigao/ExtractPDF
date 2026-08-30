#ifndef EXTRACTPDF_PDF_POSTER_INTERNAL_H
#define EXTRACTPDF_PDF_POSTER_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct extractpdf_pdf_poster_tile_plan {
    size_t row;
    size_t column;
    size_t tile_index;
    extractpdf_rect public_rect;
    fz_rect pdf_rect;
} extractpdf_pdf_poster_tile_plan;

typedef enum extractpdf_pdf_poster_annot_kind {
    EXTRACTPDF_PDF_POSTER_ANNOT_LINK = 1,
    EXTRACTPDF_PDF_POSTER_ANNOT_WIDGET = 2,
    EXTRACTPDF_PDF_POSTER_ANNOT_ORDINARY = 3
} extractpdf_pdf_poster_annot_kind;

typedef struct extractpdf_pdf_poster_annot_plan {
    size_t source_annot_index;
    extractpdf_pdf_poster_annot_kind kind;
    extractpdf_rect source_public_rect;
    size_t *tile_indices;
    size_t tile_count;
    size_t form_field_index;
    size_t form_widget_index;
} extractpdf_pdf_poster_annot_plan;

typedef struct extractpdf_pdf_poster_split_plan {
    int page_index;
    size_t columns;
    size_t rows;
    size_t tile_count;
    extractpdf_pdf_page_box_view page;
    float *x_edges;
    float *y_edges;
    extractpdf_pdf_poster_tile_plan *tiles;
    extractpdf_pdf_poster_annot_plan *annots;
    size_t annot_count;
    int changed;
} extractpdf_pdf_poster_split_plan;

typedef struct extractpdf_pdf_poster_plan {
    extractpdf_pdf_poster_split_plan *splits;
    size_t split_count;
    int source_page_count;
    int output_page_count;
    int any_changed;
    int expansion_policy_applied;
} extractpdf_pdf_poster_plan;

typedef struct extractpdf_pdf_poster_private_split {
    pdf_obj *source_page;
    pdf_obj **tile_pages;
    size_t tile_count;
} extractpdf_pdf_poster_private_split;

extractpdf_status extractpdf_pdf_poster_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_poster_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_page_poster_split *splits,
    size_t split_count,
    int expansion_policy,
    extractpdf_pdf_poster_plan **out_plan);

extractpdf_status extractpdf_pdf_poster_collect_rect_tiles(
    const extractpdf_pdf_poster_split_plan *split,
    extractpdf_rect rect,
    size_t **out_tile_indices,
    size_t *out_tile_count,
    int *out_crosses);

extractpdf_status extractpdf_pdf_poster_annotations_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan);

void extractpdf_pdf_poster_drop_annotation_plans(
    extractpdf_pdf_poster_plan *plan);

extractpdf_status extractpdf_pdf_poster_navigation_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_poster_plan *plan);

int extractpdf_pdf_poster_plan_equivalent(
    const extractpdf_pdf_poster_plan *left,
    const extractpdf_pdf_poster_plan *right);

void extractpdf_pdf_poster_drop_plan(extractpdf_pdf_poster_plan *plan);

#endif
