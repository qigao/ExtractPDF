#include "test_pdf_flatten_internal.h"

#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int extractpdf_pdf_flatten_base_main(void);
int extractpdf_test_pdf_flatten_form_multi(void);
int extractpdf_test_pdf_flatten_form_closure(void);
int extractpdf_test_pdf_flatten_form_ancestor_survives(void);
int extractpdf_test_pdf_flatten_widget_as(void);
int extractpdf_test_pdf_flatten_policy(void);
int extractpdf_test_pdf_flatten_determinism(void);
int extractpdf_test_pdf_flatten_flag_sets(void);
int extractpdf_test_pdf_flatten_render(void);

static void number_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define NUMBER_CHECK(x) number_check_impl((x), #x, __LINE__)

static void number_fixture_path(char *out_path, size_t capacity)
{
    const char *slash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '/');
    const char *backslash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '\\');
    const char *separator = slash;
    static const char name[] = "flatten-number-format.pdf";
    size_t prefix;

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    NUMBER_CHECK(separator != NULL);
    prefix = (size_t)(separator - FLATTEN_COMBINED_ORDER_PDF) + 1;
    NUMBER_CHECK(prefix + sizeof(name) <= capacity);
    memcpy(out_path, FLATTEN_COMBINED_ORDER_PDF, prefix);
    memcpy(out_path + prefix, name, sizeof(name));
}

static void check_number_format(void)
{
    static const char expected[] =
        "q\n"
        "0.123456791 0 0 1.42857146 0 0 cm\n"
        "/EPB0 Do\n"
        "Q\n";
    char path[1024];
    extractpdf_document *source = NULL;
    extractpdf_output *output = NULL;
    const unsigned char *bytes = NULL;
    size_t size = 0;
    fz_context *ctx = NULL;
    fz_stream *stream = NULL;
    pdf_document *document = NULL;
    pdf_obj *page;
    pdf_obj *contents;
    fz_buffer *buffer = NULL;
    unsigned char *data = NULL;
    size_t data_size = 0;
    int caught_code = FZ_ERROR_NONE;

    number_fixture_path(path, sizeof(path));
    NUMBER_CHECK(extractpdf_open(path, NULL, &source) == EXTRACTPDF_OK);
    NUMBER_CHECK(source != NULL);
    NUMBER_CHECK(extractpdf_flatten_interactive(
        source, EXTRACTPDF_FLATTEN_ANNOTATIONS, &output) == EXTRACTPDF_OK);
    NUMBER_CHECK(output != NULL);
    NUMBER_CHECK(extractpdf_output_data(output, &bytes, &size) == EXTRACTPDF_OK);
    NUMBER_CHECK(bytes != NULL);
    NUMBER_CHECK(size != 0);

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    NUMBER_CHECK(ctx != NULL);
    fz_var(stream);
    fz_var(document);
    fz_var(buffer);
    fz_var(caught_code);
    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, bytes, size);
        document = pdf_open_document_with_stream(ctx, stream);
        NUMBER_CHECK(document != NULL);
        page = pdf_lookup_page_obj(ctx, document, 0);
        NUMBER_CHECK(pdf_is_dict(ctx, page));
        contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
        NUMBER_CHECK(pdf_is_indirect(ctx, contents));
        NUMBER_CHECK(pdf_is_stream(ctx, contents));
        buffer = pdf_load_stream(ctx, contents);
        NUMBER_CHECK(buffer != NULL);
        data_size = fz_buffer_storage(ctx, buffer, &data);
        NUMBER_CHECK(data != NULL);
        NUMBER_CHECK(data_size == sizeof(expected) - 1);
        NUMBER_CHECK(memcmp(data, expected, data_size) == 0);
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, buffer);
        pdf_drop_document(ctx, document);
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    NUMBER_CHECK(caught_code == FZ_ERROR_NONE);

    extractpdf_drop_output(output);
    extractpdf_close(source);
}

int main(void)
{
    if (extractpdf_test_pdf_flatten_appearance() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_raw() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_multi() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_closure() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_form_ancestor_survives() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_widget_as() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_policy() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_determinism() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_flag_sets() != 0)
        return 1;
    if (extractpdf_test_pdf_flatten_render() != 0)
        return 1;
    check_number_format();
    return extractpdf_pdf_flatten_base_main();
}
