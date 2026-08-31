#ifndef QUANTAPDF_PDF_POSTER_TEST_API_H
#define QUANTAPDF_PDF_POSTER_TEST_API_H

#include <quantapdf/quantapdf.h>

typedef enum quantapdf_test_pdf_poster_fault {
    QUANTAPDF_TEST_PDF_POSTER_FAULT_NONE = 0,
    QUANTAPDF_TEST_PDF_POSTER_FAULT_ANNOTATION_PREFLIGHT = 1,
    QUANTAPDF_TEST_PDF_POSTER_FAULT_WIDGET_PREFLIGHT = 2,
    QUANTAPDF_TEST_PDF_POSTER_FAULT_NAVIGATION_PREFLIGHT = 3
} quantapdf_test_pdf_poster_fault;

void quantapdf_test_pdf_poster_set_fault(
    quantapdf_document *document,
    quantapdf_test_pdf_poster_fault fault);

#endif
