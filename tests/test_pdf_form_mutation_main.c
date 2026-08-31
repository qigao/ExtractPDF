#include <stdlib.h>

int quantapdf_pdf_form_mutation_base_main(void);
int quantapdf_pdf_form_appearance_main(void);
int quantapdf_pdf_form_rollback_main(void);

int main(void)
{
    int result = quantapdf_pdf_form_mutation_base_main();
    if (result != EXIT_SUCCESS)
        return result;
    result = quantapdf_pdf_form_appearance_main();
    if (result != EXIT_SUCCESS)
        return result;
    return quantapdf_pdf_form_rollback_main();
}
