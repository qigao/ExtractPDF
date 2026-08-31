#include "pdf_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pdf_obj *quantapdf_metadata_key(quantapdf_metadata_field field)
{
    switch (field) {
    case QUANTAPDF_METADATA_TITLE: return PDF_NAME(Title);
    case QUANTAPDF_METADATA_AUTHOR: return PDF_NAME(Author);
    case QUANTAPDF_METADATA_SUBJECT: return PDF_NAME(Subject);
    case QUANTAPDF_METADATA_KEYWORDS: return PDF_NAME(Keywords);
    case QUANTAPDF_METADATA_CREATOR: return PDF_NAME(Creator);
    case QUANTAPDF_METADATA_PRODUCER: return PDF_NAME(Producer);
    case QUANTAPDF_METADATA_CREATION_DATE: return PDF_NAME(CreationDate);
    case QUANTAPDF_METADATA_MODIFICATION_DATE: return PDF_NAME(ModDate);
    default: return NULL;
    }
}

static int quantapdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value)
{
    int count = pdf_dict_len(ctx, dictionary);
    int index;

    *out_value = NULL;
    for (index = 0; index < count; ++index) {
        pdf_obj *candidate = pdf_dict_get_key(ctx, dictionary, index);
        if (pdf_name_eq(ctx, candidate, key)) {
            *out_value = pdf_dict_get_val(ctx, dictionary, index);
            return 1;
        }
    }
    return 0;
}

quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    fz_context *ctx;
    pdf_document *pdf = NULL;
    pdf_obj *trailer = NULL;
    pdf_obj *info = NULL;
    pdf_obj *value = NULL;
    pdf_obj *key;
    const char *text = NULL;
    char *copy;
    size_t text_size = 0;
    int info_present = 0;
    int value_present = 0;
    int malformed_info = 0;
    int malformed_value = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    key = quantapdf_metadata_key(field);
    if (document == NULL || out_utf8 == NULL || out_size == NULL || key == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    ctx = document->ctx;

    fz_var(pdf);
    fz_var(trailer);
    fz_var(info);
    fz_var(value);
    fz_var(text);
    fz_var(text_size);
    fz_var(info_present);
    fz_var(value_present);
    fz_var(malformed_info);
    fz_var(malformed_value);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, document->doc);
        if (pdf != NULL) {
            trailer = pdf_trailer(ctx, pdf);
            if (trailer != NULL)
                info_present = quantapdf_pdf_dict_find(
                    ctx, trailer, PDF_NAME(Info), &info);

            if (info_present) {
                if (!pdf_is_dict(ctx, info)) {
                    malformed_info = 1;
                } else {
                    value_present = quantapdf_pdf_dict_find(
                        ctx, info, key, &value);
                    if (value_present) {
                        if (!pdf_is_string(ctx, value)) {
                            malformed_value = 1;
                        } else {
                            text = pdf_to_text_string(ctx, value);
                            text_size = strlen(text);
                        }
                    }
                }
            }
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    if (pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (malformed_info || malformed_value)
        return QUANTAPDF_ERROR_FORMAT;
    if (!info_present || !value_present)
        return QUANTAPDF_OK;
    if (text_size == SIZE_MAX)
        return QUANTAPDF_ERROR_NOMEM;

    copy = (char *)malloc(text_size + 1);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (text_size != 0)
        memcpy(copy, text, text_size);
    copy[text_size] = '\0';

    *out_utf8 = copy;
    *out_size = text_size;
    return QUANTAPDF_OK;
}
