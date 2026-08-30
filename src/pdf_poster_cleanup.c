#include "pdf_poster_internal.h"

#include <stdlib.h>

void extractpdf_pdf_poster_drop_annotation_plans(
    extractpdf_pdf_poster_plan *plan)
{
    size_t split_index;

    if (plan == NULL)
        return;
    for (split_index = 0; split_index < plan->split_count; ++split_index) {
        extractpdf_pdf_poster_split_plan *split = &plan->splits[split_index];
        size_t annot_index;
        for (annot_index = 0; annot_index < split->annot_count; ++annot_index)
            free(split->annots[annot_index].tile_indices);
        free(split->annots);
        split->annots = NULL;
        split->annot_count = 0;
    }

    free(plan->destinations);
    plan->destinations = NULL;
    plan->destination_count = 0;
    plan->destination_capacity = 0;
}
