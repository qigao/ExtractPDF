#ifndef EXTRACTPDF_PDF_INTERNAL_H
#define EXTRACTPDF_PDF_INTERNAL_H

#include "internal.h"

#include <mupdf/pdf.h>

extractpdf_status extractpdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_output **out_output);

#endif
