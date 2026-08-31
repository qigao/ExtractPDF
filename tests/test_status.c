#include <quantapdf/quantapdf.h>

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

int main(void)
{
    fprintf(stderr, "[quantapdf.status] start\n");
    fflush(stderr);

    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_OK), "ok") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_ARGUMENT), "invalid argument") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_IO), "I/O error") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_PASSWORD), "password required or invalid") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_FORMAT), "invalid document format") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_UNSUPPORTED), "unsupported operation or content") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_NOMEM), "out of memory") == 0);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_BACKEND), "backend error") == 0);
    CHECK(strcmp(quantapdf_status_string((quantapdf_status)999), "unknown error") == 0);

    fprintf(stderr, "[quantapdf.status] complete\n");
    fflush(stderr);
    return EXIT_SUCCESS;
}
