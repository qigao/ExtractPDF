#include "test_pdf_flatten_internal.h"

int extractpdf_pdf_flatten_base_main(void);
int extractpdf_test_pdf_flatten_form_multi(void);
int extractpdf_test_pdf_flatten_form_closure(void);

int main(void)
{
    if (extractpdf_test_pdf_flatten_appearance() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_raw() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_multi() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_closure() != 0)
        return 1;
    return extractpdf_pdf_flatten_base_main();
}
