#include "pdf_form_common.h"

#include <stdlib.h>

static extractpdf_pdf_form_model *extractpdf_pdf_form_new_empty_model(void)
{
    return (extractpdf_pdf_form_model *)calloc(1, sizeof(extractpdf_pdf_form_model));
}

extractpdf_status extractpdf_pdf_form_parse(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model **out_model)
{
    extractpdf_pdf_form_model *model;
    pdf_obj *trailer;
    pdf_obj *root = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;

    if (out_model == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_model = NULL;

    if (ctx == NULL || document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    trailer = pdf_trailer(ctx, document);
    if (!extractpdf_pdf_dict_find(ctx, trailer, PDF_NAME(Root), &root) ||
        !pdf_is_dict(ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;

    model = extractpdf_pdf_form_new_empty_model();
    if (model == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    if (!extractpdf_pdf_dict_find(ctx, root, PDF_NAME(AcroForm), &acroform)) {
        *out_model = model;
        return EXTRACTPDF_OK;
    }
    if (!pdf_is_dict(ctx, acroform)) {
        free(model);
        return EXTRACTPDF_ERROR_FORMAT;
    }

    if (!extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(Fields), &fields)) {
        *out_model = model;
        return EXTRACTPDF_OK;
    }
    if (!pdf_is_array(ctx, fields)) {
        free(model);
        return EXTRACTPDF_ERROR_FORMAT;
    }
    if (pdf_array_len(ctx, fields) == 0) {
        *out_model = model;
        return EXTRACTPDF_OK;
    }

    free(model);
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}

void extractpdf_pdf_form_drop_model(
    extractpdf_pdf_form_model *model)
{
    free(model);
}
