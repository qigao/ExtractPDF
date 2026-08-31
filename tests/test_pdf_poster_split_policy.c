#include "test_pdf_poster_split_internal.h"
#include "pdf_poster_test_api.h"

#include <quantapdf/quantapdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static int create_catalog_signature_fixture(
    const char *source_path,
    const char *output_path)
{
    fz_context *ctx = NULL;
    pdf_document *document = NULL;
    fz_output *output = NULL;
    pdf_obj *signature = NULL;
    pdf_obj *signature_ref = NULL;
    pdf_obj *permissions = NULL;
    pdf_write_options options = pdf_default_write_options;
    int ok = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;
    fz_var(document);
    fz_var(output);
    fz_var(signature);
    fz_var(signature_ref);
    fz_var(permissions);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *root;
        document = pdf_open_document(ctx, source_path);
        root = pdf_dict_get(ctx, pdf_trailer(ctx, document), PDF_NAME(Root));
        signature = pdf_new_dict(ctx, document, 2);
        pdf_dict_put(ctx, signature, PDF_NAME(Type), PDF_NAME(Sig));
        signature_ref = pdf_add_object(ctx, document, signature);
        permissions = pdf_new_dict(ctx, document, 1);
        pdf_dict_puts(ctx, permissions, "DocMDP", signature_ref);
        pdf_dict_puts(ctx, root, "Perms", permissions);

        options.reproducible = 1;
        options.dont_regenerate_id = 1;
        output = fz_new_output_with_path(ctx, output_path, 0);
        pdf_write_document(ctx, document, output, &options);
        fz_close_output(ctx, output);
        ok = 1;
    }
    fz_always(ctx)
    {
        fz_drop_output(ctx, output);
        pdf_drop_obj(ctx, permissions);
        pdf_drop_obj(ctx, signature_ref);
        pdf_drop_obj(ctx, signature);
        pdf_drop_document(ctx, document);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }
    fz_drop_context(ctx);
    return ok;
}

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static void expect_policy_status(
    const char *path,
    size_t columns,
    size_t rows,
    quantapdf_status expected)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = output_sentinel();
    quantapdf_page_poster_split split;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = columns;
    split.rows = rows;
    CHECK(quantapdf_poster_split_pages(document, &split, 1, &output) == expected);
    if (expected == QUANTAPDF_OK) {
        CHECK(output != NULL && output != output_sentinel());
        quantapdf_drop_output(output);
    } else {
        CHECK(output == NULL);
    }
    quantapdf_close(document);
}

static void expect_preflight_faults_are_contained(void)
{
    static const quantapdf_test_pdf_poster_fault faults[] = {
        QUANTAPDF_TEST_PDF_POSTER_FAULT_ANNOTATION_PREFLIGHT,
        QUANTAPDF_TEST_PDF_POSTER_FAULT_WIDGET_PREFLIGHT,
        QUANTAPDF_TEST_PDF_POSTER_FAULT_NAVIGATION_PREFLIGHT
    };
    quantapdf_document *document = NULL;
    quantapdf_page_poster_split split;
    size_t index;

    CHECK(quantapdf_open(POSTER_ROTATE_90_PDF, NULL, &document) == QUANTAPDF_OK);
    split.struct_size = sizeof(split);
    split.page_index = 0;
    split.columns = 2;
    split.rows = 1;

    for (index = 0; index < sizeof(faults) / sizeof(faults[0]); ++index) {
        quantapdf_output *output = output_sentinel();
        int page_count = 0;
        quantapdf_test_pdf_poster_set_fault(document, faults[index]);
        CHECK(quantapdf_poster_split_pages(document, &split, 1, &output) ==
              QUANTAPDF_ERROR_FORMAT);
        CHECK(output == NULL);
        CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
        CHECK(page_count == 1);
    }
    quantapdf_close(document);
}

int poster_run_policy_tests(void)
{
    CHECK(create_catalog_signature_fixture(
        POSTER_ROTATE_90_PDF, POSTER_PERMS_SIGNATURE_PDF));
    expect_policy_status(
        POSTER_PERMS_SIGNATURE_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(remove(POSTER_PERMS_SIGNATURE_PDF) == 0);

    expect_policy_status(
        POSTER_PRODUCTION_BOX_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_TAGGED_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_ROOT_POLICY_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_UNSELECTED_ACTIONS_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_UNSELECTED_ANNOT_ACTION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_FORM_ACTIONS_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_FIELD_ACTION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_OUTLINE_UNSUPPORTED_ACTION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_NONXYZ_DESTINATION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_NULLXYZ_DESTINATION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_OUTSIDE_DESTINATION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_MALFORMED_ANNOTS_PDF, 2, 1, QUANTAPDF_ERROR_FORMAT);
    expect_policy_status(
        POSTER_CROSSING_ANNOTATION_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_CROSSING_WIDGET_PDF, 2, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_policy_status(
        POSTER_ORPHAN_WIDGET_PDF, 2, 1, QUANTAPDF_ERROR_FORMAT);

    /* Expansion-only restrictions do not apply to a pure semantic no-op. */
    expect_policy_status(POSTER_TAGGED_PDF, 1, 1, QUANTAPDF_OK);
    expect_preflight_faults_are_contained();
    return 0;
}
