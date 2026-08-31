#include "pdf_edit_internal.h"
#include "pdf_object_common.h"

quantapdf_status quantapdf_pdf_edit_form_mutation_preflight(
    quantapdf_pdf_edit *edit)
{
    pdf_obj *root;
    pdf_obj *acroform;
    pdf_obj *value = NULL;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    root = pdf_dict_get(edit->ctx, pdf_trailer(edit->ctx, edit->document),
        PDF_NAME(Root));
    if (!pdf_is_dict(edit->ctx, root))
        return QUANTAPDF_ERROR_FORMAT;
    acroform = pdf_dict_get(edit->ctx, root, PDF_NAME(AcroForm));
    if (!pdf_is_dict(edit->ctx, acroform))
        return QUANTAPDF_ERROR_FORMAT;

    if (quantapdf_pdf_dict_find(edit->ctx, acroform, PDF_NAME(XFA), &value))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if (quantapdf_pdf_dict_find(
            edit->ctx, acroform, PDF_NAME(NeedAppearances), &value)) {
        if (!pdf_is_bool(edit->ctx, value))
            return QUANTAPDF_ERROR_FORMAT;
        if (pdf_to_bool(edit->ctx, value))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    return QUANTAPDF_OK;
}
