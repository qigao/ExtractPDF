#include "qpdf_document.h"
#include "../annotation_snapshot.h"
#include "../outline_snapshot.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFWriter.hh>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <limits>
#include <set>
#include <string>
#include <vector>

struct quantapdf_qpdf_document {
    std::shared_ptr<QPDF> pdf;
};

static quantapdf_status quantapdf_status_from_qpdf(
    QPDFExc const& error) noexcept
{
    switch (error.getErrorCode()) {
    case qpdf_e_password:
        return QUANTAPDF_ERROR_PASSWORD;
    case qpdf_e_unsupported:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    case qpdf_e_system:
        return QUANTAPDF_ERROR_IO;
    case qpdf_e_damaged_pdf:
    case qpdf_e_pages:
    case qpdf_e_object:
    case qpdf_e_json:
    case qpdf_e_linearization:
        return QUANTAPDF_ERROR_FORMAT;
    case qpdf_e_success:
    case qpdf_e_internal:
    default:
        return QUANTAPDF_ERROR_BACKEND;
    }
}

static QPDFObjectHandle quantapdf_qpdf_inherited_value(
    QPDFObjectHandle current,
    char const *key)
{
    while (!current.isNull()) {
        QPDFObjectHandle value = current.getKey(key);
        if (!value.isNull())
            return value;
        current = current.getKey("/Parent");
    }
    return QPDFObjectHandle::newNull();
}

static quantapdf_annotation_type quantapdf_qpdf_annotation_type(
    std::string const& name) noexcept
{
    if (name == "/Text") return QUANTAPDF_ANNOTATION_TEXT;
    if (name == "/FreeText") return QUANTAPDF_ANNOTATION_FREE_TEXT;
    if (name == "/Line") return QUANTAPDF_ANNOTATION_LINE;
    if (name == "/Square") return QUANTAPDF_ANNOTATION_SQUARE;
    if (name == "/Circle") return QUANTAPDF_ANNOTATION_CIRCLE;
    if (name == "/Polygon") return QUANTAPDF_ANNOTATION_POLYGON;
    if (name == "/PolyLine") return QUANTAPDF_ANNOTATION_POLY_LINE;
    if (name == "/Highlight") return QUANTAPDF_ANNOTATION_HIGHLIGHT;
    if (name == "/Underline") return QUANTAPDF_ANNOTATION_UNDERLINE;
    if (name == "/Squiggly") return QUANTAPDF_ANNOTATION_SQUIGGLY;
    if (name == "/StrikeOut") return QUANTAPDF_ANNOTATION_STRIKE_OUT;
    if (name == "/Redact") return QUANTAPDF_ANNOTATION_REDACT;
    if (name == "/Stamp") return QUANTAPDF_ANNOTATION_STAMP;
    if (name == "/Caret") return QUANTAPDF_ANNOTATION_CARET;
    if (name == "/Ink") return QUANTAPDF_ANNOTATION_INK;
    if (name == "/FileAttachment") return QUANTAPDF_ANNOTATION_FILE_ATTACHMENT;
    if (name == "/Sound") return QUANTAPDF_ANNOTATION_SOUND;
    if (name == "/Movie") return QUANTAPDF_ANNOTATION_MOVIE;
    if (name == "/RichMedia") return QUANTAPDF_ANNOTATION_RICH_MEDIA;
    if (name == "/Screen") return QUANTAPDF_ANNOTATION_SCREEN;
    if (name == "/PrinterMark") return QUANTAPDF_ANNOTATION_PRINTER_MARK;
    if (name == "/TrapNet") return QUANTAPDF_ANNOTATION_TRAP_NET;
    if (name == "/Watermark") return QUANTAPDF_ANNOTATION_WATERMARK;
    if (name == "/3D") return QUANTAPDF_ANNOTATION_3D;
    if (name == "/Projection") return QUANTAPDF_ANNOTATION_PROJECTION;
    return QUANTAPDF_ANNOTATION_UNKNOWN;
}

struct quantapdf_qpdf_captured_annotation {
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    std::string contents;
    bool has_contents;
};

struct quantapdf_qpdf_captured_outline_node {
    size_t parent_index = SIZE_MAX;
    size_t first_child_index = SIZE_MAX;
    size_t next_sibling_index = SIZE_MAX;
    quantapdf_outline_destination_kind destination_kind =
        QUANTAPDF_OUTLINE_DESTINATION_NONE;
    int target_page = -1;
    quantapdf_point target = {};
    std::string title;
    std::string uri;
    bool has_title = false;
    bool is_open = false;
};

static bool quantapdf_qpdf_same_object(
    QPDFObjectHandle const& left,
    QPDFObjectHandle const& right)
{
    return left.isIndirect() && right.isIndirect() &&
        left.getObjGen() == right.getObjGen();
}

static quantapdf_status quantapdf_qpdf_decode_outline_destination(
    QPDF& pdf,
    QPDFObjectHandle const& item,
    quantapdf_qpdf_captured_outline_node *node)
{
    QPDFObjectHandle destination = item.getKey("/Dest");
    QPDFObjectHandle action = item.getKey("/A");

    if (destination.isNull() && !action.isNull()) {
        if (!action.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle action_type = action.getKey("/S");
        if (!action_type.isName())
            return QUANTAPDF_ERROR_FORMAT;
        if (action_type.getName() == "/URI") {
            QPDFObjectHandle uri = action.getKey("/URI");
            if (!uri.isString())
                return QUANTAPDF_ERROR_FORMAT;
            node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
            node->uri = uri.getUTF8Value();
            return QUANTAPDF_OK;
        }
        if (action_type.getName() != "/GoTo")
            return QUANTAPDF_ERROR_UNSUPPORTED;
        destination = action.getKey("/D");
    }
    if (destination.isNull())
        return QUANTAPDF_OK;
    if (!destination.isArray() || destination.getArrayNItems() < 1)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle destination_page = destination.getArrayItem(0);
    auto const& pages = pdf.getAllPages();
    int page_index = -1;
    for (size_t index = 0; index < pages.size(); ++index) {
        if (quantapdf_qpdf_same_object(destination_page, pages[index])) {
            page_index = static_cast<int>(index);
            break;
        }
    }
    if (page_index < 0)
        return QUANTAPDF_ERROR_FORMAT;

    QPDFObjectHandle crop = quantapdf_qpdf_inherited_value(
        pages[static_cast<size_t>(page_index)], "/CropBox");
    if (crop.isNull()) {
        crop = quantapdf_qpdf_inherited_value(
            pages[static_cast<size_t>(page_index)], "/MediaBox");
    }
    if (!crop.isArray() || crop.getArrayNItems() != 4 ||
        !crop.getArrayItem(0).isNumber() ||
        !crop.getArrayItem(3).isNumber())
        return QUANTAPDF_ERROR_FORMAT;
    double const crop_left = crop.getArrayItem(0).getNumericValue();
    double const crop_top = crop.getArrayItem(3).getNumericValue();
    double user_unit = 1.0;
    QPDFObjectHandle user_unit_object =
        pages[static_cast<size_t>(page_index)].getKey("/UserUnit");
    if (!user_unit_object.isNull()) {
        if (!user_unit_object.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        user_unit = user_unit_object.getNumericValue();
    }

    double x = crop_left;
    double y = crop_top;
    if (destination.getArrayNItems() >= 4 &&
        destination.getArrayItem(1).isName() &&
        destination.getArrayItem(1).getName() == "/XYZ") {
        QPDFObjectHandle x_object = destination.getArrayItem(2);
        QPDFObjectHandle y_object = destination.getArrayItem(3);
        if (!x_object.isNull()) {
            if (!x_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            x = x_object.getNumericValue();
        }
        if (!y_object.isNull()) {
            if (!y_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            y = y_object.getNumericValue();
        }
    }
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_INTERNAL;
    node->target_page = page_index;
    node->target.x = static_cast<float>((x - crop_left) * user_unit);
    node->target.y = static_cast<float>((crop_top - y) * user_unit);
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_qpdf_walk_outline_siblings(
    QPDF& pdf,
    QPDFObjectHandle first,
    QPDFObjectHandle last,
    QPDFObjectHandle const& parent_object,
    size_t parent_index,
    size_t depth,
    std::vector<quantapdf_qpdf_captured_outline_node> *nodes,
    std::set<QPDFObjGen> *seen)
{
    if (first.isNull() != last.isNull())
        return QUANTAPDF_ERROR_FORMAT;
    if (first.isNull())
        return QUANTAPDF_OK;
    if (depth > 256u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    QPDFObjectHandle previous = QPDFObjectHandle::newNull();
    size_t previous_index = SIZE_MAX;
    QPDFObjectHandle current = first;
    while (!current.isNull()) {
        if (!current.isDictionary() || !current.isIndirect())
            return QUANTAPDF_ERROR_FORMAT;
        if (!seen->insert(current.getObjGen()).second)
            return QUANTAPDF_ERROR_FORMAT;
        if (!quantapdf_qpdf_same_object(
                current.getKey("/Parent"), parent_object))
            return QUANTAPDF_ERROR_FORMAT;

        QPDFObjectHandle current_previous = current.getKey("/Prev");
        if (previous.isNull()) {
            if (!current_previous.isNull())
                return QUANTAPDF_ERROR_FORMAT;
        } else if (!quantapdf_qpdf_same_object(current_previous, previous)) {
            return QUANTAPDF_ERROR_FORMAT;
        }

        size_t const current_index = nodes->size();
        if (previous_index != SIZE_MAX)
            (*nodes)[previous_index].next_sibling_index = current_index;
        nodes->emplace_back();
        auto *node = &nodes->back();
        node->parent_index = parent_index;

        QPDFObjectHandle title = current.getKey("/Title");
        if (!title.isNull()) {
            if (!title.isString())
                return QUANTAPDF_ERROR_FORMAT;
            node->title = title.getUTF8Value();
            node->has_title = true;
        }
        QPDFObjectHandle count = current.getKey("/Count");
        if (!count.isNull()) {
            if (!count.isInteger())
                return QUANTAPDF_ERROR_FORMAT;
            node->is_open = count.getIntValue() > 0;
        }
        quantapdf_status status = quantapdf_qpdf_decode_outline_destination(
            pdf, current, node);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFObjectHandle child_first = current.getKey("/First");
        QPDFObjectHandle child_last = current.getKey("/Last");
        if (!child_first.isNull())
            node->first_child_index = nodes->size();
        status = quantapdf_qpdf_walk_outline_siblings(
            pdf, child_first, child_last, current, current_index,
            depth + 1u, nodes, seen);
        if (status != QUANTAPDF_OK)
            return status;

        QPDFObjectHandle next = current.getKey("/Next");
        if (next.isNull()) {
            if (!quantapdf_qpdf_same_object(current, last))
                return QUANTAPDF_ERROR_FORMAT;
            return QUANTAPDF_OK;
        }
        previous = current;
        previous_index = current_index;
        current = next;
    }
    return QUANTAPDF_ERROR_FORMAT;
}

extern "C" quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document)
{
    if (out_document == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = nullptr;

    if (data == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto document = std::make_unique<quantapdf_qpdf_document>();
        document->pdf = QPDF::create();
        document->pdf->processMemoryFile(
            "quantapdf-memory",
            reinterpret_cast<char const *>(data),
            size,
            password_utf8);
        *out_document = document.release();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count)
{
    if (out_page_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page_count = 0;

    if (document == nullptr || document->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        size_t const page_count = document->pdf->getAllPages().size();
        if (page_count > static_cast<size_t>(INT_MAX))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *out_page_count = static_cast<int>(page_count);
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" void quantapdf_qpdf_close(
    quantapdf_qpdf_document *document)
{
    delete document;
}

extern "C" quantapdf_status quantapdf_qpdf_page_user_unit(
    quantapdf_qpdf_document *document,
    int page_index,
    double *out_user_unit)
{
    if (out_user_unit == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_user_unit = 0.0;
    if (document == nullptr || document->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const& pages = document->pdf->getAllPages();
        if (static_cast<size_t>(page_index) >= pages.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle value =
            pages[static_cast<size_t>(page_index)].getKey("/UserUnit");
        if (!value.isNull() && !value.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const user_unit = value.isNull() ? 1.0 : value.getNumericValue();
        if (!std::isfinite(user_unit) || user_unit <= 0.0 ||
            user_unit > 75000.0)
            return QUANTAPDF_ERROR_FORMAT;
        *out_user_unit = user_unit;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_page_box_bounds(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    if (document == nullptr || document->pdf == nullptr ||
        page_index < 0 || out_bounds == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (box != QUANTAPDF_PAGE_BOX_MEDIA && box != QUANTAPDF_PAGE_BOX_CROP)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const& pages = document->pdf->getAllPages();
        if (static_cast<size_t>(page_index) >= pages.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle page = pages[static_cast<size_t>(page_index)];
        QPDFObjectHandle media = quantapdf_qpdf_inherited_value(
            page, "/MediaBox");
        QPDFObjectHandle crop = quantapdf_qpdf_inherited_value(
            page, "/CropBox");
        if (crop.isNull())
            crop = media;
        QPDFObjectHandle selected = box == QUANTAPDF_PAGE_BOX_MEDIA ?
            media : crop;
        if (!crop.isArray() || crop.getArrayNItems() != 4 ||
            !selected.isArray() || selected.getArrayNItems() != 4)
            return QUANTAPDF_ERROR_FORMAT;

        double crop_values[4];
        double box_values[4];
        for (int index = 0; index < 4; ++index) {
            QPDFObjectHandle crop_item = crop.getArrayItem(index);
            QPDFObjectHandle box_item = selected.getArrayItem(index);
            if (!crop_item.isNumber() || !box_item.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            crop_values[index] = crop_item.getNumericValue();
            box_values[index] = box_item.getNumericValue();
            if (!std::isfinite(crop_values[index]) ||
                !std::isfinite(box_values[index]))
                return QUANTAPDF_ERROR_FORMAT;
        }
        if (crop_values[2] <= crop_values[0] ||
            crop_values[3] <= crop_values[1] ||
            box_values[2] <= box_values[0] ||
            box_values[3] <= box_values[1])
            return QUANTAPDF_ERROR_FORMAT;

        out_bounds->x0 = static_cast<float>(box_values[0] - crop_values[0]);
        out_bounds->y0 = static_cast<float>(crop_values[3] - box_values[3]);
        out_bounds->x1 = static_cast<float>(box_values[2] - crop_values[0]);
        out_bounds->y1 = static_cast<float>(crop_values[3] - box_values[1]);
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_extract_annotations(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_annotation_page **out_annotations)
{
    if (out_annotations == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_annotations = nullptr;
    if (document == nullptr || document->pdf == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const& pages = document->pdf->getAllPages();
        if (static_cast<size_t>(page_index) >= pages.size())
            return QUANTAPDF_ERROR_ARGUMENT;
        QPDFObjectHandle page = pages[static_cast<size_t>(page_index)];
        QPDFObjectHandle media = quantapdf_qpdf_inherited_value(
            page, "/MediaBox");
        QPDFObjectHandle crop = quantapdf_qpdf_inherited_value(
            page, "/CropBox");
        if (crop.isNull())
            crop = media;
        if (!crop.isArray() || crop.getArrayNItems() != 4)
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle crop_left_object = crop.getArrayItem(0);
        QPDFObjectHandle crop_top_object = crop.getArrayItem(3);
        if (!crop_left_object.isNumber() || !crop_top_object.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const crop_left = crop_left_object.getNumericValue();
        double const crop_top = crop_top_object.getNumericValue();

        double user_unit = 1.0;
        QPDFObjectHandle user_unit_object = page.getKey("/UserUnit");
        if (!user_unit_object.isNull()) {
            if (!user_unit_object.isNumber())
                return QUANTAPDF_ERROR_FORMAT;
            user_unit = user_unit_object.getNumericValue();
            if (!std::isfinite(user_unit) || user_unit <= 0.0 ||
                user_unit > 75000.0)
                return QUANTAPDF_ERROR_FORMAT;
        }

        std::vector<quantapdf_qpdf_captured_annotation> captured;
        QPDFObjectHandle annots = page.getKey("/Annots");
        if (annots.isArray()) {
            int const count = annots.getArrayNItems();
            for (int index = 0; index < count; ++index) {
                QPDFObjectHandle annotation = annots.getArrayItem(index);
                if (!annotation.isDictionary())
                    continue;

                QPDFObjectHandle subtype = annotation.getKey("/Subtype");
                std::string const subtype_name = subtype.isName() ?
                    subtype.getName() : std::string();
                if (subtype_name == "/Link" || subtype_name == "/Popup" ||
                    subtype_name == "/Widget")
                    continue;

                QPDFObjectHandle rect = annotation.getKey("/Rect");
                if (!rect.isArray() || rect.getArrayNItems() != 4)
                    return QUANTAPDF_ERROR_FORMAT;
                double values[4];
                for (int value_index = 0; value_index < 4; ++value_index) {
                    QPDFObjectHandle value = rect.getArrayItem(value_index);
                    if (!value.isNumber())
                        return QUANTAPDF_ERROR_FORMAT;
                    values[value_index] = value.getNumericValue();
                    if (!std::isfinite(values[value_index]))
                        return QUANTAPDF_ERROR_FORMAT;
                }
                double const left = values[0] < values[2] ? values[0] : values[2];
                double const right = values[0] < values[2] ? values[2] : values[0];
                double const bottom = values[1] < values[3] ? values[1] : values[3];
                double const top = values[1] < values[3] ? values[3] : values[1];
                if (right <= left || top <= bottom)
                    return QUANTAPDF_ERROR_FORMAT;

                quantapdf_qpdf_captured_annotation item = {};
                item.type = quantapdf_qpdf_annotation_type(subtype_name);
                item.bounds.x0 = static_cast<float>((left - crop_left) * user_unit);
                item.bounds.y0 = static_cast<float>((crop_top - top) * user_unit);
                item.bounds.x1 = static_cast<float>((right - crop_left) * user_unit);
                item.bounds.y1 = static_cast<float>((crop_top - bottom) * user_unit);

                QPDFObjectHandle flags = annotation.getKey("/F");
                if (!flags.isNull()) {
                    if (!flags.isInteger())
                        return QUANTAPDF_ERROR_FORMAT;
                    long long const flag_value = flags.getIntValue();
                    if (flag_value < 0 || flag_value > UINT32_MAX)
                        return QUANTAPDF_ERROR_FORMAT;
                    item.flags = static_cast<uint32_t>(flag_value);
                }

                QPDFObjectHandle contents = annotation.getKey("/Contents");
                if (!contents.isNull()) {
                    if (!contents.isString())
                        return QUANTAPDF_ERROR_FORMAT;
                    item.contents = contents.getUTF8Value();
                    item.has_contents = true;
                }
                captured.push_back(std::move(item));
            }
        }

        auto *snapshot = static_cast<quantapdf_annotation_page *>(
            std::calloc(1u, sizeof(quantapdf_annotation_page)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!captured.empty()) {
            snapshot->items = static_cast<quantapdf_annotation_internal *>(
                std::calloc(
                    captured.size(), sizeof(quantapdf_annotation_internal)));
            if (snapshot->items == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_size = 0;
        for (auto const& item : captured) {
            if (item.has_contents) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        item.contents.size() - 1u) {
                    std::free(snapshot->items);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += item.contents.size() + 1u;
            }
        }
        if (string_size != 0) {
            snapshot->strings = static_cast<char *>(std::malloc(string_size));
            if (snapshot->strings == nullptr) {
                std::free(snapshot->items);
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_position = 0;
        for (size_t index = 0; index < captured.size(); ++index) {
            auto const& source = captured[index];
            auto *target = &snapshot->items[index];
            target->type = source.type;
            target->bounds = source.bounds;
            target->flags = source.flags;
            if (source.has_contents) {
                target->contents_offset = string_position;
                target->contents_size = source.contents.size();
                target->has_contents = 1;
                std::memcpy(
                    snapshot->strings + string_position,
                    source.contents.data(),
                    source.contents.size());
                string_position += source.contents.size();
                snapshot->strings[string_position++] = '\0';
            }
        }
        snapshot->count = captured.size();
        snapshot->string_size = string_position;
        *out_annotations = snapshot;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_metadata(
    quantapdf_qpdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != nullptr)
        *out_utf8 = nullptr;
    if (out_size != nullptr)
        *out_size = 0;
    if (document == nullptr || document->pdf == nullptr ||
        out_utf8 == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    char const *key;
    switch (field) {
    case QUANTAPDF_METADATA_TITLE: key = "/Title"; break;
    case QUANTAPDF_METADATA_AUTHOR: key = "/Author"; break;
    case QUANTAPDF_METADATA_SUBJECT: key = "/Subject"; break;
    case QUANTAPDF_METADATA_KEYWORDS: key = "/Keywords"; break;
    case QUANTAPDF_METADATA_CREATOR: key = "/Creator"; break;
    case QUANTAPDF_METADATA_PRODUCER: key = "/Producer"; break;
    case QUANTAPDF_METADATA_CREATION_DATE: key = "/CreationDate"; break;
    case QUANTAPDF_METADATA_MODIFICATION_DATE: key = "/ModDate"; break;
    default: return QUANTAPDF_ERROR_ARGUMENT;
    }

    try {
        QPDFObjectHandle info = document->pdf->getTrailer().getKey("/Info");
        if (info.isNull())
            return QUANTAPDF_OK;
        if (!info.isDictionary())
            return QUANTAPDF_ERROR_FORMAT;
        QPDFObjectHandle value = info.getKey(key);
        if (value.isNull())
            return QUANTAPDF_OK;
        if (!value.isString())
            return QUANTAPDF_ERROR_FORMAT;
        std::string const text = value.getUTF8Value();
        auto *copy = static_cast<char *>(std::malloc(text.size() + 1u));
        if (copy == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!text.empty())
            std::memcpy(copy, text.data(), text.size());
        copy[text.size()] = '\0';
        *out_utf8 = copy;
        *out_size = text.size();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_outline(
    quantapdf_qpdf_document *document,
    quantapdf_outline **out_outline)
{
    if (out_outline == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_outline = nullptr;
    if (document == nullptr || document->pdf == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        std::vector<quantapdf_qpdf_captured_outline_node> nodes;
        std::set<QPDFObjGen> seen;
        QPDFObjectHandle root = document->pdf->getRoot();
        QPDFObjectHandle outlines = root.getKey("/Outlines");
        if (!outlines.isNull()) {
            if (!outlines.isDictionary() || !outlines.isIndirect())
                return QUANTAPDF_ERROR_FORMAT;
            quantapdf_status const status =
                quantapdf_qpdf_walk_outline_siblings(
                    *document->pdf,
                    outlines.getKey("/First"),
                    outlines.getKey("/Last"),
                    outlines,
                    SIZE_MAX,
                    1u,
                    &nodes,
                    &seen);
            if (status != QUANTAPDF_OK)
                return status;
        }

        auto *snapshot = static_cast<quantapdf_outline *>(
            std::calloc(1u, sizeof(quantapdf_outline)));
        if (snapshot == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        if (!nodes.empty()) {
            snapshot->nodes = static_cast<quantapdf_outline_node_internal *>(
                std::calloc(nodes.size(), sizeof(quantapdf_outline_node_internal)));
            if (snapshot->nodes == nullptr) {
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t string_size = 0;
        for (auto const& node : nodes) {
            if (node.has_title) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        node.title.size() - 1u) {
                    std::free(snapshot->nodes);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += node.title.size() + 1u;
            }
            if (node.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_URI) {
                if (string_size > std::numeric_limits<size_t>::max() -
                        node.uri.size() - 1u) {
                    std::free(snapshot->nodes);
                    std::free(snapshot);
                    return QUANTAPDF_ERROR_NOMEM;
                }
                string_size += node.uri.size() + 1u;
            }
        }
        if (string_size != 0) {
            snapshot->strings = static_cast<char *>(std::malloc(string_size));
            if (snapshot->strings == nullptr) {
                std::free(snapshot->nodes);
                std::free(snapshot);
                return QUANTAPDF_ERROR_NOMEM;
            }
        }

        size_t position = 0;
        for (size_t index = 0; index < nodes.size(); ++index) {
            auto const& source = nodes[index];
            auto *target = &snapshot->nodes[index];
            target->parent_index = source.parent_index;
            target->first_child_index = source.first_child_index;
            target->next_sibling_index = source.next_sibling_index;
            target->destination_kind = source.destination_kind;
            target->target_page = source.target_page;
            target->target = source.target;
            target->is_open = source.is_open ? 1 : 0;
            if (source.has_title) {
                target->title_offset = position;
                target->title_size = source.title.size();
                target->has_title = 1;
                std::memcpy(
                    snapshot->strings + position,
                    source.title.data(),
                    source.title.size());
                position += source.title.size();
                snapshot->strings[position++] = '\0';
            }
            if (source.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_URI) {
                target->uri_offset = position;
                target->uri_size = source.uri.size();
                std::memcpy(
                    snapshot->strings + position,
                    source.uri.data(),
                    source.uri.size());
                position += source.uri.size();
                snapshot->strings[position++] = '\0';
            }
        }
        snapshot->count = nodes.size();
        snapshot->string_size = position;
        *out_outline = snapshot;
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_rewrite_memory(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0;
    if (data == nullptr || size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->processMemoryFile(
            "quantapdf-rewrite",
            reinterpret_cast<char const *>(data),
            size);

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (buffer == nullptr || buffer->getSize() == 0 ||
            buffer->getBuffer() == nullptr)
            return QUANTAPDF_ERROR_BACKEND;

        auto *copy = static_cast<unsigned char *>(
            std::malloc(buffer->getSize()));
        if (copy == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        std::memcpy(copy, buffer->getBuffer(), buffer->getSize());
        *out_data = copy;
        *out_size = buffer->getSize();
        return QUANTAPDF_OK;
    } catch (QPDFExc const& error) {
        return quantapdf_status_from_qpdf(error);
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
