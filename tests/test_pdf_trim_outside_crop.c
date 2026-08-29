#include <extractpdf/extractpdf.h>
#include "test_pdf_trim_internal.h"

#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static void check_rect_close(extractpdf_rect actual, extractpdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(TRIM_INTERACTIVE_PDF, '/');
    const char *backslash = strrchr(TRIM_INTERACTIVE_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - TRIM_INTERACTIVE_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, TRIM_INTERACTIVE_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static extractpdf_rect page_box(
    extractpdf_document *document,
    extractpdf_page_box box)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_box_bounds(page, box, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static extractpdf_rect page_bounds(extractpdf_document *document)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static extractpdf_page_trim make_trim(extractpdf_rect bounds)
{
    extractpdf_page_trim trim;

    trim.struct_size = sizeof(trim);
    trim.page_index = 0;
    trim.bounds = bounds;
    return trim;
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int raw_box_matches(
    fz_context *ctx,
    pdf_obj *box,
    const float expected[4])
{
    int index;

    if (!pdf_is_array(ctx, box) || pdf_array_len(ctx, box) != 4)
        return 0;
    for (index = 0; index < 4; ++index) {
        pdf_obj *value = pdf_array_get(ctx, box, index);
        if (!pdf_is_number(ctx, value) ||
            fabsf(pdf_to_real(ctx, value) - expected[index]) >= 0.001f)
            return 0;
    }
    return 1;
}

static int raw_expect_outside_relation(
    const unsigned char *data,
    size_t size,
    const float expected_media[4],
    const float expected_crop[4])
{
    fz_context *ctx = NULL;
    fz_stream *stream = NULL;
    pdf_document *document = NULL;
    int ok = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(stream);
    fz_var(document);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *page;
        pdf_obj *media;
        pdf_obj *crop;
        float mx0;
        float my0;
        float mx1;
        float my1;
        float cx0;
        float cy0;
        float cx1;
        float cy1;

        stream = fz_open_memory(ctx, data, size);
        document = pdf_open_document_with_stream(ctx, stream);
        page = pdf_lookup_page_obj(ctx, document, 0);
        media = pdf_dict_get_inheritable(ctx, page, PDF_NAME(MediaBox));
        crop = pdf_dict_get_inheritable(ctx, page, PDF_NAME(CropBox));
        if (!raw_box_matches(ctx, media, expected_media) ||
            !raw_box_matches(ctx, crop, expected_crop))
            break;

        mx0 = fminf(pdf_to_real(ctx, pdf_array_get(ctx, media, 0)),
                    pdf_to_real(ctx, pdf_array_get(ctx, media, 2)));
        my0 = fminf(pdf_to_real(ctx, pdf_array_get(ctx, media, 1)),
                    pdf_to_real(ctx, pdf_array_get(ctx, media, 3)));
        mx1 = fmaxf(pdf_to_real(ctx, pdf_array_get(ctx, media, 0)),
                    pdf_to_real(ctx, pdf_array_get(ctx, media, 2)));
        my1 = fmaxf(pdf_to_real(ctx, pdf_array_get(ctx, media, 1)),
                    pdf_to_real(ctx, pdf_array_get(ctx, media, 3)));
        cx0 = fminf(pdf_to_real(ctx, pdf_array_get(ctx, crop, 0)),
                    pdf_to_real(ctx, pdf_array_get(ctx, crop, 2)));
        cy0 = fminf(pdf_to_real(ctx, pdf_array_get(ctx, crop, 1)),
                    pdf_to_real(ctx, pdf_array_get(ctx, crop, 3)));
        cx1 = fmaxf(pdf_to_real(ctx, pdf_array_get(ctx, crop, 0)),
                    pdf_to_real(ctx, pdf_array_get(ctx, crop, 2)));
        cy1 = fmaxf(pdf_to_real(ctx, pdf_array_get(ctx, crop, 1)),
                    pdf_to_real(ctx, pdf_array_get(ctx, crop, 3)));

        ok = (cx0 < mx0 || cy0 < my0 || cx1 > mx1 || cy1 > my1) &&
            fmaxf(mx0, cx0) < fminf(mx1, cx1) &&
            fmaxf(my0, cy0) < fminf(my1, cy1);
    }
    fz_always(ctx)
    {
        pdf_drop_document(ctx, document);
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    fz_drop_context(ctx);
    return ok;
}

int trim_run_outside_crop_test(void)
{
    static const float source_media_raw[4] = {0, 0, 300, 200};
    static const float crop_raw[4] = {-20, -10, 280, 190};
    static const float changed_media_raw[4] = {10, 10, 290, 190};
    char path[1024];
    const char *output_path = "trim-outside-crop-output.pdf";
    extractpdf_document *document;
    extractpdf_document *reopened;
    extractpdf_output *baseline = NULL;
    extractpdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    extractpdf_rect source_media;
    extractpdf_rect source_visible;
    extractpdf_page_trim noop;
    extractpdf_page_trim trim;

    sibling_fixture_path("crop-cropbox-outside-media.pdf", path, sizeof(path));
    document = open_document(path);
    source_media = page_box(document, EXTRACTPDF_PAGE_BOX_MEDIA);
    source_visible = page_bounds(document);
    check_rect_close(source_media, (extractpdf_rect){0, -10, 300, 190});
    check_rect_close(source_visible, (extractpdf_rect){0, 0, 280, 190});

    noop = make_trim(source_media);
    CHECK(extractpdf_trim_pages(document, &noop, 1, &baseline) == EXTRACTPDF_OK);
    CHECK(baseline != NULL);
    CHECK(extractpdf_output_data(
              baseline, &baseline_data, &baseline_size) == EXTRACTPDF_OK);
    CHECK(raw_expect_outside_relation(
              baseline_data, baseline_size, source_media_raw, crop_raw));

    trim = make_trim((extractpdf_rect){10, 0, 290, 180});
    CHECK(extractpdf_trim_pages(document, &trim, 1, &changed) == EXTRACTPDF_OK);
    CHECK(changed != NULL);
    CHECK(extractpdf_output_data(
              changed, &changed_data, &changed_size) == EXTRACTPDF_OK);
    CHECK(raw_expect_outside_relation(
              changed_data, changed_size, changed_media_raw, crop_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              baseline_data, baseline_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    check_rect_close(
        page_box(document, EXTRACTPDF_PAGE_BOX_MEDIA), source_media);
    check_rect_close(page_bounds(document), source_visible);

    (void)remove(output_path);
    CHECK(write_bytes(output_path, changed_data, changed_size));
    reopened = open_document(output_path);
    check_rect_close(page_bounds(reopened), (extractpdf_rect){0, 0, 270, 180});
    check_rect_close(
        page_box(reopened, EXTRACTPDF_PAGE_BOX_CROP),
        (extractpdf_rect){0, 0, 270, 180});
    check_rect_close(
        page_box(reopened, EXTRACTPDF_PAGE_BOX_MEDIA),
        (extractpdf_rect){0, 0, 280, 180});
    extractpdf_close(reopened);
    (void)remove(output_path);

    extractpdf_drop_output(changed);
    extractpdf_drop_output(baseline);
    extractpdf_close(document);
    return 1;
}
