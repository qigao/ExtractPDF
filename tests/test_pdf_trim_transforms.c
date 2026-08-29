#include <extractpdf/extractpdf.h>
#include "test_pdf_trim_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct trim_observation {
    extractpdf_rect page_bounds;
    extractpdf_rect media_bounds;
    extractpdf_rect crop_bounds;
    extractpdf_rect text_bounds;
    extractpdf_quad image_quad;
    extractpdf_rect uri_hotspot;
    char uri[128];
    extractpdf_rect internal_hotspot;
    int internal_target_page;
    extractpdf_point internal_target;
    extractpdf_annotation_info annotation;
    char annotation_contents[128];
    extractpdf_form_field_info field;
    char field_name[128];
    char field_value[128];
    extractpdf_form_widget_info widget;
    extractpdf_outline_info outline;
    char outline_title[128];
} trim_observation;

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static void check_rect_close(extractpdf_rect actual, extractpdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static void check_point_close(extractpdf_point actual, extractpdf_point expected)
{
    CHECK(close_float(actual.x, expected.x));
    CHECK(close_float(actual.y, expected.y));
}

static extractpdf_rect shifted_rect(extractpdf_rect rect, float x, float y)
{
    rect.x0 -= x;
    rect.x1 -= x;
    rect.y0 -= y;
    rect.y1 -= y;
    return rect;
}

static extractpdf_point shifted_point(extractpdf_point point, float x, float y)
{
    point.x -= x;
    point.y -= y;
    return point;
}

static void check_quad_shifted(
    extractpdf_quad actual,
    extractpdf_quad source,
    float x,
    float y)
{
    check_point_close(actual.ul, shifted_point(source.ul, x, y));
    check_point_close(actual.ur, shifted_point(source.ur, x, y));
    check_point_close(actual.ll, shifted_point(source.ll, x, y));
    check_point_close(actual.lr, shifted_point(source.lr, x, y));
}

static void copy_text(
    char *destination,
    size_t capacity,
    const char *source,
    size_t size)
{
    CHECK(destination != NULL);
    CHECK(capacity != 0);
    CHECK(source != NULL);
    CHECK(size < capacity);
    memcpy(destination, source, size);
    destination[size] = '\0';
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *slash = strrchr(TRIM_INTERACTIVE_PDF, '/');
    const char *backslash = strrchr(TRIM_INTERACTIVE_PDF, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - TRIM_INTERACTIVE_PDF) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, TRIM_INTERACTIVE_PDF, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static extractpdf_document *open_document(const char *path)
{
    extractpdf_document *document = NULL;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    return document;
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static extractpdf_rect page_box(
    extractpdf_document *document,
    int page_index,
    extractpdf_page_box box)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_box_bounds(page, box, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static extractpdf_rect page_bounds(
    extractpdf_document *document,
    int page_index)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds = {0};

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(page != NULL);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    extractpdf_drop_page(page);
    return bounds;
}

static extractpdf_rect find_text_bounds(
    extractpdf_page *page,
    const char *needle)
{
    extractpdf_text_page *text = NULL;
    size_t block_count = 0;
    size_t block_index;
    extractpdf_rect result = {0};
    int found = 0;

    CHECK(extractpdf_extract_structured_text(page, &text) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(extractpdf_text_block_count(text, &block_count) == EXTRACTPDF_OK);
    for (block_index = 0; block_index < block_count && !found; ++block_index) {
        size_t line_count = 0;
        size_t line_index;
        CHECK(extractpdf_text_line_count(text, block_index, &line_count) ==
              EXTRACTPDF_OK);
        for (line_index = 0; line_index < line_count && !found; ++line_index) {
            size_t span_count = 0;
            size_t span_index;
            CHECK(extractpdf_text_span_count(
                      text, block_index, line_index, &span_count) ==
                  EXTRACTPDF_OK);
            for (span_index = 0; span_index < span_count; ++span_index) {
                const char *span_text = NULL;
                size_t span_size = 0;
                extractpdf_text_span_info info = {0};

                CHECK(extractpdf_text_span_text(
                          text, block_index, line_index, span_index,
                          &span_text, &span_size) == EXTRACTPDF_OK);
                if (span_text == NULL || strstr(span_text, needle) == NULL)
                    continue;
                info.struct_size = sizeof(info);
                CHECK(extractpdf_text_get_span_info(
                          text, block_index, line_index, span_index, &info) ==
                      EXTRACTPDF_OK);
                result = info.bounds;
                found = 1;
                break;
            }
        }
    }
    extractpdf_drop_text_page(text);
    CHECK(found);
    return result;
}

static void capture_observation(
    extractpdf_document *document,
    trim_observation *observation)
{
    extractpdf_page *page = NULL;
    extractpdf_image_page *images = NULL;
    extractpdf_link_page *links = NULL;
    extractpdf_annotation_page *annotations = NULL;
    extractpdf_form *form = NULL;
    extractpdf_outline *outline = NULL;
    size_t count = 0;
    size_t index;
    int saw_uri = 0;
    int saw_internal = 0;

    memset(observation, 0, sizeof(*observation));
    observation->annotation.struct_size = sizeof(observation->annotation);
    observation->field.struct_size = sizeof(observation->field);
    observation->widget.struct_size = sizeof(observation->widget);
    observation->outline.struct_size = sizeof(observation->outline);

    CHECK(extractpdf_load_page(document, 0, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_bounds(page, &observation->page_bounds) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_box_bounds(
              page, EXTRACTPDF_PAGE_BOX_MEDIA, &observation->media_bounds) ==
          EXTRACTPDF_OK);
    CHECK(extractpdf_page_box_bounds(
              page, EXTRACTPDF_PAGE_BOX_CROP, &observation->crop_bounds) ==
          EXTRACTPDF_OK);
    observation->text_bounds = find_text_bounds(page, "TRIM-TEXT");

    CHECK(extractpdf_extract_images(page, &images) == EXTRACTPDF_OK);
    CHECK(extractpdf_image_count(images, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    {
        extractpdf_image_info info = {0};
        info.struct_size = sizeof(info);
        CHECK(extractpdf_image_get_info(images, 0, &info) == EXTRACTPDF_OK);
        observation->image_quad = info.quad;
    }

    CHECK(extractpdf_extract_links(page, &links) == EXTRACTPDF_OK);
    CHECK(extractpdf_link_count(links, &count) == EXTRACTPDF_OK);
    CHECK(count == 2);
    for (index = 0; index < count; ++index) {
        extractpdf_link_info info = {0};
        info.struct_size = sizeof(info);
        CHECK(extractpdf_link_get_info(links, index, &info) == EXTRACTPDF_OK);
        if (info.kind == EXTRACTPDF_LINK_URI) {
            const char *uri = NULL;
            size_t uri_size = 0;
            CHECK(extractpdf_link_uri(links, index, &uri, &uri_size) ==
                  EXTRACTPDF_OK);
            observation->uri_hotspot = info.hotspot;
            copy_text(observation->uri, sizeof(observation->uri), uri, uri_size);
            saw_uri = 1;
        }
        else if (info.kind == EXTRACTPDF_LINK_INTERNAL) {
            observation->internal_hotspot = info.hotspot;
            observation->internal_target_page = info.target_page;
            observation->internal_target = info.target;
            saw_internal = 1;
        }
    }
    CHECK(saw_uri && saw_internal);

    CHECK(extractpdf_extract_annotations(page, &annotations) == EXTRACTPDF_OK);
    CHECK(extractpdf_annotation_count(annotations, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_annotation_get_info(
              annotations, 0, &observation->annotation) == EXTRACTPDF_OK);
    {
        const char *contents = NULL;
        size_t contents_size = 0;
        CHECK(extractpdf_annotation_contents(
                  annotations, 0, &contents, &contents_size) == EXTRACTPDF_OK);
        copy_text(
            observation->annotation_contents,
            sizeof(observation->annotation_contents),
            contents,
            contents_size);
    }

    CHECK(extractpdf_document_form(document, &form) == EXTRACTPDF_OK);
    CHECK(form != NULL);
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_form_field_get_info(form, 0, &observation->field) ==
          EXTRACTPDF_OK);
    {
        const char *name = NULL;
        size_t name_size = 0;
        extractpdf_form_value_info value_info = {0};
        const char *value = NULL;
        size_t value_size = 0;

        CHECK(extractpdf_form_field_name(form, 0, &name, &name_size) ==
              EXTRACTPDF_OK);
        copy_text(observation->field_name, sizeof(observation->field_name),
                  name, name_size);
        CHECK(observation->field.value_count == 1);
        value_info.struct_size = sizeof(value_info);
        CHECK(extractpdf_form_field_value_get_info(
                  form, 0, 0, &value_info) == EXTRACTPDF_OK);
        CHECK(value_info.kind == EXTRACTPDF_FORM_VALUE_UTF8);
        CHECK(extractpdf_form_field_value_utf8(
                  form, 0, 0, &value, &value_size) == EXTRACTPDF_OK);
        copy_text(observation->field_value, sizeof(observation->field_value),
                  value, value_size);
    }
    CHECK(extractpdf_form_widget_count(form, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_form_widget_get_info(form, 0, &observation->widget) ==
          EXTRACTPDF_OK);

    CHECK(extractpdf_document_outline(document, &outline) == EXTRACTPDF_OK);
    CHECK(outline != NULL);
    CHECK(extractpdf_outline_count(outline, &count) == EXTRACTPDF_OK);
    CHECK(count == 1);
    CHECK(extractpdf_outline_get_info(outline, 0, &observation->outline) ==
          EXTRACTPDF_OK);
    {
        const char *title = NULL;
        size_t title_size = 0;
        CHECK(extractpdf_outline_title(outline, 0, &title, &title_size) ==
              EXTRACTPDF_OK);
        copy_text(observation->outline_title, sizeof(observation->outline_title),
                  title, title_size);
    }

    extractpdf_drop_outline(outline);
    extractpdf_drop_form(form);
    extractpdf_drop_annotation_page(annotations);
    extractpdf_drop_link_page(links);
    extractpdf_drop_image_page(images);
    extractpdf_drop_page(page);
}

static void expect_source_same(
    const trim_observation *actual,
    const trim_observation *source)
{
    check_rect_close(actual->page_bounds, source->page_bounds);
    check_rect_close(actual->media_bounds, source->media_bounds);
    check_rect_close(actual->crop_bounds, source->crop_bounds);
    check_rect_close(actual->text_bounds, source->text_bounds);
    check_quad_shifted(actual->image_quad, source->image_quad, 0, 0);
    check_rect_close(actual->uri_hotspot, source->uri_hotspot);
    CHECK(strcmp(actual->uri, source->uri) == 0);
    check_rect_close(actual->internal_hotspot, source->internal_hotspot);
    CHECK(actual->internal_target_page == source->internal_target_page);
    check_point_close(actual->internal_target, source->internal_target);
    CHECK(actual->annotation.type == source->annotation.type);
    CHECK(actual->annotation.flags == source->annotation.flags);
    check_rect_close(actual->annotation.bounds, source->annotation.bounds);
    CHECK(strcmp(actual->annotation_contents, source->annotation_contents) == 0);
    CHECK(actual->field.type == source->field.type);
    CHECK(actual->field.flags == source->field.flags);
    CHECK(strcmp(actual->field_name, source->field_name) == 0);
    CHECK(strcmp(actual->field_value, source->field_value) == 0);
    CHECK(actual->widget.field_index == source->widget.field_index);
    CHECK(actual->widget.page_index == source->widget.page_index);
    CHECK(actual->widget.flags == source->widget.flags);
    check_rect_close(actual->widget.bounds, source->widget.bounds);
    CHECK(actual->outline.destination_kind == source->outline.destination_kind);
    CHECK(actual->outline.target_page == source->outline.target_page);
    check_point_close(actual->outline.target, source->outline.target);
    CHECK(strcmp(actual->outline_title, source->outline_title) == 0);
}

static void expect_no_crop_trimmed(
    const trim_observation *actual,
    const trim_observation *source)
{
    check_rect_close(actual->page_bounds,
                     (extractpdf_rect){0, 0, 320, 240});
    check_rect_close(actual->media_bounds, actual->page_bounds);
    check_rect_close(actual->crop_bounds, actual->page_bounds);
    check_rect_close(actual->text_bounds,
                     shifted_rect(source->text_bounds, 40, 30));
    check_quad_shifted(actual->image_quad, source->image_quad, 40, 30);
    check_rect_close(actual->uri_hotspot,
                     shifted_rect(source->uri_hotspot, 40, 30));
    CHECK(strcmp(actual->uri, source->uri) == 0);
    check_rect_close(actual->internal_hotspot,
                     shifted_rect(source->internal_hotspot, 40, 30));
    CHECK(actual->internal_target_page == source->internal_target_page);
    check_point_close(actual->internal_target, source->internal_target);
    CHECK(actual->annotation.type == source->annotation.type);
    CHECK(actual->annotation.flags == source->annotation.flags);
    check_rect_close(actual->annotation.bounds,
                     shifted_rect(source->annotation.bounds, 40, 30));
    CHECK(strcmp(actual->annotation_contents, source->annotation_contents) == 0);
    CHECK(actual->field.type == source->field.type);
    CHECK(actual->field.flags == source->field.flags);
    CHECK(strcmp(actual->field_name, "trim.text") == 0);
    CHECK(strcmp(actual->field_name, source->field_name) == 0);
    CHECK(strcmp(actual->field_value, "TRIM-VALUE") == 0);
    CHECK(strcmp(actual->field_value, source->field_value) == 0);
    CHECK(actual->widget.field_index == source->widget.field_index);
    CHECK(actual->widget.page_index == source->widget.page_index);
    CHECK(actual->widget.flags == source->widget.flags);
    check_rect_close(actual->widget.bounds,
                     shifted_rect(source->widget.bounds, 40, 30));
    CHECK(actual->outline.destination_kind == source->outline.destination_kind);
    CHECK(actual->outline.target_page == source->outline.target_page);
    check_point_close(actual->outline.target, source->outline.target);
    CHECK(strcmp(actual->outline_title, source->outline_title) == 0);
}

static extractpdf_page_trim make_trim(
    int page_index,
    extractpdf_rect bounds)
{
    extractpdf_page_trim trim;
    trim.struct_size = sizeof(trim);
    trim.page_index = page_index;
    trim.bounds = bounds;
    return trim;
}

static void test_no_crop_interactive(void)
{
    extractpdf_document *document = open_document(TRIM_INTERACTIVE_PDF);
    extractpdf_output *seed = NULL;
    extractpdf_output *changed = NULL;
    const unsigned char *seed_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t seed_size = 0;
    size_t changed_size = 0;
    extractpdf_rect media = page_box(document, 0, EXTRACTPDF_PAGE_BOX_MEDIA);
    extractpdf_page_trim full = make_trim(0, media);
    extractpdf_page_trim trim = make_trim(
        0, (extractpdf_rect){40, 30, 360, 270});
    trim_observation source;
    trim_observation source_after;
    trim_observation output_observation;
    const float expected_raw[4] = {40, 30, 360, 270};
    const char *output_path = "trim-interactive-output.pdf";
    extractpdf_document *reopened;

    capture_observation(document, &source);
    CHECK(strcmp(source.uri, "https://example.com/trim") == 0);
    CHECK(strcmp(source.annotation_contents, "TRIM-ANNOT") == 0);
    CHECK(strcmp(source.field_name, "trim.text") == 0);
    CHECK(strcmp(source.field_value, "TRIM-VALUE") == 0);

    CHECK(extractpdf_trim_pages(document, &full, 1, &seed) == EXTRACTPDF_OK);
    CHECK(seed != NULL);
    CHECK(extractpdf_output_data(seed, &seed_data, &seed_size) == EXTRACTPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(seed_data, seed_size, 0, 0, NULL));

    CHECK(extractpdf_trim_pages(document, &trim, 1, &changed) == EXTRACTPDF_OK);
    CHECK(changed != NULL);
    capture_observation(document, &source_after);
    expect_source_same(&source_after, &source);

    extractpdf_close(document);
    document = NULL;

    CHECK(extractpdf_output_data(changed, &changed_data, &changed_size) ==
          EXTRACTPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_graph(
              seed_data, seed_size, changed_data, changed_size));
    CHECK(write_bytes(output_path, changed_data, changed_size));

    reopened = open_document(output_path);
    capture_observation(reopened, &output_observation);
    expect_no_crop_trimmed(&output_observation, &source);
    extractpdf_close(reopened);
    remove(output_path);

    extractpdf_drop_output(changed);
    extractpdf_drop_output(seed);
}

static void test_preserved_crop_frames(void)
{
    char path[1024];
    const char *output_path = "trim-preserved-output.pdf";
    extractpdf_document *document;
    extractpdf_document *reopened;
    extractpdf_output *seed = NULL;
    extractpdf_output *changed = NULL;
    const unsigned char *seed_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t seed_size = 0;
    size_t changed_size = 0;
    extractpdf_rect media0;
    extractpdf_rect media1;
    extractpdf_rect visible0;
    extractpdf_page_trim full;
    extractpdf_page_trim trims[2];
    const float physical_raw[4] = {20, 20, 380, 280};
    const float clipped_raw[4] = {80, 60, 380, 280};

    sibling_fixture_path("trim-preserved-crop.pdf", path, sizeof(path));
    document = open_document(path);
    media0 = page_box(document, 0, EXTRACTPDF_PAGE_BOX_MEDIA);
    media1 = page_box(document, 1, EXTRACTPDF_PAGE_BOX_MEDIA);
    visible0 = page_bounds(document, 0);
    check_rect_close(media0, (extractpdf_rect){-50, -40, 350, 260});
    check_rect_close(media1, media0);
    check_rect_close(visible0, (extractpdf_rect){0, 0, 300, 220});

    full = make_trim(0, media0);
    CHECK(extractpdf_trim_pages(document, &full, 1, &seed) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(seed, &seed_data, &seed_size) == EXTRACTPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(seed_data, seed_size, 0, 0, NULL));

    trims[0] = make_trim(0, (extractpdf_rect){
        media0.x0 + 20, media0.y0 + 20,
        media0.x1 - 20, media0.y1 - 20});
    trims[1] = make_trim(1, (extractpdf_rect){
        media1.x0 + 80, media1.y0 + 20,
        media1.x1 - 20, media1.y1 - 60});

    CHECK(extractpdf_trim_pages(document, trims, 2, &changed) == EXTRACTPDF_OK);
    CHECK(changed != NULL);
    CHECK(extractpdf_output_data(changed, &changed_data, &changed_size) ==
          EXTRACTPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, physical_raw));
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 1, 1, clipped_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 1));
    CHECK(trim_raw_expect_preserved_graph(
              seed_data, seed_size, changed_data, changed_size));

    check_rect_close(
        page_box(document, 0, EXTRACTPDF_PAGE_BOX_MEDIA), media0);
    check_rect_close(
        page_box(document, 1, EXTRACTPDF_PAGE_BOX_MEDIA), media1);

    CHECK(write_bytes(output_path, changed_data, changed_size));
    reopened = open_document(output_path);

    check_rect_close(page_bounds(reopened, 0),
                     (extractpdf_rect){0, 0, 300, 220});
    check_rect_close(page_box(reopened, 0, EXTRACTPDF_PAGE_BOX_CROP),
                     (extractpdf_rect){0, 0, 300, 220});
    check_rect_close(page_box(reopened, 0, EXTRACTPDF_PAGE_BOX_MEDIA),
                     (extractpdf_rect){-30, -20, 330, 240});

    check_rect_close(page_bounds(reopened, 1),
                     (extractpdf_rect){0, 0, 270, 200});
    check_rect_close(page_box(reopened, 1, EXTRACTPDF_PAGE_BOX_CROP),
                     (extractpdf_rect){0, 0, 270, 200});
    check_rect_close(page_box(reopened, 1, EXTRACTPDF_PAGE_BOX_MEDIA),
                     (extractpdf_rect){0, -20, 300, 200});

    extractpdf_close(reopened);
    remove(output_path);
    extractpdf_drop_output(changed);
    extractpdf_drop_output(seed);
    extractpdf_close(document);
}

int trim_run_frame_mode_tests(void)
{
    test_no_crop_interactive();
    test_preserved_crop_frames();
    return 1;
}
