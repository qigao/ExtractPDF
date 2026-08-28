#ifndef EXTRACTPDF_PDF_EDIT_TEST_API_H
#define EXTRACTPDF_PDF_EDIT_TEST_API_H

#include <extractpdf/extractpdf.h>

typedef enum extractpdf_test_pdf_edit_fault {
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_NONE = 0,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD = 1,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION = 2,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH = 3
} extractpdf_test_pdf_edit_fault;

EXTRACTPDF_API void extractpdf_test_pdf_edit_set_fault(
    extractpdf_pdf_edit *edit,
    extractpdf_test_pdf_edit_fault fault);

#endif
