#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n",
                __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

int main(void)
{
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_rewrite_lossless(NULL, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_rewrite_lossless(NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    return EXIT_SUCCESS;
}
