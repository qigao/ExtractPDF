#include "../src/pdf_edit_internal.h"
#include "pdf_edit_test_api.h"

void extractpdf_test_pdf_edit_set_fault(
    extractpdf_pdf_edit *edit,
    extractpdf_test_pdf_edit_fault fault)
{
#if defined(EXTRACTPDF_TESTING)
    if (edit != NULL)
        edit->test_fault = (int)fault;
#else
    (void)edit;
    (void)fault;
#endif
}
