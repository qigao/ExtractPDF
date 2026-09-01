#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>

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

int main(void)
{
    CHECK(test_create_contract() == 0);
    CHECK(test_page_validation_and_capacity() == 0);
    return 0;
}
