#include "pdf_flatten_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int flatten_same_indirect(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right)
{
    return left != NULL && right != NULL &&
        pdf_is_indirect(ctx, left) && pdf_is_indirect(ctx, right) &&
        pdf_to_num(ctx, left) == pdf_to_num(ctx, right) &&
        pdf_to_gen(ctx, left) == pdf_to_gen(ctx, right);
}

static int flatten_rect_equal(fz_rect left, fz_rect right)
{
    return left.x0 == right.x0 && left.y0 == right.y0 &&
        left.x1 == right.x1 && left.y1 == right.y1;
}

static int flatten_matrix_equal(fz_matrix left, fz_matrix right)
{
    return left.a == right.a && left.b == right.b &&
        left.c == right.c && left.d == right.d &&
        left.e == right.e && left.f == right.f;
}

static int flatten_view_matches_target(
    const extractpdf_pdf_appearance_view *view,
    const extractpdf_pdf_flatten_target_plan *target)
{
    if (!flatten_rect_equal(view->rect, target->rect) ||
        view->stateful != target->appearance_stateful ||
        view->state_name_size != target->appearance_state_size ||
        !flatten_rect_equal(view->bbox, target->bbox) ||
        !flatten_matrix_equal(view->matrix, target->appearance_matrix) ||
        !flatten_matrix_equal(view->placement, target->placement))
        return 0;
    if (view->state_name_size != 0 &&
        memcmp(
            view->state_name,
            target->appearance_state,
            view->state_name_size) != 0)
        return 0;
    return 1;
}

void extractpdf_pdf_flatten_drop_runtime(
    fz_context *ctx,
    extractpdf_pdf_flatten_runtime *runtime)
{
    size_t page_index;

    if (runtime == NULL)
        return;
    for (page_index = 0; page_index < runtime->page_count; ++page_index) {
        extractpdf_pdf_flatten_runtime_page *page = &runtime->pages[page_index];
        size_t target_index;
        for (target_index = 0; target_index < page->target_count; ++target_index) {
            pdf_drop_obj(ctx, page->targets[target_index].appearance);
            pdf_drop_obj(ctx, page->targets[target_index].annotation);
        }
        free(page->targets);
        pdf_drop_obj(ctx, page->page);
    }
    free(runtime->pages);
    free(runtime);
}

extractpdf_status extractpdf_pdf_flatten_resolve_runtime(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime **out_runtime)
{
    extractpdf_pdf_flatten_runtime *runtime = NULL;
    extractpdf_status status = EXTRACTPDF_OK;
    int caught_code = FZ_ERROR_NONE;
    size_t page_index;

    if (out_runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_runtime = NULL;
    if (ctx == NULL || document == NULL || plan == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;

    runtime = (extractpdf_pdf_flatten_runtime *)calloc(1, sizeof(*runtime));
    if (runtime == NULL)
        return EXTRACTPDF_ERROR_NOMEM;
    runtime->page_count = plan->page_count;
    if (runtime->page_count != 0) {
        if (runtime->page_count > SIZE_MAX / sizeof(*runtime->pages)) {
            free(runtime);
            return EXTRACTPDF_ERROR_NOMEM;
        }
        runtime->pages = (extractpdf_pdf_flatten_runtime_page *)calloc(
            runtime->page_count, sizeof(*runtime->pages));
        if (runtime->pages == NULL) {
            free(runtime);
            return EXTRACTPDF_ERROR_NOMEM;
        }
    }

    fz_var(status);
    fz_var(caught_code);
    fz_try(ctx)
    {
        for (page_index = 0; page_index < plan->page_count; ++page_index) {
            const extractpdf_pdf_flatten_page_plan *page_plan =
                &plan->pages[page_index];
            extractpdf_pdf_flatten_runtime_page *runtime_page =
                &runtime->pages[page_index];
            pdf_obj *annots;
            size_t target_index;

            runtime_page->page = pdf_keep_obj(
                ctx, pdf_lookup_page_obj(ctx, document, page_plan->page_index));
            if (!pdf_is_dict(ctx, runtime_page->page)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            annots = pdf_dict_get(ctx, runtime_page->page, PDF_NAME(Annots));
            if (!pdf_is_array(ctx, annots)) {
                status = EXTRACTPDF_ERROR_FORMAT;
                break;
            }
            runtime_page->target_count = page_plan->target_count;
            if (runtime_page->target_count > SIZE_MAX / sizeof(*runtime_page->targets)) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }
            runtime_page->targets = (extractpdf_pdf_flatten_runtime_target *)calloc(
                runtime_page->target_count, sizeof(*runtime_page->targets));
            if (runtime_page->targets == NULL && runtime_page->target_count != 0) {
                status = EXTRACTPDF_ERROR_NOMEM;
                break;
            }

            for (target_index = 0; target_index < page_plan->target_count;
                 ++target_index) {
                const extractpdf_pdf_flatten_target_plan *target_plan =
                    &plan->targets[page_plan->first_target + target_index];
                extractpdf_pdf_flatten_runtime_target *runtime_target =
                    &runtime_page->targets[target_index];
                extractpdf_pdf_appearance_view view;
                pdf_obj *annotation;
                pdf_obj *appearance = NULL;

                memset(&view, 0, sizeof(view));
                if (target_plan->annot_ordinal >=
                    (size_t)pdf_array_len(ctx, annots)) {
                    status = EXTRACTPDF_ERROR_FORMAT;
                    break;
                }
                annotation = pdf_array_get(
                    ctx, annots, (int)target_plan->annot_ordinal);
                if (!pdf_is_indirect(ctx, annotation) || !pdf_is_dict(ctx, annotation)) {
                    status = EXTRACTPDF_ERROR_FORMAT;
                    break;
                }
                status = extractpdf_pdf_appearance_resolve(
                    ctx, document, annotation, &view, &appearance);
                if (status != EXTRACTPDF_OK) {
                    extractpdf_pdf_appearance_drop_view(&view);
                    break;
                }
                if (!flatten_view_matches_target(&view, target_plan)) {
                    extractpdf_pdf_appearance_drop_view(&view);
                    status = EXTRACTPDF_ERROR_FORMAT;
                    break;
                }
                extractpdf_pdf_appearance_drop_view(&view);
                runtime_target->annotation = pdf_keep_obj(ctx, annotation);
                runtime_target->appearance = pdf_keep_obj(ctx, appearance);
            }
            if (status != EXTRACTPDF_OK)
                break;
        }
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        status = extractpdf_status_from_mupdf(caught_code);
    if (status != EXTRACTPDF_OK) {
        extractpdf_pdf_flatten_drop_runtime(ctx, runtime);
        return status;
    }
    *out_runtime = runtime;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_format_number(
    float value,
    char *buffer,
    size_t buffer_size)
{
    size_t written;

    if (!isfinite(value) || buffer == NULL || buffer_size == 0)
        return EXTRACTPDF_ERROR_FORMAT;
    written = fz_snprintf(buffer, buffer_size, "%g", (double)value);
    if (written == 0 || written >= buffer_size || strchr(buffer, ',') != NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_append_number(
    fz_context *ctx,
    fz_buffer *buffer,
    float value)
{
    char text[64];
    extractpdf_status status = flatten_format_number(value, text, sizeof(text));
    if (status != EXTRACTPDF_OK)
        return status;
    fz_append_string(ctx, buffer, text);
    return EXTRACTPDF_OK;
}

static extractpdf_status flatten_build_bake_buffer(
    fz_context *ctx,
    const extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_flatten_page_plan *page_plan,
    fz_buffer **out_buffer)
{
    fz_buffer *buffer = NULL;
    size_t target_index;
    extractpdf_status status = EXTRACTPDF_OK;

    *out_buffer = NULL;
    buffer = fz_new_buffer(ctx, 256);
    for (target_index = 0; target_index < page_plan->target_count;
         ++target_index) {
        const extractpdf_pdf_flatten_target_plan *target =
            &plan->targets[page_plan->first_target + target_index];
        const fz_matrix m = target->placement;
        size_t alias_number;
        char alias[64];
        size_t written;

        if (target->appearance_slot >= page_plan->appearance_slot_count) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        alias_number = page_plan->alias_numbers[target->appearance_slot];
        if (alias_number > (size_t)INT_MAX) {
            status = EXTRACTPDF_ERROR_UNSUPPORTED;
            break;
        }

        fz_append_string(ctx, buffer, "q\n");
        status = flatten_append_number(ctx, buffer, m.a);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_byte(ctx, buffer, ' ');
        status = flatten_append_number(ctx, buffer, m.b);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_byte(ctx, buffer, ' ');
        status = flatten_append_number(ctx, buffer, m.c);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_byte(ctx, buffer, ' ');
        status = flatten_append_number(ctx, buffer, m.d);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_byte(ctx, buffer, ' ');
        status = flatten_append_number(ctx, buffer, m.e);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_byte(ctx, buffer, ' ');
        status = flatten_append_number(ctx, buffer, m.f);
        if (status != EXTRACTPDF_OK)
            break;
        fz_append_string(ctx, buffer, " cm\n");
        written = fz_snprintf(
            alias, sizeof(alias), "/EPB%d Do\nQ\n", (int)alias_number);
        if (written == 0 || written >= sizeof(alias)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        fz_append_string(ctx, buffer, alias);
    }

    if (status != EXTRACTPDF_OK) {
        fz_drop_buffer(ctx, buffer);
        return status;
    }
    *out_buffer = buffer;
    return EXTRACTPDF_OK;
}

static pdf_obj *flatten_appearance_for_slot(
    const extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_flatten_page_plan *page_plan,
    const extractpdf_pdf_flatten_runtime_page *runtime_page,
    size_t slot)
{
    size_t target_index;
    for (target_index = 0; target_index < page_plan->target_count;
         ++target_index) {
        const extractpdf_pdf_flatten_target_plan *target =
            &plan->targets[page_plan->first_target + target_index];
        if (target->appearance_slot == slot)
            return runtime_page->targets[target_index].appearance;
    }
    return NULL;
}

static extractpdf_status flatten_apply_page(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    const extractpdf_pdf_flatten_page_plan *page_plan,
    extractpdf_pdf_flatten_runtime_page *runtime_page)
{
    pdf_obj *effective_resources = NULL;
    pdf_obj *effective_xobjects = NULL;
    pdf_obj *resources = NULL;
    pdf_obj *xobjects = NULL;
    pdf_obj *bake_stream = NULL;
    pdf_obj *new_contents = NULL;
    pdf_obj *new_annots = NULL;
    fz_buffer *bake_buffer = NULL;
    int caught_code = FZ_ERROR_NONE;
    int kept_annots = 0;

    fz_var(resources);
    fz_var(xobjects);
    fz_var(bake_stream);
    fz_var(new_contents);
    fz_var(new_annots);
    fz_var(bake_buffer);
    fz_var(caught_code);
    fz_try(ctx)
    {
        pdf_obj *old_contents;
        pdf_obj *old_annots;
        size_t slot;
        int index;
        int count;

        effective_resources = pdf_dict_get_inheritable(
            ctx, runtime_page->page, PDF_NAME(Resources));
        if (effective_resources != NULL && !pdf_is_null(ctx, effective_resources)) {
            if (!pdf_is_dict(ctx, effective_resources))
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Resources must be dictionary");
            resources = pdf_copy_dict(ctx, effective_resources);
            effective_xobjects = pdf_dict_get(
                ctx, effective_resources, PDF_NAME(XObject));
        } else {
            resources = pdf_new_dict(ctx, document, 2);
        }
        if (effective_xobjects != NULL && !pdf_is_null(ctx, effective_xobjects)) {
            if (!pdf_is_dict(ctx, effective_xobjects))
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten XObject must be dictionary");
            xobjects = pdf_copy_dict(ctx, effective_xobjects);
        } else {
            xobjects = pdf_new_dict(ctx, document, (int)page_plan->appearance_slot_count);
        }

        for (slot = 0; slot < page_plan->appearance_slot_count; ++slot) {
            pdf_obj *appearance = flatten_appearance_for_slot(
                plan, page_plan, runtime_page, slot);
            size_t alias_number = page_plan->alias_numbers[slot];
            char name[48];
            size_t written;
            if (appearance == NULL || alias_number > (size_t)INT_MAX)
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten appearance slot unresolved");
            written = fz_snprintf(
                name, sizeof(name), "EPB%d", (int)alias_number);
            if (written == 0 || written >= sizeof(name))
                fz_throw(ctx, FZ_ERROR_FORMAT, "flatten alias formatting failed");
            pdf_dict_puts(ctx, xobjects, name, appearance);
        }
        pdf_dict_put(ctx, resources, PDF_NAME(XObject), xobjects);

        if (flatten_build_bake_buffer(ctx, plan, page_plan, &bake_buffer) !=
            EXTRACTPDF_OK)
            fz_throw(ctx, FZ_ERROR_FORMAT, "flatten bake number formatting failed");
        bake_stream = pdf_add_stream(ctx, document, bake_buffer, NULL, 0);
        if (!pdf_is_indirect(ctx, bake_stream) || !pdf_is_stream(ctx, bake_stream))
            fz_throw(ctx, FZ_ERROR_GENERIC, "flatten bake stream creation failed");

        old_contents = pdf_dict_get(
            ctx, runtime_page->page, PDF_NAME(Contents));
        if (old_contents == NULL || pdf_is_null(ctx, old_contents)) {
            new_contents = pdf_keep_obj(ctx, bake_stream);
        } else if (pdf_is_indirect(ctx, old_contents) &&
                   pdf_is_stream(ctx, old_contents)) {
            new_contents = pdf_new_array(ctx, document, 2);
            pdf_array_push(ctx, new_contents, old_contents);
            pdf_array_push(ctx, new_contents, bake_stream);
        } else if (pdf_is_array(ctx, old_contents)) {
            count = pdf_array_len(ctx, old_contents);
            new_contents = pdf_new_array(ctx, document, count + 1);
            for (index = 0; index < count; ++index)
                pdf_array_push(ctx, new_contents, pdf_array_get(ctx, old_contents, index));
            pdf_array_push(ctx, new_contents, bake_stream);
        } else {
            fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Contents changed after preflight");
        }

        old_annots = pdf_dict_get(ctx, runtime_page->page, PDF_NAME(Annots));
        if (!pdf_is_array(ctx, old_annots))
            fz_throw(ctx, FZ_ERROR_FORMAT, "flatten Annots changed after preflight");
        count = pdf_array_len(ctx, old_annots);
        new_annots = pdf_new_array(ctx, document, count);
        for (index = 0; index < count; ++index) {
            pdf_obj *candidate = pdf_array_get(ctx, old_annots, index);
            size_t selected;
            int remove = 0;
            for (selected = 0; selected < runtime_page->target_count; ++selected) {
                if (flatten_same_indirect(
                        ctx,
                        candidate,
                        runtime_page->targets[selected].annotation)) {
                    remove = 1;
                    break;
                }
            }
            if (!remove) {
                pdf_array_push(ctx, new_annots, candidate);
                ++kept_annots;
            }
        }

        pdf_dict_put(ctx, runtime_page->page, PDF_NAME(Resources), resources);
        pdf_dict_put(ctx, runtime_page->page, PDF_NAME(Contents), new_contents);
        if (kept_annots != 0)
            pdf_dict_put(ctx, runtime_page->page, PDF_NAME(Annots), new_annots);
        else
            pdf_dict_del(ctx, runtime_page->page, PDF_NAME(Annots));
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, bake_buffer);
        pdf_drop_obj(ctx, new_annots);
        pdf_drop_obj(ctx, new_contents);
        pdf_drop_obj(ctx, bake_stream);
        pdf_drop_obj(ctx, xobjects);
        pdf_drop_obj(ctx, resources);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return extractpdf_status_from_mupdf(caught_code);
    return EXTRACTPDF_OK;
}

extractpdf_status extractpdf_pdf_flatten_apply_bake(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime)
{
    size_t page_index;

    if (ctx == NULL || document == NULL || plan == NULL || runtime == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    if (runtime->page_count != plan->page_count)
        return EXTRACTPDF_ERROR_FORMAT;

    for (page_index = 0; page_index < plan->page_count; ++page_index) {
        extractpdf_status status = flatten_apply_page(
            ctx,
            document,
            plan,
            &plan->pages[page_index],
            &runtime->pages[page_index]);
        if (status != EXTRACTPDF_OK)
            return status;
    }
    return EXTRACTPDF_OK;
}
