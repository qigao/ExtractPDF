#include "test_pdf_poster_split_internal.h"

int extractpdf_pdf_poster_split_base_main(void);

int main(void)
{
    if (extractpdf_pdf_poster_split_base_main() != 0)
        return 1;
    return poster_run_geometry_tests();
}
