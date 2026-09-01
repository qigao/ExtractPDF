#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static quantapdf_composer *composer_sentinel(void)
{
    return (quantapdf_composer *)(uintptr_t)1;
}

static int test_create_contract(void)
{
    quantapdf_composer *composer = composer_sentinel();
    quantapdf_composer_options options = {0};

    CHECK(quantapdf_composer_create(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(composer != NULL && composer != composer_sentinel());
    quantapdf_drop_composer(composer);
    quantapdf_drop_composer(NULL);

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_MIN_SIZE - 1u;
    composer = composer_sentinel();
    CHECK(quantapdf_composer_create(&options, &composer) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(composer == NULL);
    return 0;
}

static int test_page_validation_and_capacity(void)
{
    quantapdf_composer_options options = {0};
    quantapdf_composer_page_options page = {0};
    quantapdf_composer *composer = NULL;
    size_t page_index = SIZE_MAX;

    options.struct_size = QUANTAPDF_COMPOSER_OPTIONS_V1_SIZE;
    options.max_pages = 1u;
    CHECK(quantapdf_composer_create(&options, &composer) == QUANTAPDF_OK);

    page.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page.width_points = 612.0f;
    page.height_points = 792.0f;
    page.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_OK);
    CHECK(page_index == 0u);

    page_index = 19u;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(page_index == SIZE_MAX);

    page.width_points = 0.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);
    page.width_points = 612.0f;
    page.height_points = -1.0f;
    CHECK(quantapdf_composer_add_page(composer, &page, &page_index) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_composer(composer);
    return 0;
}

static int test_finish_text_document(void)
{
    static const char expected[] = "Hello copied text";
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {36.0f, 48.0f, 360.0f, 120.0f};
    quantapdf_rect actual_bounds = {0};
    char text_input[] = "Hello copied text";
    char *extracted = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0u;
    size_t second_size = 0u;
    size_t extracted_size = 0u;
    size_t page_index = SIZE_MAX;
    int page_count = 0;

    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 612.0f;
    page_options.height_points = 792.0f;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(
              composer, &page_options, &page_index) == QUANTAPDF_OK);

    text_options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA_BOLD;
    text_options.font_size = 18.0f;
    text_options.argb = UINT32_C(0xff204080);
    text_options.line_height_multiplier = 1.2f;
    text_options.alignment = QUANTAPDF_COMPOSER_TEXT_ALIGN_LEFT;
    text_options.wrap = 1;
    CHECK(quantapdf_composer_draw_text(
              composer, page_index, text_input, &bounds, &text_options) ==
          QUANTAPDF_OK);
    memset(text_input, 'X', sizeof(text_input) - 1u);

    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_size > 8u);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(memcmp(first_data, "%PDF-", 5u) == 0);
    CHECK(quantapdf_output_save_file(first, COMPOSER_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_open(COMPOSER_OUTPUT_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &actual_bounds) == QUANTAPDF_OK);
    CHECK(actual_bounds.x1 == 612.0f);
    CHECK(actual_bounds.y1 == 792.0f);
    CHECK(quantapdf_extract_text(page, &extracted, &extracted_size) ==
          QUANTAPDF_OK);
    CHECK(extracted_size >= sizeof(expected) - 1u);
    CHECK(memcmp(extracted, expected, sizeof(expected) - 1u) == 0);

    quantapdf_free(extracted);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_composer(composer);
    return 0;
}

int main(void)
{
    CHECK(test_create_contract() == 0);
    CHECK(test_page_validation_and_capacity() == 0);
    CHECK(test_finish_text_document() == 0);
    return 0;
}
