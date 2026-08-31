#include "test_pdf_poster_split_internal.h"

#include <stdio.h>

int quantapdf_pdf_poster_split_base_main(void);

int main(void)
{
    fprintf(stderr, "poster suite: batch-before\n");
    if (poster_run_batch_tests() != 0)
        return 1;
    fprintf(stderr, "poster suite: policy\n");
    if (poster_run_policy_tests() != 0)
        return 1;
    fprintf(stderr, "poster suite: base\n");
    if (quantapdf_pdf_poster_split_base_main() != 0)
        return 1;
    fprintf(stderr, "poster suite: geometry\n");
    if (poster_run_geometry_tests() != 0)
        return 1;
    fprintf(stderr, "poster suite: interactive\n");
    if (poster_run_interactive_tests() != 0)
        return 1;
    fprintf(stderr, "poster suite: navigation\n");
    if (poster_run_navigation_tests() != 0)
        return 1;
    fprintf(stderr, "poster suite: batch-after\n");
    return poster_run_batch_tests();
}
