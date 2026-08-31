#ifndef QUANTAPDF_PDF_EDIT_INTERNAL_H
#define QUANTAPDF_PDF_EDIT_INTERNAL_H

#include "internal.h"
#include "backend/qpdf_edit.h"

struct quantapdf_pdf_edit {
    quantapdf_qpdf_edit *backend;
#if defined(QUANTAPDF_TESTING)
    int test_fault;
#endif
};

#endif
