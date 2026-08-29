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
    extractpdf_form *form = NULL;
    extractpdf_form_field_type type = EXTRACTPDF_FORM_FIELD_UNKNOWN;
    extractpdf_form_value_presence presence = EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE;
    extractpdf_form_value_kind value_kind = EXTRACTPDF_FORM_VALUE_UTF8;
    extractpdf_form_option_kind option_kind = EXTRACTPDF_FORM_OPTION_BUTTON_STATE;
    extractpdf_form_field_info field = {0};
    extractpdf_form_value_info value = {0};
    extractpdf_form_option_info option = {0};
    extractpdf_form_widget_info widget = {0};
    extractpdf_document *document = NULL;
    const char *text = NULL;
    size_t size = 0;
    size_t count = 0;

    field.struct_size = sizeof(field);
    value.struct_size = sizeof(value);
    option.struct_size = sizeof(option);
    widget.struct_size = sizeof(widget);
    (void)type; (void)presence; (void)value_kind; (void)option_kind;

    if (0) {
        (void)extractpdf_document_form(document, &form);
        (void)extractpdf_form_field_count(form, &count);
        (void)extractpdf_form_field_get_info(form, 0, &field);
        (void)extractpdf_form_field_name(form, 0, &text, &size);
        (void)extractpdf_form_field_label(form, 0, &text, &size);
        (void)extractpdf_form_field_value_get_info(form, 0, 0, &value);
        (void)extractpdf_form_field_value_utf8(form, 0, 0, &text, &size);
        (void)extractpdf_form_field_option_get_info(form, 0, 0, &option);
        (void)extractpdf_form_field_option_export(form, 0, 0, &text, &size);
        (void)extractpdf_form_field_option_display(form, 0, 0, &text, &size);
        (void)extractpdf_form_widget_count(form, &count);
        (void)extractpdf_form_widget_get_info(form, 0, &widget);
        extractpdf_drop_form(form);
    }
}

static void expect_empty(const char *path)
{
    extractpdf_document *document = NULL;
    extractpdf_form *form = NULL;
    size_t fields = 99;
    size_t widgets = 99;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    extractpdf_close(document);
    CHECK(extractpdf_form_field_count(form, &fields) == EXTRACTPDF_OK);
    CHECK(fields == 0);
    CHECK(extractpdf_form_widget_count(form, &widgets) == EXTRACTPDF_OK);
    CHECK(widgets == 0);
    extractpdf_drop_form(form);
}

static void expect_string(
    const extractpdf_form *form,
    size_t field_index,
    int label,
    const char *expected,
    int present)
{
    const char *text = (const char *)(uintptr_t)1;
    size_t size = 99;
    extractpdf_status status;

    if (label)
        status = extractpdf_form_field_label(
            form, field_index, &text, &size);
    else
        status = extractpdf_form_field_name(
            form, field_index, &text, &size);
    CHECK(status == EXTRACTPDF_OK);

    if (!present) {
        CHECK(text == NULL);
        CHECK(size == 0);
        return;
    }

    CHECK(text != NULL);
    CHECK(size == strlen(expected));
    CHECK(memcmp(text, expected, size) == 0);
    CHECK(text[size] == '\0');
}

static void expect_structure_info(
    const extractpdf_form *form,
    size_t index)
{
    extractpdf_form_field_info info = {0};
    info.struct_size = sizeof(info);
    CHECK(extractpdf_form_field_get_info(form, index, &info) == EXTRACTPDF_OK);
    CHECK(info.type == EXTRACTPDF_FORM_FIELD_TEXT);
    CHECK(info.flags == 0);
    CHECK(info.value_presence == EXTRACTPDF_FORM_VALUE_MISSING);
    CHECK(info.value_count == 0);
    CHECK(info.option_count == 0);
    CHECK(info.widget_count == 0);
    CHECK(info.is_multiselect == 0);
    CHECK(info.is_signed == 0);
}

static void expect_extract_status(
    const char *path,
    extractpdf_status expected)
{
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_form *form = (extractpdf_form *)&sentinel;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(document, &form) == expected);
    CHECK(form == NULL);
    extractpdf_close(document);
}

static void test_api_shell(void)
{
    int sentinel = 0;
    extractpdf_form *form = (extractpdf_form *)&sentinel;
    size_t count = 99;
    const char *text = (const char *)&sentinel;
    size_t size = 99;

    CHECK(EXTRACTPDF_FORM_FIELD_UNKNOWN == 0);
    CHECK(EXTRACTPDF_FORM_FIELD_PUSH_BUTTON == 1);
    CHECK(EXTRACTPDF_FORM_FIELD_CHECKBOX == 2);
    CHECK(EXTRACTPDF_FORM_FIELD_RADIO_BUTTON == 3);
    CHECK(EXTRACTPDF_FORM_FIELD_TEXT == 4);
    CHECK(EXTRACTPDF_FORM_FIELD_COMBO_BOX == 5);
    CHECK(EXTRACTPDF_FORM_FIELD_LIST_BOX == 6);
    CHECK(EXTRACTPDF_FORM_FIELD_SIGNATURE == 7);
    CHECK(EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE == 0);
    CHECK(EXTRACTPDF_FORM_VALUE_MISSING == 1);
    CHECK(EXTRACTPDF_FORM_VALUE_PRESENT == 2);
    CHECK(EXTRACTPDF_FORM_VALUE_UTF8 == 1);
    CHECK(EXTRACTPDF_FORM_VALUE_OPTION == 2);
    CHECK(EXTRACTPDF_FORM_OPTION_BUTTON_STATE == 1);
    CHECK(EXTRACTPDF_FORM_OPTION_CHOICE == 2);

    CHECK(extractpdf_document_form(NULL, &form) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(form == NULL);
    CHECK(extractpdf_document_form(NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_form_field_count(NULL, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(extractpdf_form_widget_count(NULL, &count) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(extractpdf_form_field_name(NULL, 0, &text, &size) == EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(text == NULL && size == 0);
    extractpdf_drop_form(NULL);
}

static void test_empty_and_non_pdf(void)
{
    int sentinel = 0;
    extractpdf_document *document = NULL;
    extractpdf_form *form = (extractpdf_form *)&sentinel;

    expect_empty(NO_ACROFORM_PDF);
    expect_empty(ACROFORM_NO_FIELDS_PDF);
    expect_empty(ACROFORM_EMPTY_FIELDS_PDF);

    CHECK(extractpdf_open(NON_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_ERROR_UNSUPPORTED);
    CHECK(form == NULL);
    extractpdf_close(document);
}

static void test_structure(void)
{
    extractpdf_document *document = NULL;
    extractpdf_form *form = NULL;
    size_t count = 99;

    CHECK(extractpdf_open(ACROFORM_STRUCTURE_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);
    CHECK(extractpdf_form_widget_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == 0);

    expect_structure_info(form, 0);
    expect_string(form, 0, 0, "profile.nickname", 1);
    expect_string(form, 0, 1, "Profile label", 1);

    expect_structure_info(form, 1);
    expect_string(form, 1, 0, "repeat", 1);
    expect_string(form, 1, 1, "", 0);

    expect_structure_info(form, 2);
    expect_string(form, 2, 0, "", 0);
    expect_string(form, 2, 1, "", 0);

    expect_structure_info(form, 3);
    expect_string(form, 3, 0, "", 1);
    expect_string(form, 3, 1, "", 0);

    extractpdf_drop_form(form);
    extractpdf_close(document);

    expect_extract_status(ACROFORM_BAD_ROOT_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_BAD_FIELDS_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_BAD_KID_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_CYCLE_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_REPEATED_NODE_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_PARENT_MISMATCH_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_ROOT_PARENT_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_DEPTH_257_PDF, EXTRACTPDF_ERROR_UNSUPPORTED);
    expect_extract_status(ACROFORM_MIXED_GROUP_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_GROUP_CONFLICT_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_DUPLICATE_NAME_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_PERIOD_NAME_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_BAD_FT_PDF, EXTRACTPDF_ERROR_FORMAT);
    expect_extract_status(ACROFORM_BAD_FF_PDF, EXTRACTPDF_ERROR_FORMAT);
}

static void future_red(void) { CHECK(0); }

static void run_case(const char *name)
{
    if (strcmp(name, "api-shell") == 0) test_api_shell();
    else if (strcmp(name, "empty") == 0) test_empty_and_non_pdf();
    else if (strcmp(name, "structure") == 0) test_structure();
    else if (strcmp(name, "widgets") == 0) future_red();
    else if (strcmp(name, "scalar-values") == 0) future_red();
    else if (strcmp(name, "choice-values") == 0) future_red();
    else if (strcmp(name, "button-values") == 0) future_red();
    else if (strcmp(name, "lifetime") == 0) future_red();
    else CHECK(0);
}

int main(int argc, char **argv)
{
    compile_surface();
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        run_case(argv[2]);
        return EXIT_SUCCESS;
    }
    CHECK(argc == 1);
    test_api_shell();
    test_empty_and_non_pdf();
    test_structure();
    future_red();
    return EXIT_SUCCESS;
}
