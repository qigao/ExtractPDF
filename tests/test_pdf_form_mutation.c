#include <extractpdf/extractpdf.h>

#include <stddef.h>
#include <stdint.h>
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

static void compile_surface(void)
{
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "x";
    value.utf8_size = 1;

    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    if (0) {
        (void)extractpdf_pdf_edit_form_snapshot(edit, &form);
        (void)extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref);
        (void)extractpdf_pdf_edit_form_set_values(edit, &ref, &update);
    }
}

int main(void)
{
    compile_surface();
    return 0;
}
