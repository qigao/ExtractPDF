#include "test_pdf_flatten_internal.h"

int extractpdf_pdf_flatten_base_main(void);
int extractpdf_test_pdf_flatten_form_multi(void);
int extractpdf_test_pdf_flatten_form_closure(void);
int extractpdf_test_pdf_flatten_form_ancestor_survives(void);
int extractpdf_test_pdf_flatten_form_general(void);
int extractpdf_test_pdf_flatten_widget_as(void);
int extractpdf_test_pdf_flatten_policy(void);
int extractpdf_test_pdf_flatten_determinism(void);
int extractpdf_test_pdf_flatten_flag_sets(void);
int extractpdf_test_pdf_flatten_render(void);

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
    if (extractpdf_test_pdf_flatten_form_ancestor_survives() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_general() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_widget_as() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_policy() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_determinism() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_flag_sets() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_render() != 0)
        return 1;
    return extractpdf_pdf_flatten_base_main();
}
