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

static void check_zero_rect(const extractpdf_rect *rect)
{
    CHECK(rect->x0 == 0.0f);
    CHECK(rect->y0 == 0.0f);
    CHECK(rect->x1 == 0.0f);
    CHECK(rect->y1 == 0.0f);
}

static void check_tail_bytes(const void *object, size_t start, size_t size, unsigned char expected)
{
    const unsigned char *bytes = (const unsigned char *)object;
    size_t i;

    for (i = start; i < size; ++i)
        CHECK(bytes[i] == expected);
}

int main(void)
{
    const unsigned char sentinel_byte = 0xa5;
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_page *page = NULL;
    extractpdf_text_page *text = (extractpdf_text_page *)&sentinel;
    extractpdf_text_block_info block = { sizeof(block), { 0 } };
    extractpdf_text_line_info line = { sizeof(line), { 0 }, 0.0f, 0.0f, 0 };
    extractpdf_text_span_info span0 = { sizeof(span0), { 0 }, 0.0f, 0, 0 };
    extractpdf_text_span_info span1 = { sizeof(span1), { 0 }, 0.0f, 0, 0 };
    const char *span_text = NULL;
    size_t block_min = offsetof(extractpdf_text_block_info, bounds) + sizeof(block.bounds);
    size_t line_min = offsetof(extractpdf_text_line_info, writing_mode) + sizeof(line.writing_mode);
    size_t span_min = offsetof(extractpdf_text_span_info, bidi_level) + sizeof(span0.bidi_level);
    size_t count = 0;
    size_t size = 0;

    CHECK(extractpdf_extract_structured_text(NULL, &text) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);

    CHECK(extractpdf_open(STRUCTURED_TEXT_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_structured_text(page, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
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

    /* Count and text outputs reset whenever they are supplied on failure. */
    count = 123;
    CHECK(extractpdf_text_block_count(NULL, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(extractpdf_text_block_count(text, NULL) == EXTRACTPDF_ERROR_ARGUMENT);

    count = 123;
    CHECK(extractpdf_text_line_count(text, 1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    count = 123;
    CHECK(extractpdf_text_span_count(text, 0, 1, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    span_text = (const char *)&sentinel;
    size = 123;
    CHECK(extractpdf_text_span_text(text, 0, 0, 2, &span_text, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(span_text == NULL);
    CHECK(size == 0);

    size = 123;
    CHECK(extractpdf_text_span_text(text, 0, 0, 0, NULL, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(size == 0);
    span_text = (const char *)&sentinel;
    CHECK(extractpdf_text_span_text(text, 0, 0, 0, &span_text, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(span_text == NULL);

    /* A valid-sized info object is zeroed on an invalid handle/index. */
    memset(&block, sentinel_byte, sizeof(block));
    block.struct_size = sizeof(block);
    CHECK(extractpdf_text_get_block_info(text, 1, &block) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(block.struct_size == sizeof(block));
    check_zero_rect(&block.bounds);

    memset(&line, sentinel_byte, sizeof(line));
    line.struct_size = sizeof(line);
    CHECK(extractpdf_text_get_line_info(text, 0, 1, &line) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(line.struct_size == sizeof(line));
    check_zero_rect(&line.bounds);
    CHECK(line.direction_x == 0.0f);
    CHECK(line.direction_y == 0.0f);
    CHECK(line.writing_mode == 0);

    memset(&span0, sentinel_byte, sizeof(span0));
    span0.struct_size = sizeof(span0);
    CHECK(extractpdf_text_get_span_info(NULL, 0, 0, 0, &span0) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(span0.struct_size == sizeof(span0));
    check_zero_rect(&span0.bounds);
    CHECK(span0.font_size == 0.0f);
    CHECK(span0.argb == 0);
    CHECK(span0.bidi_level == 0);

    /* Undersized structs are rejected without writing beyond caller-authorized bytes. */
    memset(&block, sentinel_byte, sizeof(block));
    block.struct_size = block_min - 1;
    CHECK(extractpdf_text_get_block_info(text, 0, &block) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(block.struct_size == block_min - 1);
    check_tail_bytes(&block, sizeof(block.struct_size), sizeof(block), sentinel_byte);

    memset(&line, sentinel_byte, sizeof(line));
    line.struct_size = line_min - 1;
    CHECK(extractpdf_text_get_line_info(text, 0, 0, &line) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(line.struct_size == line_min - 1);
    check_tail_bytes(&line, sizeof(line.struct_size), sizeof(line), sentinel_byte);

    memset(&span0, sentinel_byte, sizeof(span0));
    span0.struct_size = span_min - 1;
    CHECK(extractpdf_text_get_span_info(text, 0, 0, 0, &span0) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(span0.struct_size == span_min - 1);
    check_tail_bytes(&span0, sizeof(span0.struct_size), sizeof(span0), sentinel_byte);

    /* Exact V1 minimum sizes are accepted; tail padding remains untouched. */
    memset(&block, sentinel_byte, sizeof(block));
    block.struct_size = block_min;
    CHECK(extractpdf_text_get_block_info(text, 0, &block) == EXTRACTPDF_OK);
    check_rect(&block.bounds);
    check_tail_bytes(&block, block_min, sizeof(block), sentinel_byte);

    memset(&line, sentinel_byte, sizeof(line));
    line.struct_size = line_min;
    CHECK(extractpdf_text_get_line_info(text, 0, 0, &line) == EXTRACTPDF_OK);
    check_rect(&line.bounds);
    CHECK(line.writing_mode == 0);
    check_tail_bytes(&line, line_min, sizeof(line), sentinel_byte);

    memset(&span0, sentinel_byte, sizeof(span0));
    span0.struct_size = span_min;
    CHECK(extractpdf_text_get_span_info(text, 0, 0, 0, &span0) == EXTRACTPDF_OK);
    check_rect(&span0.bounds);
    CHECK(close_float(span0.font_size, 18.0f));
    CHECK(span0.argb == UINT32_C(0xff000000));
    CHECK(span0.bidi_level == 0);
    check_tail_bytes(&span0, span_min, sizeof(span0), sentinel_byte);

    extractpdf_drop_text_page(text);
    extractpdf_drop_text_page(NULL);

    /* Empty pages still produce a valid immutable snapshot. */
    text = NULL;
    CHECK(extractpdf_open(ONE_PAGE_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_extract_structured_text(page, &text) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    extractpdf_drop_page(page);
    extractpdf_close(document);
    page = NULL;
    document = NULL;

    count = 123;
    CHECK(extractpdf_text_block_count(text, &count) == EXTRACTPDF_OK);
    CHECK(count == 0);
    extractpdf_drop_text_page(text);

    return EXIT_SUCCESS;
}
