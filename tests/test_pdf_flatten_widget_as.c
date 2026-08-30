#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <stdio.h>
#include <stdlib.h>

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

static int check_source_form(
    extractpdf_document *document,
    extractpdf_form_field_type expected_type,
    size_t expected_widgets)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_info field = {0};
    size_t field_count = 0;
    size_t widget_count = 0;

    field.struct_size = sizeof(field);
    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &field_count) == EXTRACTPDF_OK);
    CHECK(field_count == 1);
    CHECK(extractpdf_form_widget_count(form, &widget_count) == EXTRACTPDF_OK);
    CHECK(widget_count == expected_widgets);
    CHECK(extractpdf_form_field_get_info(form, 0, &field) == EXTRACTPDF_OK);
    CHECK(field.type == expected_type);
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
    pdf_obj *expected_marker)
{
    pdf_obj *appearance = pdf_dict_gets(ctx, xobjects, alias);
    pdf_obj *marker;

    RAW_CHECK(pdf_is_indirect(ctx, appearance));
    RAW_CHECK(pdf_is_stream(ctx, appearance));
    marker = pdf_dict_gets(ctx, appearance, "StateMarker");
    RAW_CHECK(pdf_is_name(ctx, marker));
    RAW_CHECK(pdf_name_eq(ctx, marker, expected_marker));
}

static void check_checkbox_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *page;
    pdf_obj *resources;
    pdf_obj *xobjects;
    int caught_code = FZ_ERROR_NONE;

    RAW_CHECK(ctx != NULL);
    fz_var(document);
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
        check_marker(ctx, xobjects, "EPB0", PDF_NAME(Yes));
    }
    fz_always(ctx)
    {
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

static void check_radio_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *page;
    pdf_obj *resources;
    pdf_obj *xobjects;
    int caught_code = FZ_ERROR_NONE;

    RAW_CHECK(ctx != NULL);
    fz_var(document);
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
        check_marker(ctx, xobjects, "EPB0", PDF_NAME(OffA));
        check_marker(ctx, xobjects, "EPB1", PDF_NAME(B));
    }
    fz_always(ctx)
    {
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

static int check_checkbox_as(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(FLATTEN_WIDGET_AS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_form(document, EXTRACTPDF_FORM_FIELD_CHECKBOX, 1) == 0);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_checkbox_output(output);

    CHECK(check_source_form(document, EXTRACTPDF_FORM_FIELD_CHECKBOX, 1) == 0);
    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}

static int check_radio_as(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(
        FLATTEN_WIDGET_RADIO_AS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_form(document, EXTRACTPDF_FORM_FIELD_RADIO_BUTTON, 2) == 0);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_radio_output(output);

    CHECK(check_source_form(document, EXTRACTPDF_FORM_FIELD_RADIO_BUTTON, 2) == 0);
    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}

int extractpdf_test_pdf_flatten_widget_as(void)
{
    if (check_checkbox_as() != 0)
        return 1;
    return check_radio_as();
}