#include <extractpdf/extractpdf.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void check_rect(const extractpdf_rect *rect)
{
    CHECK(isfinite(rect->x0));
    CHECK(isfinite(rect->y0));
    CHECK(isfinite(rect->x1));
    CHECK(isfinite(rect->y1));
    CHECK(rect->x1 > rect->x0);
    CHECK(rect->y1 > rect->y0);
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_text_page *text = NULL;
    extractpdf_text_block_info block = { sizeof(block), { 0 } };
    extractpdf_text_line_info line = { sizeof(line), { 0 }, 0.0f, 0.0f, 0 };
    extractpdf_text_span_info span0 = { sizeof(span0), { 0 }, 0.0f, 0, 0 };
    extractpdf_text_span_info span1 = { sizeof(span1), { 0 }, 0.0f, 0, 0 };
    const char *span_text = NULL;
    size_t count = 0;
    size_t size = 0;

    CHECK(extractpdf_open(STRUCTURED_TEXT_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_structured_text(page, &text) == EXTRACTPDF_OK);
    CHECK(text != NULL);

    /* The snapshot must own everything needed by all later accessors. */
    extractpdf_drop_page(page);
    extractpdf_close(document);
    page = NULL;
    document = NULL;

    CHECK(extractpdf_text_block_count(text, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_text_get_block_info(text, 0, &block) == EXTRACTPDF_OK);
    CHECK(block.struct_size == sizeof(block));
    check_rect(&block.bounds);

    CHECK(extractpdf_text_line_count(text, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_text_get_line_info(text, 0, 0, &line) == EXTRACTPDF_OK);
    CHECK(line.struct_size == sizeof(line));
    check_rect(&line.bounds);
    CHECK(close_float(line.direction_x, 1.0f));
    CHECK(close_float(line.direction_y, 0.0f));
    CHECK(line.writing_mode == 0);

    CHECK(extractpdf_text_span_count(text, 0, 0, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);

    CHECK(extractpdf_text_get_span_info(text, 0, 0, 0, &span0) == EXTRACTPDF_OK);
    CHECK(extractpdf_text_get_span_info(text, 0, 0, 1, &span1) == EXTRACTPDF_OK);
    check_rect(&span0.bounds);
    check_rect(&span1.bounds);
    CHECK(close_float(span0.font_size, 18.0f));
    CHECK(close_float(span1.font_size, 12.0f));
    CHECK(span0.argb == UINT32_C(0xff000000));
    CHECK(span1.argb == UINT32_C(0xffff0000));
    CHECK(span0.bidi_level == 0);
    CHECK(span1.bidi_level == 0);
    CHECK(span0.bounds.x0 >= line.bounds.x0 - 0.01f);
    CHECK(span1.bounds.x1 <= line.bounds.x1 + 0.01f);
    CHECK(span1.bounds.x0 >= span0.bounds.x0);

    CHECK(extractpdf_text_span_text(text, 0, 0, 0, &span_text, &size) == EXTRACTPDF_OK);
    CHECK(size == 6);
    CHECK(strcmp(span_text, "Hello ") == 0);

    span_text = NULL;
    size = 0;
    CHECK(extractpdf_text_span_text(text, 0, 0, 1, &span_text, &size) == EXTRACTPDF_OK);
    CHECK(size == 5);
    CHECK(memcmp(span_text, "Caf\xc3\xa9", 5) == 0);
    CHECK(span_text[5] == '\0');

    extractpdf_drop_text_page(text);
    extractpdf_drop_text_page(NULL);
    return EXIT_SUCCESS;
}
