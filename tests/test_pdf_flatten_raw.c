#include "test_pdf_flatten_internal.h"

#include <extractpdf/extractpdf.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void raw_check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) raw_check_impl((x), #x, __LINE__)

static pdf_document *open_file_pdf(fz_context *ctx, const char *filename)
{
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    fz_var(stream);
    fz_var(document);
    fz_try(ctx)
    {
        stream = fz_open_file(ctx, filename);
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

static pdf_obj *page_obj(fz_context *ctx, pdf_document *document, int index)
{
    pdf_obj *page = pdf_lookup_page_obj(ctx, document, index);
    CHECK(pdf_is_dict(ctx, page));
    return page;
}

static int same_indirect(fz_context *ctx, pdf_obj *object, int number)
{
    return object != NULL && pdf_is_indirect(ctx, object) &&
        pdf_to_num(ctx, object) == number && pdf_to_gen(ctx, object) == 0;
}

static void check_stream_contains(
    fz_context *ctx,
    pdf_obj *stream_ref,
    const char *needle)
{
    fz_buffer *buffer = NULL;
    unsigned char *data = NULL;
    size_t size;

    CHECK(stream_ref != NULL);
    CHECK(pdf_is_indirect(ctx, stream_ref));
    CHECK(pdf_is_stream(ctx, stream_ref));
    buffer = pdf_load_stream(ctx, stream_ref);
    CHECK(buffer != NULL);
    size = fz_buffer_storage(ctx, buffer, &data);
    CHECK(data != NULL || size == 0);
    CHECK(size >= strlen(needle));
    {
        size_t at;
        int found = 0;
        for (at = 0; at + strlen(needle) <= size; ++at) {
            if (memcmp(data + at, needle, strlen(needle)) == 0) {
                found = 1;
                break;
            }
        }
        CHECK(found);
    }
    fz_drop_buffer(ctx, buffer);
}

static void check_source_unchanged(
    fz_context *ctx,
    pdf_document *source)
{
    pdf_obj *page;
    pdf_obj *contents;
    pdf_obj *annots;

    page = page_obj(ctx, source, 0);
    CHECK(pdf_dict_get(ctx, page, PDF_NAME(Contents)) == NULL);
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    CHECK(pdf_is_array(ctx, annots));
    CHECK(pdf_array_len(ctx, annots) == 1);

    page = page_obj(ctx, source, 1);
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(same_indirect(ctx, contents, 6));
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    CHECK(pdf_is_array(ctx, annots));
    CHECK(pdf_array_len(ctx, annots) == 2);

    page = page_obj(ctx, source, 2);
    CHECK(pdf_dict_get(ctx, page, PDF_NAME(Resources)) == NULL);
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(pdf_is_array(ctx, contents));
    CHECK(pdf_array_len(ctx, contents) == 2);
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 0), 12));
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 1), 13));
}

static void check_output_graph(
    fz_context *ctx,
    pdf_document *document)
{
    pdf_obj *page;
    pdf_obj *resources;
    pdf_obj *xobjects;
    pdf_obj *contents;
    pdf_obj *annots;
    pdf_obj *parent;
    pdf_obj *parent_resources;

    CHECK(pdf_count_pages(ctx, document) == 4);

    page = page_obj(ctx, document, 0);
    resources = pdf_dict_get(ctx, page, PDF_NAME(Resources));
    CHECK(pdf_is_dict(ctx, resources));
    xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    CHECK(pdf_is_dict(ctx, xobjects));
    CHECK(pdf_dict_gets(ctx, xobjects, "EPB0") != NULL);
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB1"), 4));
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(pdf_is_indirect(ctx, contents));
    CHECK(pdf_is_stream(ctx, contents));
    check_stream_contains(ctx, contents, "q\n1 0 0 1 20 20 cm\n/EPB1 Do\nQ\n");
    CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);

    page = page_obj(ctx, document, 1);
    resources = pdf_dict_get(ctx, page, PDF_NAME(Resources));
    CHECK(pdf_is_dict(ctx, resources));
    xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    CHECK(pdf_is_dict(ctx, xobjects));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB0"), 7));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "Keep"), 7));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB1"), 9));
    CHECK(pdf_dict_gets(ctx, xobjects, "EPB2") == NULL);
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(pdf_is_array(ctx, contents));
    CHECK(pdf_array_len(ctx, contents) == 2);
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 0), 6));
    check_stream_contains(ctx, pdf_array_get(ctx, contents, 1), "/EPB1 Do\nQ\nq\n");
    CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);

    page = page_obj(ctx, document, 2);
    resources = pdf_dict_get(ctx, page, PDF_NAME(Resources));
    CHECK(pdf_is_dict(ctx, resources));
    parent = pdf_dict_get(ctx, page, PDF_NAME(Parent));
    CHECK(pdf_is_dict(ctx, parent));
    parent_resources = pdf_dict_get(ctx, parent, PDF_NAME(Resources));
    CHECK(pdf_is_dict(ctx, parent_resources));
    CHECK(resources != parent_resources);
    CHECK(pdf_dict_gets(ctx, resources, "ProcSet") != NULL);
    xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    CHECK(pdf_is_dict(ctx, xobjects));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB0"), 25));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB1"), 15));
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(pdf_is_array(ctx, contents));
    CHECK(pdf_array_len(ctx, contents) == 3);
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 0), 12));
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 1), 13));
    CHECK(pdf_is_stream(ctx, pdf_array_get(ctx, contents, 2)));
    annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    CHECK(pdf_is_array(ctx, annots));
    CHECK(pdf_array_len(ctx, annots) == 1);
    CHECK(pdf_name_eq(
        ctx,
        pdf_dict_get(ctx, pdf_array_get(ctx, annots, 0), PDF_NAME(Subtype)),
        PDF_NAME(Link)));

    page = page_obj(ctx, document, 3);
    resources = pdf_dict_get(ctx, page, PDF_NAME(Resources));
    CHECK(pdf_is_dict(ctx, resources));
    xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    CHECK(pdf_is_dict(ctx, xobjects));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB0"), 25));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB1"), 19));
    CHECK(same_indirect(ctx, pdf_dict_gets(ctx, xobjects, "EPB2"), 21));
    contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    CHECK(pdf_is_array(ctx, contents));
    CHECK(pdf_array_len(ctx, contents) == 2);
    CHECK(same_indirect(ctx, pdf_array_get(ctx, contents, 0), 18));
    check_stream_contains(ctx, pdf_array_get(ctx, contents, 1), "/EPB1 Do\nQ\nq\n");
    check_stream_contains(ctx, pdf_array_get(ctx, contents, 1), "/EPB2 Do\nQ\n");
    CHECK(pdf_dict_get(ctx, page, PDF_NAME(Annots)) == NULL);
}

int extractpdf_test_pdf_flatten_raw(void)
{
    extractpdf_document *document = NULL;
    extractpdf_document *malformed = NULL;
    extractpdf_output *output = NULL;
    fz_context *ctx = NULL;
    pdf_document *source = NULL;
    pdf_document *result = NULL;
    int caught = 0;

    CHECK(extractpdf_open(FLATTEN_CONTENTS_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    CHECK(ctx != NULL);
    fz_var(source);
    fz_var(result);
    fz_var(caught);
    fz_try(ctx)
    {
        source = open_file_pdf(ctx, FLATTEN_CONTENTS_PDF);
        result = open_output_pdf(ctx, output);
        check_source_unchanged(ctx, source);
        check_output_graph(ctx, result);
    }
    fz_catch(ctx)
    {
        caught = 1;
        fz_report_error(ctx);
    }
    pdf_drop_document(ctx, result);
    pdf_drop_document(ctx, source);
    fz_drop_context(ctx);
    CHECK(!caught);

    extractpdf_drop_output(output);
    output = NULL;
    extractpdf_close(document);
    document = NULL;

    CHECK(extractpdf_open(
        FLATTEN_CONTENTS_MALFORMED_PDF, NULL, &malformed) == EXTRACTPDF_OK);
    CHECK(malformed != NULL);
    CHECK(extractpdf_flatten_interactive(
        malformed,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    extractpdf_close(malformed);
    return 0;
}
