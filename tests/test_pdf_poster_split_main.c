#include "test_pdf_poster_split_internal.h"

int extractpdf_pdf_poster_split_base_main(void);

int main(void)
{
    if (poster_run_policy_tests() != 0)
        return 1;
    if (extractpdf_pdf_poster_split_base_main() != 0)
        return 1;
    if (poster_run_geometry_tests() != 0)
        return 1;
    if (poster_run_interactive_tests() != 0)
        return 1;
    if (poster_run_navigation_tests() != 0)
        return 1;
    return poster_run_batch_tests();
}
