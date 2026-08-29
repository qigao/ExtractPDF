#include "test_pdf_trim_internal.h"

int extractpdf_pdf_trim_base_main(void);

int main(void)
{
    if (!trim_run_frame_mode_tests())
        return 1;
    if (!trim_run_policy_tests())
        return 1;
    if (!trim_run_batch_tests())
        return 1;
    if (!trim_run_outside_crop_test())
        return 1;
    return extractpdf_pdf_trim_base_main();
}
