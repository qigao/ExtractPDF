#include "pdf_edit_internal.h"
#include "pdf_rewrite_security.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void quantapdf_pdf_edit_discard_log(
    void *user,
    const char *message)
{
    (void)user;
    (void)message;
}

static uint64_t quantapdf_pdf_edit_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static void quantapdf_dispose_pdf_edit(quantapdf_pdf_edit *edit)
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
    quantapdf_drop_output(edit->seed_output);
    free(edit);
}

static uint64_t quantapdf_pdf_edit_session_cookie(
    quantapdf_pdf_edit *edit)
{
    uint64_t random_bits = 0;
    uint64_t seed;

    fz_memrnd(
        edit->ctx,
        (unsigned char *)&random_bits,
        (int)sizeof(random_bits));

    seed = (uint64_t)(uintptr_t)edit;
    seed ^= quantapdf_pdf_edit_mix64((uint64_t)(uintptr_t)edit->ctx);
    seed ^= quantapdf_pdf_edit_mix64((uint64_t)(uintptr_t)edit->document);
    seed ^= quantapdf_pdf_edit_mix64((uint64_t)time(NULL));
    seed ^= quantapdf_pdf_edit_mix64((uint64_t)clock());
    seed ^= random_bits;
    seed = quantapdf_pdf_edit_mix64(seed);
    if (seed == 0)
        seed = UINT64_C(0x9e3779b97f4a7c15);
    return seed;
}

quantapdf_status quantapdf_pdf_edit_begin(
    quantapdf_document *source,
    quantapdf_pdf_edit **out_edit)
{
    pdf_document *source_pdf;
    quantapdf_output *seed_output = NULL;
    quantapdf_pdf_edit *edit = NULL;
    fz_stream *stream = NULL;
    int caught_code = FZ_ERROR_NONE;
    quantapdf_status status;

    if (out_edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_edit = NULL;

    if (source == NULL || source->ctx == NULL || source->doc == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    source_pdf = pdf_document_from_fz_document(source->ctx, source->doc);
    if (source_pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    status = quantapdf_pdf_rewrite_check_security(source->ctx, source_pdf);
    if (status != QUANTAPDF_OK)
        return status;

    status = quantapdf_serialize_pdf(
        source->ctx, source_pdf, &seed_output);
    if (status != QUANTAPDF_OK)
        return status;

    edit = (quantapdf_pdf_edit *)calloc(1, sizeof(*edit));
    if (edit == NULL) {
        quantapdf_drop_output(seed_output);
        return QUANTAPDF_ERROR_NOMEM;
    }
    edit->seed_output = seed_output;

    edit->ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (edit->ctx == NULL) {
        quantapdf_dispose_pdf_edit(edit);
        return QUANTAPDF_ERROR_NOMEM;
    }
    fz_set_error_callback(
        edit->ctx, quantapdf_pdf_edit_discard_log, NULL);
    fz_set_warning_callback(
        edit->ctx, quantapdf_pdf_edit_discard_log, NULL);

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
        edit->session_cookie = quantapdf_pdf_edit_session_cookie(edit);
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
        status = quantapdf_status_from_backend(caught_code);
        quantapdf_dispose_pdf_edit(edit);
        return status;
    }

    if (edit->document == NULL || edit->session_cookie == 0) {
        quantapdf_dispose_pdf_edit(edit);
        return QUANTAPDF_ERROR_BACKEND;
    }

    status = quantapdf_pdf_rewrite_check_security(edit->ctx, edit->document);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_pdf_edit(edit);
        return status;
    }

    *out_edit = edit;
    return QUANTAPDF_OK;
}

static void quantapdf_pdf_edit_drop_snapshot_state(
    fz_context *ctx,
    fz_buffer *buffer,
    fz_output *memory_output)
{
    if (memory_output != NULL)
        fz_drop_output(ctx, memory_output);
    if (buffer != NULL)
        fz_drop_buffer(ctx, buffer);
}

static quantapdf_status quantapdf_pdf_edit_snapshot_pdf(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output)
{
    fz_buffer *buffer = NULL;
    fz_output *memory_output = NULL;
    unsigned char *data = NULL;
    size_t size = 0;
    quantapdf_output *result = NULL;
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
        quantapdf_status status =
            quantapdf_status_from_backend(caught_code);
        quantapdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return status;
    }

    if (data == NULL || size == 0) {
        quantapdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return QUANTAPDF_ERROR_BACKEND;
    }

    result = (quantapdf_output *)calloc(1, sizeof(*result));
    if (result == NULL) {
        quantapdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return QUANTAPDF_ERROR_NOMEM;
    }

    result->data = (unsigned char *)malloc(size);
    if (result->data == NULL) {
        free(result);
        quantapdf_pdf_edit_drop_snapshot_state(
            edit->ctx, buffer, memory_output);
        return QUANTAPDF_ERROR_NOMEM;
    }

    memcpy(result->data, data, size);
    result->size = size;
    quantapdf_pdf_edit_drop_snapshot_state(
        edit->ctx, buffer, memory_output);

    *out_output = result;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output)
{
    quantapdf_output *result = NULL;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdf_edit_snapshot_pdf(edit, &result);
    if (status != QUANTAPDF_OK)
        return status;

#if defined(QUANTAPDF_TESTING)
    if (edit->test_fault ==
        QUANTAPDF_PDF_EDIT_TEST_FAULT_SNAPSHOT_BEFORE_PUBLISH) {
        edit->test_fault = QUANTAPDF_PDF_EDIT_TEST_FAULT_NONE;
        quantapdf_drop_output(result);
        return QUANTAPDF_ERROR_BACKEND;
    }
#endif

    *out_output = result;
    return QUANTAPDF_OK;
}

void quantapdf_drop_pdf_edit(quantapdf_pdf_edit *edit)
{
    quantapdf_dispose_pdf_edit(edit);
}
