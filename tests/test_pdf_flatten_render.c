#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int compare_rendered_page(
    extractpdf_document *source,
    extractpdf_document *result,
    int page_index)
{
    extractpdf_page *source_page = NULL;
    extractpdf_page *result_page = NULL;
    extractpdf_bitmap *source_bitmap = NULL;
    extractpdf_bitmap *result_bitmap = NULL;
    extractpdf_render_options options = {sizeof(options), 72.0f, 0.0f, 0, {0}, 0};
    const unsigned char *source_data = NULL;
    const unsigned char *result_data = NULL;
    size_t source_size = 0;
    size_t result_size = 0;
    int sw = 0, sh = 0, ss = 0, sc = 0;
    int rw = 0, rh = 0, rs = 0, rc = 0;

    CHECK(extractpdf_load_page(source, page_index, &source_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(result, page_index, &result_page) == EXTRACTPDF_OK);
    CHECK(extractpdf_render_page_with_options(source_page, &options, &source_bitmap) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_render_page_with_options(result_page, &options, &result_bitmap) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(source_bitmap, &sw, &sh, &ss, &sc) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_dimensions(result_bitmap, &rw, &rh, &rs, &rc) ==
        EXTRACTPDF_OK);
    CHECK(sw == rw);
    CHECK(sh == rh);
    CHECK(ss == rs);
    CHECK(sc == rc);
    CHECK(extractpdf_bitmap_data(source_bitmap, &source_data, &source_size) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_bitmap_data(result_bitmap, &result_data, &result_size) ==
        EXTRACTPDF_OK);
    CHECK(source_data != NULL);
    CHECK(result_data != NULL);
    CHECK(source_size == result_size);
    CHECK(memcmp(source_data, result_data, source_size) == 0);

    extractpdf_drop_bitmap(result_bitmap);
    extractpdf_drop_bitmap(source_bitmap);
    extractpdf_drop_page(result_page);
    extractpdf_drop_page(source_page);
    return 0;
}

static int check_fixture(
    const char *path,
    uint32_t flags,
    int first_page,
    int page_count)
{
    extractpdf_document *source = NULL;
    extractpdf_document *result = NULL;
    extractpdf_output *output = NULL;
    int index;

    (void)remove(FLATTEN_RENDER_OUTPUT_PDF);
    CHECK(extractpdf_open(path, NULL, &source) == EXTRACTPDF_OK);
    CHECK(source != NULL);
    CHECK(extractpdf_flatten_interactive(source, flags, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    CHECK(extractpdf_output_save_file(output, FLATTEN_RENDER_OUTPUT_PDF) ==
        EXTRACTPDF_OK);
    CHECK(extractpdf_open(FLATTEN_RENDER_OUTPUT_PDF, NULL, &result) == EXTRACTPDF_OK);
    CHECK(result != NULL);

    for (index = 0; index < page_count; ++index)
        CHECK(compare_rendered_page(source, result, first_page + index) == 0);

    extractpdf_close(result);
    extractpdf_drop_output(output);
    extractpdf_close(source);
    (void)remove(FLATTEN_RENDER_OUTPUT_PDF);
    return 0;
}

int extractpdf_test_pdf_flatten_render(void)
{
    if (check_fixture(
            FLATTEN_APPEARANCE_PDF,
            EXTRACTPDF_FLATTEN_ANNOTATIONS,
            0,
            6) != 0)
        return 1;
    if (check_fixture(
            FLATTEN_WIDGETS_PDF,
            EXTRACTPDF_FLATTEN_WIDGETS,
            0,
            1) != 0)
        return 1;
    if (check_fixture(
            FLATTEN_WIDGET_AS_PDF,
            EXTRACTPDF_FLATTEN_WIDGETS,
            0,
            1) != 0)
        return 1;
    if (check_fixture(
            FLATTEN_WIDGET_RADIO_AS_PDF,
            EXTRACTPDF_FLATTEN_WIDGETS,
            0,
            1) != 0)
        return 1;
    return check_fixture(
        FLATTEN_COMBINED_ORDER_PDF,
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS,
        0,
        1);
}
