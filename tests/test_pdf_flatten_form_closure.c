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

static int check_source_counts(
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

static int same_identity(fz_context *ctx, pdf_obj *left, pdf_obj *right)
{
    int left_indirect = left != NULL && pdf_is_indirect(ctx, left);
    int right_indirect = right != NULL && pdf_is_indirect(ctx, right);

    if (left_indirect || right_indirect) {
        if (!left_indirect || !right_indirect)
            return 0;
        return pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
            pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
    }
    return left == right;
}

static void check_closure_output(const extractpdf_output *output)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    pdf_document *document = NULL;
    pdf_obj *root;
    pdf_obj *acroform;
    pdf_obj *fields;
    pdf_obj *root_field;
    pdf_obj *kids;
    pdf_obj *audit_root_kids;
    pdf_obj *audit_mid_kids;
    pdf_obj *keep_ref;
    pdf_obj *page;
    pdf_obj *q;
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
        acroform = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
        RAW_CHECK(pdf_is_dict(ctx, acroform));
        fields = pdf_dict_get(ctx, acroform, PDF_NAME(Fields));
        RAW_CHECK(pdf_is_array(ctx, fields));
        RAW_CHECK(pdf_array_len(ctx, fields) == 1);
        root_field = pdf_array_get(ctx, fields, 0);
        RAW_CHECK(pdf_is_dict(ctx, root_field));

        kids = pdf_dict_get(ctx, root_field, PDF_NAME(Kids));
        audit_root_kids = pdf_dict_gets(ctx, root, "AuditRootKids");
        audit_mid_kids = pdf_dict_gets(ctx, root, "AuditMidKids");
        keep_ref = pdf_dict_gets(ctx, acroform, "KeepRef");
        RAW_CHECK(pdf_is_array(ctx, kids));
        RAW_CHECK(pdf_array_len(ctx, kids) == 1);
        RAW_CHECK(pdf_is_array(ctx, audit_root_kids));
        RAW_CHECK(pdf_array_len(ctx, audit_root_kids) == 2);
        RAW_CHECK(pdf_is_array(ctx, audit_mid_kids));
        RAW_CHECK(pdf_array_len(ctx, audit_mid_kids) == 1);
        RAW_CHECK(!same_identity(ctx, kids, audit_root_kids));
        RAW_CHECK(same_identity(ctx, pdf_array_get(ctx, kids, 0), keep_ref));
        RAW_CHECK(same_identity(
            ctx, pdf_array_get(ctx, audit_root_kids, 1), keep_ref));
        RAW_CHECK(same_identity(
            ctx,
            pdf_dict_get(ctx, pdf_array_get(ctx, audit_root_kids, 0), PDF_NAME(Kids)),
            audit_mid_kids));

        q = pdf_dict_get(ctx, acroform, PDF_NAME(Q));
        RAW_CHECK(pdf_is_int(ctx, q));
        RAW_CHECK(pdf_to_int(ctx, q) == 1);
        page = pdf_lookup_page_obj(ctx, document, 0);
        RAW_CHECK(pdf_is_dict(ctx, page));
        RAW_CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);
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

int extractpdf_test_pdf_flatten_form_closure(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(
        FLATTEN_FORM_CLOSURE_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(check_source_counts(document, 2, 1) == 0);

    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_WIDGETS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    check_closure_output(output);

    CHECK(check_source_counts(document, 2, 1) == 0);
    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
