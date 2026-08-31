#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static void raw_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define RAW_CHECK(x) raw_check_impl((x), #x, __LINE__)

static int check_source_form(extractpdf_document *document)
{
    extractpdf_form *form = NULL;
    size_t field_count = 0;
    size_t widget_count = 0;

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == 1);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == 1);
    extractpdf_drop_form(form);
    return 0;
}

static pdf_document *open_output_pdf(
    fz_context *ctx,
    const extractpdf_output *output)
{
    const unsigned char *bytes = NULL;
    size_t size = 0;
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    RAW_CHECK(extractpdf_output_data(output, &bytes, &size) == EXTRACTPDF_OK);
    RAW_CHECK(bytes != NULL);
    RAW_CHECK(size != 0);
    fz_var(stream);
    fz_var(document);
    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, bytes, size);
        document = pdf_open_document_with_stream(ctx, stream);
    }
    fz_always(ctx)
    {
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        pdf_drop_document(ctx, document);
        fz_rethrow(ctx);
    }
    return document;
}

static void check_marker(
    fz_context *ctx,
    pdf_obj *xobjects,
    const char *alias,
    const char *expected)
{
    pdf_obj *appearance = pdf_dict_gets(ctx, xobjects, alias);
    pdf_obj *marker;
    const char *name;

    RAW_CHECK(pdf_is_indirect(ctx, appearance));
    RAW_CHECK(pdf_is_stream(ctx, appearance));
    marker = pdf_dict_gets(ctx, appearance, "StateMarker");
    RAW_CHECK(pdf_is_name(ctx, marker));
    name = pdf_to_name(ctx, marker);
    RAW_CHECK(name != NULL);
    RAW_CHECK(strcmp(name, expected) == 0);
}

static size_t find_bytes(
    const unsigned char *data,
    size_t size,
    const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t at;

    if (needle_size == 0 || needle_size > size)
        return SIZE_MAX;
    for (at = 0; at + needle_size <= size; ++at) {
        if (memcmp(data + at, needle, needle_size) == 0)
            return at;
    }
    return SIZE_MAX;
}

static void check_combined_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *page;
    pdf_obj *resources;
    pdf_obj *xobjects;
    pdf_obj *contents;
    fz_buffer *buffer = NULL;
    unsigned char *data = NULL;
    size_t size = 0;
    size_t widget_do;
    size_t square_do;
    int caught_code = FZ_ERROR_NONE;

    RAW_CHECK(ctx != NULL);
    fz_var(document);
    fz_var(buffer);
    fz_var(caught_code);
    fz_try(ctx)
    {
        document = open_output_pdf(ctx, output);
        RAW_CHECK(document != NULL);
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        RAW_CHECK(pdf_is_dict(ctx, root));
        RAW_CHECK(pdf_dict_get(ctx, root, PDF_NAME(AcroForm)) == NULL);

        page = pdf_lookup_page_obj(ctx, document, 0);
        RAW_CHECK(pdf_is_dict(ctx, page));
        RAW_CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);
        resources = pdf_dict_get(ctx, page, PDF_NAME(Resources));
        RAW_CHECK(pdf_is_dict(ctx, resources));
        xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
        RAW_CHECK(pdf_is_dict(ctx, xobjects));
        check_marker(ctx, xobjects, "EPB0", "Widget");
        check_marker(ctx, xobjects, "EPB1", "Square");
        RAW_CHECK(pdf_dict_gets(ctx, xobjects, "EPB2") == NULL);

        contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
        RAW_CHECK(pdf_is_indirect(ctx, contents));
        RAW_CHECK(pdf_is_stream(ctx, contents));
        buffer = pdf_load_stream(ctx, contents);
        RAW_CHECK(buffer != NULL);
        size = fz_buffer_storage(ctx, buffer, &data);
        RAW_CHECK(data != NULL || size == 0);
        widget_do = find_bytes(data, size, "/EPB0 Do");
        square_do = find_bytes(data, size, "/EPB1 Do");
        RAW_CHECK(widget_do != SIZE_MAX);
        RAW_CHECK(square_do != SIZE_MAX);
        RAW_CHECK(widget_do < square_do);
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, buffer);
        buffer = NULL;
        pdf_drop_document(ctx, document);
        document = NULL;
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    RAW_CHECK(caught_code == FZ_ERROR_NONE);
}

int extractpdf_test_pdf_flatten_determinism(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    const uint32_t flags =
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS;

    CHECK(extractpdf_open(FLATTEN_COMBINED_ORDER_PDF, NULL, &document) ==
        EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_form(document) == 0);

    CHECK(extractpdf_flatten_interactive(document, flags, &output) ==
        EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_combined_output(output);
    CHECK(check_source_form(document) == 0);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
