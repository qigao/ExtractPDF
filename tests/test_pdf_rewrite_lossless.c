#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rewrite_create_gc_fixture(const char *source_path, const char *output_path);
int rewrite_marker_mask(const unsigned char *data, size_t size);

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n",
                __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *file = NULL;
    unsigned char *data;
    long length;

    *out_size = 0;
#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0)
        return NULL;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
#endif
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL ||
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)length;
    return data;
}

static void test_gc_and_repeated_determinism(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    unsigned char *source_data;
    size_t source_size = 0;
    size_t first_size = 0;
    size_t second_size = 0;
    int page_count = 0;

    CHECK(rewrite_create_gc_fixture(ONE_PAGE_PDF, REWRITE_GC_PDF));
    source_data = read_file(REWRITE_GC_PDF, &source_size);
    CHECK(source_data != NULL);
    CHECK(rewrite_marker_mask(source_data, source_size) == 7);
    free(source_data);

    CHECK(quantapdf_open(REWRITE_GC_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    CHECK(quantapdf_rewrite_lossless(document, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_rewrite_lossless(document, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(rewrite_marker_mask(first_data, first_size) == 4);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);

    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL && first_size != 0);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
}

int main(void)
{
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_rewrite_lossless(NULL, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_rewrite_lossless(NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    test_gc_and_repeated_determinism();
    return EXIT_SUCCESS;
}
