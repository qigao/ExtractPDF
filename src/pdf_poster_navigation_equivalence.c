#include "pdf_poster_internal.h"

#include <math.h>

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

int quantapdf_pdf_poster_navigation_plans_equivalent(
    const quantapdf_pdf_poster_plan *left,
    const quantapdf_pdf_poster_plan *right)
{
    size_t index;

    if (left == NULL || right == NULL ||
        left->destination_count != right->destination_count)
        return 0;

    for (index = 0; index < left->destination_count; ++index) {
        const quantapdf_pdf_poster_dest_plan *a = &left->destinations[index];
        const quantapdf_pdf_poster_dest_plan *b = &right->destinations[index];

        if (a->owner_kind != b->owner_kind ||
            a->owner_page_index != b->owner_page_index ||
            a->owner_ordinal != b->owner_ordinal ||
            a->source_target_page_index != b->source_target_page_index ||
            a->split_plan_index != b->split_plan_index ||
            a->tile_index != b->tile_index ||
            !close_float(a->target_public.x, b->target_public.x) ||
            !close_float(a->target_public.y, b->target_public.y))
            return 0;
    }

    return 1;
}
