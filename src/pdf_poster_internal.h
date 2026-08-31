#ifndef QUANTAPDF_PDF_POSTER_INTERNAL_H
#define QUANTAPDF_PDF_POSTER_INTERNAL_H

#include "pdf_page_box_common.h"

typedef struct quantapdf_pdf_poster_tile_plan {
    size_t row;
    size_t column;
    size_t tile_index;
    quantapdf_rect public_rect;
    fz_rect pdf_rect;
} quantapdf_pdf_poster_tile_plan;

typedef enum quantapdf_pdf_poster_annot_kind {
    QUANTAPDF_PDF_POSTER_ANNOT_LINK = 1,
    QUANTAPDF_PDF_POSTER_ANNOT_WIDGET = 2,
    QUANTAPDF_PDF_POSTER_ANNOT_ORDINARY = 3
} quantapdf_pdf_poster_annot_kind;

typedef struct quantapdf_pdf_poster_annot_plan {
    size_t source_annot_index;
    quantapdf_pdf_poster_annot_kind kind;
    quantapdf_rect source_public_rect;
    size_t *tile_indices;
    size_t tile_count;
    size_t form_field_index;
    size_t form_widget_index;
} quantapdf_pdf_poster_annot_plan;

typedef enum quantapdf_pdf_poster_dest_owner_kind {
    QUANTAPDF_PDF_POSTER_DEST_LINK_DIRECT = 1,
    QUANTAPDF_PDF_POSTER_DEST_LINK_ACTION = 2,
    QUANTAPDF_PDF_POSTER_DEST_OUTLINE_DIRECT = 3,
    QUANTAPDF_PDF_POSTER_DEST_OUTLINE_ACTION = 4,
    QUANTAPDF_PDF_POSTER_DEST_NAME_TREE = 5,
    QUANTAPDF_PDF_POSTER_DEST_LEGACY_DICT = 6
} quantapdf_pdf_poster_dest_owner_kind;

typedef struct quantapdf_pdf_poster_dest_plan {
    quantapdf_pdf_poster_dest_owner_kind owner_kind;
    int owner_page_index;
    size_t owner_ordinal;
    int source_target_page_index;
    quantapdf_point target_public;
    size_t split_plan_index;
    size_t tile_index;
} quantapdf_pdf_poster_dest_plan;

typedef struct quantapdf_pdf_poster_split_plan {
    int page_index;
    size_t columns;
    size_t rows;
    size_t tile_count;
    quantapdf_pdf_page_box_view page;
    float *x_edges;
    float *y_edges;
    quantapdf_pdf_poster_tile_plan *tiles;
    quantapdf_pdf_poster_annot_plan *annots;
    size_t annot_count;
    int changed;
} quantapdf_pdf_poster_split_plan;

typedef struct quantapdf_pdf_poster_plan {
    quantapdf_pdf_poster_split_plan *splits;
    size_t split_count;
    int source_page_count;
    int output_page_count;
    int any_changed;
    int expansion_policy_applied;
    quantapdf_pdf_poster_dest_plan *destinations;
    size_t destination_count;
    size_t destination_capacity;
} quantapdf_pdf_poster_plan;

typedef struct quantapdf_pdf_poster_private_split {
    pdf_obj *source_page;
    pdf_obj **tile_pages;
    size_t tile_count;
} quantapdf_pdf_poster_private_split;

quantapdf_status quantapdf_pdf_poster_check_security(
    fz_context *ctx,
    pdf_document *document);

quantapdf_status quantapdf_pdf_poster_build_plan(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    int expansion_policy,
    quantapdf_pdf_poster_plan **out_plan);

quantapdf_status quantapdf_pdf_poster_collect_rect_tiles(
    const quantapdf_pdf_poster_split_plan *split,
    quantapdf_rect rect,
    size_t **out_tile_indices,
    size_t *out_tile_count,
    int *out_crosses);

quantapdf_status quantapdf_pdf_poster_annotations_preflight(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_poster_plan *plan);

quantapdf_status quantapdf_pdf_poster_widget_provenance_preflight(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_poster_plan *plan);

void quantapdf_pdf_poster_drop_annotation_plans(
    quantapdf_pdf_poster_plan *plan);

int quantapdf_pdf_poster_annotation_plans_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right);

quantapdf_status quantapdf_pdf_poster_apply_annotations(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_plan *plan,
    quantapdf_pdf_poster_private_split *runtime);

quantapdf_status quantapdf_pdf_poster_navigation_preflight(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_poster_plan *plan);

int quantapdf_pdf_poster_navigation_plans_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right);

quantapdf_status quantapdf_pdf_poster_apply_navigation(
    fz_context *ctx,
    pdf_document *document,
    const quantapdf_pdf_poster_plan *plan,
    quantapdf_pdf_poster_private_split *runtime);

int quantapdf_pdf_poster_plan_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right);

void quantapdf_pdf_poster_drop_plan(quantapdf_pdf_poster_plan *plan);

#endif
