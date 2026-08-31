#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

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
    const char *slash = strrchr(FLATTEN_MULTI_WIDGET_PDF, '/');
    const char *backslash = strrchr(FLATTEN_MULTI_WIDGET_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    RAW_CHECK(separator != NULL);
    prefix = (size_t)(separator - FLATTEN_MULTI_WIDGET_PDF) + 1;
    RAW_CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, FLATTEN_MULTI_WIDGET_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static int check_form_counts(
    extractpdf_document *document,
    size_t expected_fields,
    size_t expected_widgets)
{
    extractpdf_form *form = NULL;
    size_t field_count = 0;
    size_t widget_count = 0;

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == expected_fields);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == expected_widgets);
    extractpdf_drop_form(form);
    return 0;
}

static void check_fully_pruned_output(const extractpdf_output *output)
{
    const unsigned char *bytes = NULL;
    size_t size = 0;
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_stream *stream = NULL;
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *page;
    int caught_code = FZ_ERROR_NONE;

    RAW_CHECK(ctx != NULL);
    RAW_CHECK(extractpdf_output_data(output, &bytes, &size) == EXTRACTPDF_OK);
    RAW_CHECK(bytes != NULL);
    RAW_CHECK(size != 0);
    fz_var(stream);
    fz_var(document);
    fz_var(caught_code);
    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, bytes, size);
        document = pdf_open_document_with_stream(ctx, stream);
        RAW_CHECK(document != NULL);
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        RAW_CHECK(pdf_is_dict(ctx, root));
        RAW_CHECK(pdf_dict_get(ctx, root, PDF_NAME(AcroForm)) == NULL);
        page = pdf_lookup_page_obj(ctx, document, 0);
        RAW_CHECK(pdf_is_dict(ctx, page));
        RAW_CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);
    }
    fz_always(ctx)
    {
        fz_drop_stream(ctx, stream);
        pdf_drop_document(ctx, document);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    RAW_CHECK(caught_code == FZ_ERROR_NONE);
}

static int run_general_case(
    const char *path,
    size_t expected_fields,
    size_t expected_widgets)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_form_counts(document, expected_fields, expected_widgets) == 0);
    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_fully_pruned_output(output);
    CHECK(check_form_counts(document, expected_fields, expected_widgets) == 0);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}

int extractpdf_test_pdf_flatten_form_multi(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    char multi_root[1024];
    char deep[1024];

    CHECK(extractpdf_open(
        FLATTEN_MULTI_WIDGET_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_form_counts(document, 1, 2) == 0);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    CHECK(check_form_counts(document, 1, 2) == 0);

    extractpdf_drop_output(output);
    extractpdf_close(document);

    sibling_fixture_path(
        "flatten-form-multi-root.pdf", multi_root, sizeof(multi_root));
    sibling_fixture_path("flatten-form-deep.pdf", deep, sizeof(deep));

    if (run_general_case(multi_root, 2, 2) != 0)
        return 1;
    return run_general_case(deep, 4, 1);
}
