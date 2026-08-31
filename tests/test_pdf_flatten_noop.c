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
        exit(EXIT_FAILURE); \
    } \
} while (0)

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

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;
    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static pdf_document *open_output_pdf(
    fz_context *ctx,
    const extractpdf_output *output)
{
    const unsigned char *bytes = NULL;
    size_t size = 0;
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    CHECK(extractpdf_output_data(output, &bytes, &size) == EXTRACTPDF_OK);
    CHECK(bytes != NULL);
    CHECK(size != 0);
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

static int same_identity(fz_context *ctx, pdf_obj *left, pdf_obj *right)
{
    return left != NULL && right != NULL &&
        pdf_is_indirect(ctx, left) && pdf_is_indirect(ctx, right) &&
        pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
        pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
}

static void check_equal_outputs(
    const extractpdf_output *left,
    const extractpdf_output *right)
{
    const unsigned char *left_bytes = NULL;
    const unsigned char *right_bytes = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(left != NULL);
    CHECK(right != NULL);
    CHECK(extractpdf_output_data(left, &left_bytes, &left_size) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(right, &right_bytes, &right_size) == EXTRACTPDF_OK);
    CHECK(left_bytes != NULL);
    CHECK(right_bytes != NULL);
    CHECK(left_size != 0);
    CHECK(left_size == right_size);
    CHECK(memcmp(left_bytes, right_bytes, left_size) == 0);
}

static void check_source_form_counts(
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
}

static void check_neutral_link_noop(void)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;
    int page_count = 0;

    sibling_fixture_path(
        "flatten-noop-neutral-link.pdf", path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &first) == EXTRACTPDF_OK);
    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &second) == EXTRACTPDF_OK);
    check_equal_outputs(first, second);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(document);
}

static void check_zero_widget_noop(void)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *first = NULL;
    extractpdf_output *second = NULL;

    sibling_fixture_path(
        "flatten-noop-zero-widget.pdf", path, sizeof(path));
    document = open_document(path);
    check_source_form_counts(document, 1, 0);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_WIDGETS, &first) == EXTRACTPDF_OK);
    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_WIDGETS, &second) == EXTRACTPDF_OK);
    check_equal_outputs(first, second);
    check_source_form_counts(document, 1, 0);

    extractpdf_drop_output(second);
    extractpdf_drop_output(first);
    extractpdf_close(document);
}

static void expect_format(const char *filename, uint32_t flags)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *output = (extractpdf_output *)(uintptr_t)1;
    int page_count = 0;

    sibling_fixture_path(filename, path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);
    CHECK(extractpdf_flatten_interactive(document, flags, &output) ==
        EXTRACTPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 1);
    extractpdf_close(document);
}

static void inspect_annotation_only_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *acroform;
    pdf_obj *fields;
    pdf_obj *page0;
    pdf_obj *page1;
    pdf_obj *annots1;
    pdf_obj *widget;
    pdf_obj *ap;
    pdf_obj *normal;
    int caught = FZ_ERROR_NONE;

    CHECK(ctx != NULL);
    fz_var(document);
    fz_var(caught);
    fz_try(ctx)
    {
        document = open_output_pdf(ctx, output);
        CHECK(document != NULL);
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        CHECK(pdf_is_dict(ctx, root));
        acroform = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
        CHECK(pdf_is_dict(ctx, acroform));
        fields = pdf_dict_get(ctx, acroform, PDF_NAME(Fields));
        CHECK(pdf_is_array(ctx, fields));
        CHECK(pdf_array_len(ctx, fields) == 1);

        page0 = pdf_lookup_page_obj(ctx, document, 0);
        page1 = pdf_lookup_page_obj(ctx, document, 1);
        CHECK(pdf_is_dict(ctx, page0));
        CHECK(pdf_is_dict(ctx, page1));
        CHECK(pdf_dict_get(ctx, page0, PDF_NAME(Annots)) == NULL);
        annots1 = pdf_dict_get(ctx, page1, PDF_NAME(Annots));
        CHECK(pdf_is_array(ctx, annots1));
        CHECK(pdf_array_len(ctx, annots1) == 1);
        widget = pdf_array_get(ctx, annots1, 0);
        CHECK(pdf_name_eq(
            ctx, pdf_dict_get(ctx, widget, PDF_NAME(Subtype)), PDF_NAME(Widget)));
        CHECK(same_identity(ctx, pdf_array_get(ctx, fields, 0), widget));
        ap = pdf_dict_get(ctx, widget, PDF_NAME(AP));
        CHECK(pdf_is_dict(ctx, ap));
        normal = pdf_dict_get(ctx, ap, PDF_NAME(N));
        CHECK(pdf_is_indirect(ctx, normal));
        CHECK(pdf_is_stream(ctx, normal));
    }
    fz_always(ctx)
    {
        pdf_drop_document(ctx, document);
    }
    fz_catch(ctx)
    {
        caught = fz_caught(ctx);
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    CHECK(caught == FZ_ERROR_NONE);
}

static void inspect_widget_only_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *page0;
    pdf_obj *page1;
    pdf_obj *annots0;
    pdf_obj *square;
    pdf_obj *ap;
    pdf_obj *normal;
    int caught = FZ_ERROR_NONE;

    CHECK(ctx != NULL);
    fz_var(document);
    fz_var(caught);
    fz_try(ctx)
    {
        document = open_output_pdf(ctx, output);
        CHECK(document != NULL);
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        CHECK(pdf_is_dict(ctx, root));
        CHECK(pdf_dict_get(ctx, root, PDF_NAME(AcroForm)) == NULL);

        page0 = pdf_lookup_page_obj(ctx, document, 0);
        page1 = pdf_lookup_page_obj(ctx, document, 1);
        CHECK(pdf_is_dict(ctx, page0));
        CHECK(pdf_is_dict(ctx, page1));
        annots0 = pdf_dict_get(ctx, page0, PDF_NAME(Annots));
        CHECK(pdf_is_array(ctx, annots0));
        CHECK(pdf_array_len(ctx, annots0) == 1);
        square = pdf_array_get(ctx, annots0, 0);
        CHECK(pdf_name_eq(
            ctx, pdf_dict_get(ctx, square, PDF_NAME(Subtype)), PDF_NAME(Square)));
        ap = pdf_dict_get(ctx, square, PDF_NAME(AP));
        CHECK(pdf_is_dict(ctx, ap));
        normal = pdf_dict_get(ctx, ap, PDF_NAME(N));
        CHECK(pdf_is_indirect(ctx, normal));
        CHECK(pdf_is_stream(ctx, normal));
        CHECK(pdf_dict_get(ctx, page1, PDF_NAME(Annots)) == NULL);
    }
    fz_always(ctx)
    {
        pdf_drop_document(ctx, document);
    }
    fz_catch(ctx)
    {
        caught = fz_caught(ctx);
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    CHECK(caught == FZ_ERROR_NONE);
}

static void check_flag_isolation(void)
{
    char path[1024];
    extractpdf_document *document;
    extractpdf_output *annotations = NULL;
    extractpdf_output *widgets = NULL;
    int page_count = 0;

    sibling_fixture_path("flatten-flag-isolation.pdf", path, sizeof(path));
    document = open_document(path);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    check_source_form_counts(document, 1, 1);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_ANNOTATIONS, &annotations) == EXTRACTPDF_OK);
    CHECK(annotations != NULL);
    inspect_annotation_only_output(annotations);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    check_source_form_counts(document, 1, 1);

    CHECK(extractpdf_flatten_interactive(
        document, EXTRACTPDF_FLATTEN_WIDGETS, &widgets) == EXTRACTPDF_OK);
    CHECK(widgets != NULL);
    inspect_widget_only_output(widgets);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    check_source_form_counts(document, 1, 1);

    extractpdf_drop_output(widgets);
    extractpdf_drop_output(annotations);
    extractpdf_close(document);
}

int extractpdf_test_pdf_flatten_noop(void)
{
    check_neutral_link_noop();
    check_zero_widget_noop();
    expect_format(
        "flatten-noop-malformed-annots.pdf",
        EXTRACTPDF_FLATTEN_ANNOTATIONS);
    expect_format(
        "flatten-noop-malformed-acroform.pdf",
        EXTRACTPDF_FLATTEN_WIDGETS);
    check_flag_isolation();
    return 0;
}
