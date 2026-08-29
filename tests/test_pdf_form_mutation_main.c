#include <stdlib.h>

int extractpdf_pdf_form_mutation_base_main(void);
int extractpdf_pdf_form_appearance_main(void);
int extractpdf_pdf_form_rollback_main(void);

int main(void)
{
    int result = extractpdf_pdf_form_mutation_base_main();
    if (result != EXIT_SUCCESS)
        return result;
    result = extractpdf_pdf_form_appearance_main();
    if (result != EXIT_SUCCESS)
        return result;
    return extractpdf_pdf_form_rollback_main();
}
