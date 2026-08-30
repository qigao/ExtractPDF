#include "test_pdf_flatten_internal.h"

int extractpdf_pdf_flatten_base_main(void);

int main(void)
{
    if (extractpdf_test_pdf_flatten_appearance() != 0)
        return 1;
    return extractpdf_pdf_flatten_base_main();
}
