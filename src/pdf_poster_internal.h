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

typedef struct extractpdf_pdf_poster_split_plan {
    int page_index;
    size_t columns;
    size_t rows;
    size_t tile_count;
    extractpdf_pdf_page_box_view page;
    float *x_edges;
    float *y_edges;
    extractpdf_pdf_poster_tile_plan *tiles;
    int changed;
} extractpdf_pdf_poster_split_plan;

typedef struct extractpdf_pdf_poster_plan {
    extractpdf_pdf_poster_split_plan *splits;
    size_t split_count;
    int source_page_count;
    int output_page_count;
    int any_changed;
} extractpdf_pdf_poster_plan;

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

int extractpdf_pdf_poster_plan_equivalent(
    const extractpdf_pdf_poster_plan *left,
    const extractpdf_pdf_poster_plan *right);

void extractpdf_pdf_poster_drop_plan(extractpdf_pdf_poster_plan *plan);

#endif
