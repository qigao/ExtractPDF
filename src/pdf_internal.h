#ifndef QUANTAPDF_PDF_INTERNAL_H
#define QUANTAPDF_PDF_INTERNAL_H

#include "internal.h"

#include <mupdf/pdf.h>

quantapdf_status quantapdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_output **out_output);

#endif
