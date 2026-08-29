#include "pdf_edit_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void extractpdf_pdf_edit_discard_log(
    void *user,
    const char *message)
{
    (void)user;
    (void)message;
}

static uint64_t extractpdf_pdf_edit_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static int extractpdf_pdf_edit_dict_has_key(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key)
{
    int count;
    int index;

    count = pdf_dict_len(ctx, dictionary);
    for (index = 0; index < count; ++index) {
        if (pdf_name_eq(ctx, pdf_dict_get_key(ctx, dictionary, index), key))
            return 1;
    }
    return 0;
}

typedef struct extractpdf_pdf_edit_signature_scan {
    pdf_document *document;
    int has_signed_field;
} extractpdf_pdf_edit_signature_scan;

static void extractpdf_pdf_edit_scan_signature_field(
    fz_context *ctx,
    pdf_obj *field,
    void *data,
    pdf_obj **ft)
{
    extractpdf_pdf_edit_signature_scan *scan =
        (extractpdf_pdf_edit_signature_scan *)data;

    if (scan->has_signed_field)
        return;
    if (!pdf_name_eq(ctx, *ft, PDF_NAME(Sig)))
        return;
    if (pdf_signature_is_signed(ctx, scan->document, field))
        scan->has_signed_field = 1;
}

static int extractpdf_pdf_edit_has_signed_field(
    fz_context *ctx,
    pdf_document *document)
{
    static pdf_obj *field_type_names[2] = {PDF_NAME(FT), NULL};
    extractpdf_pdf_edit_signature_scan scan;
    pdf_obj *field_type = NULL;
    pdf_obj *fields;

    scan.document = document;
    scan.has_signed_field = 0;
    fields = pdf_dict_getp(
        ctx, pdf_trailer(ctx, document), "Root/AcroForm/Fields");
    pdf_walk_tree(
        ctx,
        fields,
        PDF_NAME(Kids),
        extractpdf_pdf_edit_scan_signature_field,
        NULL,
        &scan,
        field_type_names,
        &field_type);
    return scan.has_signed_field;
}

static void extractpdf_dispose_pdf_edit(extractpdf_pdf_edit *edit)
{
    size_t index;

    if (edit == NULL)
        return;

    if (edit->ctx != NULL) {
        for (index = 0; index < edit->entry_count; ++index) {
            if (edit->entries[index].object != NULL)
                pdf_drop_obj(edit->ctx, edit->entries[index].object);
        }
        if (edit->document != NULL)
            pdf_drop_document(edit->ctx, edit->document);
    }

    for (index = 0; index < edit->form_entry_count; ++index) {
        free(edit->form_entries[index].locator_steps);
        edit->form_entries[index].locator_steps = NULL;
        edit->form_entries[index].locator_step_count = 0;
    }
    free(edit->entries);
    free(edit->form_entries);
    if (edit->ctx != NULL)
        fz_drop_context(edit->ctx);
    extractpdf_drop_output(edit->seed_output);
    free(edit);
}

static uint64_t extractpdf_pdf_edit_session_cookie(
    extractpdf_pdf_edit *edit)
{
    uint64_t random_bits = 0;
    uint64_t seed;

    fz_memrnd(
        edit->ctx,
        (unsigned char *)&random_bits,
        (int)sizeof(random_bits));

    seed = (uint64_t)(uintptr_t)edit;
    seed ^= extractpdf_pdf_edit_mix64((uint64_t)(uintptr_t)edit->ctx);
    seed ^= extractpdf_pdf_edit_mix64((uint64_t)(uintptr_t)edit->document);
    seed ^= extractpdf_pdf_edit_mix64((uint64_t)time(NULL));
    seed ^= extractpdf_pdf_edit_mix64((uint64_t)clock());
    seed ^= random_bits;
    seed = extractpdf_pdf_edit_mix64(seed);
    if (seed == 0)
        seed = UINT64_C(0x9e3779b97f4a7c15);
    return seed;
}

extractpdf_status extractpdf_pdf_edit_begin(
    extractpdf_document *source,
    extractpdf_pdf_edit **out_edit)
{
    pdf_document *source_pdf;
    extractpdf_output *seed_output = NULL;
    extractpdf_pdf_edit *edit = NULL;
    fz_stream *stream = NULL;
    int encrypted = 0;
    int signed_field = 0;
    int caught_code = FZ_ERROR_NONE;
    extractpdf_status status;

    if (out_edit == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_edit = NULL;

    if (source == NULL || source->ctx == NULL || source->doc == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(source->ctx, source->doc);
    if (source_pdf == NULL)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    fz_var(encrypted);
    fz_var(signed_field);
    fz_var(caught_code);

    fz_try(source->ctx)
    {
        pdf_obj *trailer = pdf_trailer(source->ctx, source_pdf);
        encrypted = extractpdf_pdf_edit_dict_has_key(
            source->ctx, trailer, PDF_NAME(Encrypt));
        if (!encrypted)
            signed_field = extractpdf_pdf_edit_has_signed_field(
                source->ctx, source_pdf);
    }
    fz_catch(source->ctx)
    {
        caught_code = fz_caught(source->ctx);
        fz_report_error(source->ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    if (encrypted || signed_field)
        return EXTRACTPDF_ERROR_UNSUPPORTED;

    status = extractpdf_serialize_pdf(
        source->ctx, source_pdf, &seed_output);
    if (status != EXTRACTPDF_OK)
        return status;

    edit = (extractpdf_pdf_edit *)calloc(1, sizeof(*edit));
    if (edit == NULL) {
        extractpdf_drop_output(seed_output);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    edit->seed_output = seed_output;

    edit->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (edit->ctx == NULL) {
        extractpdf_dispose_pdf_edit(edit);
        return EXTRACTPDF_ERROR_NOMEM;
    }
    fz_set_error_callback(
        edit->ctx, extractpdf_pdf_edit_discard_log, NULL);
    fz_set_warning_callback(
        edit->ctx, extractpdf_pdf_edit_discard_log, NULL);

    caught_code = FZ_ERROR_NONE;
    fz_var(stream);
    fz_var(caught_code);

    fz_try(edit->ctx)
    {
        stream = fz_open_memory(
            edit->ctx,
            edit->seed_output->data,
            edit->seed_output->size);
        edit->document = pdf_open_document_with_stream(edit->ctx, stream);
        pdf_disable_js(edit->ctx, edit->document);
        pdf_enable_journal(edit->ctx, edit->document);
        edit->session_cookie = extractpdf_pdf_edit_session_cookie(edit);
    }
    fz_always(edit->ctx)
    {
        fz_drop_stream(edit->ctx, stream);
        stream = NULL;
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        status = extractpdf_status_from_mupdf(caught_code);
        extractpdf_dispose_pdf_edit(edit);
        return status;
    }

    if (edit->document == NULL || edit->session_cookie == 0) {
        extractpdf_dispose_pdf_edit(edit);
        return EXTRACTPDF_ERROR_MUPDF;
    }

    *out_edit = edit;
    return EXTRACTPDF_OK;
}

static void extractpdf_pdf_edit_drop_snapshot_state(
    fz_context *ctx,
    fz_buffer *buffer,
    fz_output *memory_output)
{
    if (memory_output != NULL)
        fz_drop_output(ctx, memory_output);
    if (buffer != NULL)
        fz_drop_buffer(ctx, buffer);
}

static extractpdf_status extractpdf_pdf_edit_snapshot_pdf(
    extractpdf_pdf_edit *edit,
    extractpdf_output **out_output)
{
    fz_buffer *buffer = NULL;
    fz_output *memory_output = NULL;
    unsigned char *data = NULL;
    size_t size = 0;
    extractpdf_output *result = NULL;
    int caught_code = FZ_ERROR_NONE;

    *out_output = NULL;

    fz_var(buffer);
    fz_var(memory_output);
    fz_var(data);
    fz_var(size);
    fz_var(caught_code);

    fz_try(edit->ctx)
    {
        buffer = fz_new_buffer(edit->ctx, 0);
        memory_output = fz_new_output_with_buffer(edit->ctx, buffer);
        pdf_write_snapshot(edit->ctx, edit->document, memory_output);
        fz_close_output(edit->ctx, memory_output);
        size = fz_buffer_storage(edit->ctx, buffer, &data);
    }
    fz_catch(edit->ctx)
    {
        caught_code = fz_caught(edit->ctx);
        fz_report_error(edit->ctx);
    }

    if (caught_code != FZ_ERROR_NONE) {
        extractpdf_status status =
            extractpdf_status_from_mupdf(caught_code);
        extractpdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return status;
    }

    if (data == NULL || size == 0) {
        extractpdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return EXTRACTPDF_ERROR_MUPDF;
    }

    result = (extractpdf_output *)calloc(1, sizeof(*result));
    if (result == NULL) {
        extractpdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    result->data = (unsigned char *)malloc(size);
    if (result->data == NULL) {
        free(result);
        extractpdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return EXTRACTPDF_ERROR_NOMEM;
    }

    memcpy(result->data, data, size);
    result->size = size;
    extractpdf_pdf_edit_drop_snapshot_state(
        edit->ctx, buffer, memory_output);

    *out_output = result;
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_edit_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_output **out_output)
{
    extractpdf_output *result = NULL;
    extractpdf_status status;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_pdf_edit_snapshot_pdf(edit, &result);
    if (status != EXTRACTPDF_OK)
        return status;

#if defined(EXTRACTPDF_TESTING)
    if (edit->test_fault ==
        EXTRACTPDF_PDF_EDIT_TEST_FAULT_SNAPSHOT_BEFORE_PUBLISH) {
        edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
        extractpdf_drop_output(result);
        return EXTRACTPDF_ERROR_MUPDF;
    }
#endif

    *out_output = result;
    return EXTRACTPDF_OK;
}

void extractpdf_drop_pdf_edit(extractpdf_pdf_edit *edit)
{
    extractpdf_dispose_pdf_edit(edit);
}
