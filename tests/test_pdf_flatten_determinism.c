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

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '/');
    const char *backslash = strrchr(FLATTEN_COMBINED_ORDER_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - FLATTEN_COMBINED_ORDER_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, FLATTEN_COMBINED_ORDER_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path, const char *password)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, password, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_flatten_unsupported(extractpdf_document *document)
{
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
}

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

static void check_security_fail_closed(void)
{
    extractpdf_document *document = NULL;
    char non_pdf[1024];
    char encrypted_pdf[1024];
    char signed_pdf[1024];

    sibling_fixture_path("composition-non-pdf.txt", non_pdf, sizeof(non_pdf));
    sibling_fixture_path(
        "encrypted-one-page.pdf", encrypted_pdf, sizeof(encrypted_pdf));
    sibling_fixture_path(
        "annotation-mutation-signed.pdf", signed_pdf, sizeof(signed_pdf));

    document = open_document(non_pdf, NULL);
    expect_flatten_unsupported(document);
    extractpdf_close(document);

    document = open_document(encrypted_pdf, "user-pass");
    expect_flatten_unsupported(document);
    extractpdf_close(document);

    document = open_document(signed_pdf, NULL);
    expect_flatten_unsupported(document);
    extractpdf_close(document);
}

static void check_semantic_noop(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    const unsigned char *first_bytes = NULL;
    const unsigned char *second_bytes = NULL;
    const unsigned char *lifetime_bytes = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t lifetime_size = 0;
    int page_count = 0;

    document = open_document(FLATTEN_NO_ANNOTATIONS_PDF, NULL);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &first) == EXTRACTPDF_OK);
    CHECK(first != NULL);
    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &second) == EXTRACTPDF_OK);
    CHECK(second != NULL);

    CHECK(extractpdf_output_data(first, &first_bytes, &first_size) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(second, &second_bytes, &second_size) ==
        EXTRACTPDF_OK);
    CHECK(first_bytes != NULL);
    CHECK(second_bytes != NULL);
    CHECK(first_size != 0);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_bytes, second_bytes, first_size) == 0);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    extractpdf_close(document);
    document = NULL;

    CHECK(extractpdf_output_data(first, &lifetime_bytes, &lifetime_size) ==
        EXTRACTPDF_OK);
    CHECK(lifetime_bytes != NULL);
    CHECK(lifetime_size == first_size);
    CHECK(memcmp(lifetime_bytes, first_bytes, first_size) == 0);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
}

int extractpdf_test_pdf_flatten_determinism(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    extractpdf_output *repeated = NULL;
    const unsigned char *first_bytes = NULL;
    const unsigned char *second_bytes = NULL;
    const unsigned char *lifetime_bytes = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t lifetime_size = 0;
    const uint32_t flags =
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS;

    check_security_fail_closed();
    check_semantic_noop();

    CHECK(extractpdf_open(FLATTEN_COMBINED_ORDER_PDF, NULL, &document) ==
        EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_form(document) == 0);

    CHECK(extractpdf_flatten_interactive(document, flags, &output) ==
        EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_combined_output(output);
    CHECK(check_source_form(document) == 0);

    CHECK(extractpdf_flatten_interactive(document, flags, &repeated) ==
        EXTRACTPDF_OK);
    CHECK(repeated != NULL);
    CHECK(extractpdf_output_data(output, &first_bytes, &first_size) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(repeated, &second_bytes, &second_size) ==
        EXTRACTPDF_OK);
    CHECK(first_bytes != NULL);
    CHECK(second_bytes != NULL);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_bytes, second_bytes, first_size) == 0);
    CHECK(check_source_form(document) == 0);

    extractpdf_close(document);
    document = NULL;

    CHECK(extractpdf_output_data(output, &lifetime_bytes, &lifetime_size) ==
        EXTRACTPDF_OK);
    CHECK(lifetime_bytes != NULL);
    CHECK(lifetime_size == first_size);
    CHECK(memcmp(lifetime_bytes, first_bytes, first_size) == 0);
    check_combined_output(output);

    extractpdf_drop_output(repeated);
    extractpdf_drop_output(output);
    return 0;
}
