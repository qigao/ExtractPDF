#ifndef QUANTAPDF_PDF_REWRITE_SECURITY_H
#define QUANTAPDF_PDF_REWRITE_SECURITY_H

#include "pdf_internal.h"

quantapdf_status quantapdf_pdf_rewrite_check_security(
    fz_context *ctx,
    pdf_document *document);

#endif
