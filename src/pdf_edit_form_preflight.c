#include "pdf_edit_internal.h"
#include "pdf_object_common.h"

extractpdf_status extractpdf_pdf_edit_form_mutation_preflight(
    extractpdf_pdf_edit *edit)
{
    pdf_obj *root;
    pdf_obj *acroform;
    pdf_obj *value = NULL;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    root = pdf_dict_get(edit->ctx, pdf_trailer(edit->ctx, edit->document),
        PDF_NAME(Root));
    if (!pdf_is_dict(edit->ctx, root))
        return EXTRACTPDF_ERROR_FORMAT;
    acroform = pdf_dict_get(edit->ctx, root, PDF_NAME(AcroForm));
    if (!pdf_is_dict(edit->ctx, acroform))
        return EXTRACTPDF_ERROR_FORMAT;

    if (extractpdf_pdf_dict_find(edit->ctx, acroform, PDF_NAME(XFA), &value))
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    if (extractpdf_pdf_dict_find(
            edit->ctx, acroform, PDF_NAME(NeedAppearances), &value)) {
        if (!pdf_is_bool(edit->ctx, value))
            return EXTRACTPDF_ERROR_FORMAT;
        if (pdf_to_bool(edit->ctx, value))
            return EXTRACTPDF_ERROR_UNSUPPORTED;
    }

    return EXTRACTPDF_OK;
}
