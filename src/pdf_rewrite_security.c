#include "pdf_rewrite_security.h"

#include <string.h>

typedef struct quantapdf_pdf_signature_scan {
    pdf_document *document;
    int has_signed_field;
} quantapdf_pdf_signature_scan;

static int quantapdf_rewrite_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count;
    int index;

    if (out_value != NULL)
        *out_value = NULL;
    if (!pdf_is_dict(ctx, dictionary))
        return 0;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key)) {
            if (out_value != NULL)
                *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

static int quantapdf_rewrite_dict_finds(
    fz_context *ctx,
    pdf_obj *dictionary,
    const char *key,
    pdf_obj **out_value)
{
    int count;
    int index;

    if (out_value != NULL)
        *out_value = NULL;
    if (!pdf_is_dict(ctx, dictionary))
        return 0;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        pdf_obj *candidate = pdf_dict_get_key(ctx, dictionary, index);
        if (pdf_is_name(ctx, candidate) &&
            strcmp(pdf_to_name(ctx, candidate), key) == 0) {
            if (out_value != NULL)
                *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

static void quantapdf_pdf_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    quantapdf_pdf_signature_scan *scan =
        (quantapdf_pdf_signature_scan *)data;

    if (scan->has_signed_field || !pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static quantapdf_status quantapdf_pdf_rewrite_inspect_security(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    quantapdf_pdf_signature_scan scan;
    pdf_obj *trailer;
    pdf_obj *root;
    pdf_obj *permissions = NULL;
    pdf_obj *acroform = NULL;
    pdf_obj *fields = NULL;
    pdf_obj *field_type = NULL;

    trailer = pdf_trailer(ctx, document);
    if (!pdf_is_dict(ctx, trailer))
        return QUANTAPDF_ERROR_FORMAT;
    if (quantapdf_rewrite_dict_find(ctx, trailer, PDF_NAME(Encrypt), NULL))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
    if (!pdf_is_dict(ctx, root))
        return QUANTAPDF_ERROR_FORMAT;

    if (quantapdf_rewrite_dict_finds(ctx, root, "Perms", &permissions)) {
        if (!pdf_is_dict(ctx, permissions))
            return QUANTAPDF_ERROR_FORMAT;
        if (quantapdf_rewrite_dict_finds(ctx, permissions, "DocMDP", NULL) ||
            quantapdf_rewrite_dict_finds(ctx, permissions, "UR", NULL) ||
            quantapdf_rewrite_dict_finds(ctx, permissions, "UR3", NULL))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    if (!quantapdf_rewrite_dict_find(ctx, root, PDF_NAME(AcroForm), &acroform))
        return QUANTAPDF_OK;
    if (!pdf_is_dict(ctx, acroform))
        return QUANTAPDF_ERROR_FORMAT;
    if (!quantapdf_rewrite_dict_find(ctx, acroform, PDF_NAME(Fields), &fields))
        return QUANTAPDF_OK;
    if (!pdf_is_array(ctx, fields))
        return QUANTAPDF_ERROR_FORMAT;

    scan.document = document;
    scan.has_signed_field = 0;
    pdf_walk_tree(
        ctx,
        fields,
        PDF_NAME(Kids),
        quantapdf_pdf_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field ?
        QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_rewrite_check_security(
    fz_context *ctx,
    pdf_document *document)
{
    quantapdf_status status = QUANTAPDF_OK;
    int caught_code = FZ_ERROR_NONE;

    if (ctx == NULL || document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        status = quantapdf_pdf_rewrite_inspect_security(ctx, document);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_backend(caught_code);
    return status;
}
