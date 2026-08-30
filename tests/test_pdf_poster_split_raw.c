#include "test_pdf_poster_split_internal.h"

#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <string.h>

static pdf_document *open_pdf_bytes(
    fz_context *ctx,
    const unsigned char *data,
    size_t size)
{
    fz_stream *stream = NULL;
    pdf_document *document = NULL;

    fz_var(stream);
    fz_var(document);
    fz_try(ctx)
    {
        stream = fz_open_memory(ctx, data, size);
        document = pdf_open_document_with_stream(ctx, stream);
    }
    fz_always(ctx)
    {
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        pdf_drop_document(ctx, document);
        fz_rethrow(ctx);
    }
    return document;
}

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.001f;
}

static int box_matches(fz_context *ctx, pdf_obj *box, const float expected[4])
{
    int i;
    if (!pdf_is_array(ctx, box) || pdf_array_len(ctx, box) != 4)
        return 0;
    for (i = 0; i < 4; ++i) {
        pdf_obj *item = pdf_array_get(ctx, box, i);
        if (!pdf_is_number(ctx, item) ||
            !close_float(pdf_to_real(ctx, item), expected[i]))
            return 0;
    }
    return 1;
}

static int same_object_semantics(fz_context *ctx, pdf_obj *left, pdf_obj *right)
{
    if (left == NULL || right == NULL)
        return left == right;
    if (pdf_is_indirect(ctx, left) && pdf_is_indirect(ctx, right))
        return pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
            pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
    return pdf_objcmp_deep(ctx, left, right) == 0;
}

static int forbidden_tile_key_present(fz_context *ctx, pdf_obj *page)
{
    return pdf_dict_get(ctx, page, PDF_NAME(BleedBox)) != NULL ||
        pdf_dict_get(ctx, page, PDF_NAME(TrimBox)) != NULL ||
        pdf_dict_get(ctx, page, PDF_NAME(ArtBox)) != NULL ||
        pdf_dict_get(ctx, page, PDF_NAME(AA)) != NULL ||
        pdf_dict_get(ctx, page, PDF_NAME(StructParents)) != NULL;
}

int poster_raw_check_basic_tiles(
    const unsigned char *data,
    size_t size,
    int first_tile_page,
    size_t tile_count,
    const float (*expected_boxes)[4],
    int expected_rotate,
    float expected_user_unit)
{
    fz_context *ctx = NULL;
    pdf_document *document = NULL;
    int ok = 0;

    if (data == NULL || size == 0 || first_tile_page < 0 ||
        tile_count == 0 || expected_boxes == NULL)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(document);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *first_page;
        pdf_obj *first_contents;
        pdf_obj *first_resources;
        size_t i;

        document = open_pdf_bytes(ctx, data, size);
        first_page = pdf_lookup_page_obj(ctx, document, first_tile_page);
        first_contents = pdf_dict_get(ctx, first_page, PDF_NAME(Contents));
        first_resources = pdf_dict_get(ctx, first_page, PDF_NAME(Resources));
        ok = 1;

        for (i = 0; i < tile_count && ok; ++i) {
            pdf_obj *page = pdf_lookup_page_obj(
                ctx, document, first_tile_page + (int)i);
            pdf_obj *media = pdf_dict_get(ctx, page, PDF_NAME(MediaBox));
            pdf_obj *crop = pdf_dict_get(ctx, page, PDF_NAME(CropBox));
            pdf_obj *rotate = pdf_dict_get(ctx, page, PDF_NAME(Rotate));
            pdf_obj *user_unit = pdf_dict_get(ctx, page, PDF_NAME(UserUnit));

            if (!box_matches(ctx, media, expected_boxes[i]) ||
                !box_matches(ctx, crop, expected_boxes[i]) ||
                forbidden_tile_key_present(ctx, page) ||
                !same_object_semantics(
                    ctx, first_contents,
                    pdf_dict_get(ctx, page, PDF_NAME(Contents))) ||
                !same_object_semantics(
                    ctx, first_resources,
                    pdf_dict_get(ctx, page, PDF_NAME(Resources)))) {
                ok = 0;
                break;
            }

            if (expected_rotate == 0) {
                if (rotate != NULL)
                    ok = 0;
            } else if (!pdf_is_int(ctx, rotate) ||
                       pdf_to_int(ctx, rotate) != expected_rotate) {
                ok = 0;
            }

            if (close_float(expected_user_unit, 1.0f)) {
                if (user_unit != NULL)
                    ok = 0;
            } else if (!pdf_is_number(ctx, user_unit) ||
                       !close_float(pdf_to_real(ctx, user_unit),
                                   expected_user_unit)) {
                ok = 0;
            }
        }
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}

static int text_string_equals(fz_context *ctx, pdf_obj *object, const char *expected)
{
    const char *text;
    if (!pdf_is_string(ctx, object))
        return 0;
    text = pdf_to_text_string(ctx, object);
    return text != NULL && strcmp(text, expected) == 0;
}

int poster_raw_check_interactive(
    const unsigned char *data,
    size_t size)
{
    fz_context *ctx = NULL;
    pdf_document *document = NULL;
    int ok = 0;

    if (data == NULL || size == 0)
        return 0;
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (ctx == NULL)
        return 0;

    fz_var(document);
    fz_var(ok);
    fz_try(ctx)
    {
        pdf_obj *fields;
        pdf_obj *parent;
        pdf_obj *kids;
        pdf_obj *field_widget;
        pdf_obj *page3;
        pdf_obj *page3_annots;
        pdf_obj *page_widget = NULL;
        pdf_obj *widget_parent;
        pdf_obj *widget_page;
        int page_count;
        int widget_count = 0;
        int page_index;

        document = open_pdf_bytes(ctx, data, size);
        page_count = pdf_count_pages(ctx, document);
        fields = pdf_dict_getp(
            ctx, pdf_trailer(ctx, document), "Root/AcroForm/Fields");
        ok = pdf_is_array(ctx, fields) && pdf_array_len(ctx, fields) == 1;
        if (!ok)
            goto done;

        parent = pdf_array_get(ctx, fields, 0);
        kids = pdf_dict_get(ctx, parent, PDF_NAME(Kids));
        ok = pdf_is_indirect(ctx, parent) &&
            text_string_equals(ctx, pdf_dict_get(ctx, parent, PDF_NAME(T)), "poster") &&
            pdf_is_array(ctx, kids) && pdf_array_len(ctx, kids) == 1;
        if (!ok)
            goto done;

        field_widget = pdf_array_get(ctx, kids, 0);
        ok = pdf_is_indirect(ctx, field_widget) &&
            pdf_name_eq(
                ctx,
                pdf_dict_get(ctx, field_widget, PDF_NAME(Subtype)),
                PDF_NAME(Widget)) &&
            text_string_equals(
                ctx, pdf_dict_get(ctx, field_widget, PDF_NAME(T)), "text") &&
            text_string_equals(
                ctx, pdf_dict_get(ctx, field_widget, PDF_NAME(V)), "POSTER-VALUE");
        if (!ok)
            goto done;

        widget_parent = pdf_dict_get(ctx, field_widget, PDF_NAME(Parent));
        ok = same_object_semantics(ctx, widget_parent, parent);
        if (!ok)
            goto done;

        for (page_index = 0; page_index < page_count; ++page_index) {
            pdf_obj *page = pdf_lookup_page_obj(ctx, document, page_index);
            pdf_obj *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
            int annot_count;
            int annot_index;

            if (annots == NULL)
                continue;
            if (!pdf_is_array(ctx, annots)) {
                ok = 0;
                goto done;
            }
            annot_count = pdf_array_len(ctx, annots);
            for (annot_index = 0; annot_index < annot_count; ++annot_index) {
                pdf_obj *annotation = pdf_array_get(ctx, annots, annot_index);
                if (!pdf_is_dict(ctx, annotation) ||
                    !pdf_name_eq(
                        ctx,
                        pdf_dict_get(ctx, annotation, PDF_NAME(Subtype)),
                        PDF_NAME(Widget)))
                    continue;
                ++widget_count;
                if (page_index == 3)
                    page_widget = annotation;
            }
        }
        ok = widget_count == 1 && page_widget != NULL &&
            same_object_semantics(ctx, page_widget, field_widget);
        if (!ok)
            goto done;

        page3 = pdf_lookup_page_obj(ctx, document, 3);
        page3_annots = pdf_dict_get(ctx, page3, PDF_NAME(Annots));
        ok = pdf_is_array(ctx, page3_annots) &&
            same_object_semantics(ctx, page_widget, field_widget);
        if (!ok)
            goto done;

        widget_page = pdf_dict_get(ctx, field_widget, PDF_NAME(P));
        ok = widget_page != NULL &&
            pdf_objcmp_resolve(ctx, widget_page, page3) == 0;

done:
        ;
    }
    fz_catch(ctx)
    {
        ok = 0;
    }

    pdf_drop_document(ctx, document);
    fz_drop_context(ctx);
    return ok;
}
