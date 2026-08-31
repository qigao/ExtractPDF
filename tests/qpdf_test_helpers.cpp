#include "test_pdf_crop_internal.h"
#include "test_pdf_poster_split_internal.h"
#include "test_pdf_trim_internal.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

std::shared_ptr<QPDF> open_pdf(
    unsigned char const *data, size_t size, char const *description)
{
    auto pdf = QPDF::create();
    pdf->processMemoryFile(
        description, reinterpret_cast<char const *>(data), size);
    return pdf;
}

bool close_number(double left, double right)
{
    return std::fabs(left - right) < 0.001;
}

bool box_matches(QPDFObjectHandle box, float const expected[4])
{
    if (expected == nullptr || !box.isArray() || box.getArrayNItems() != 4)
        return false;
    for (int index = 0; index < 4; ++index) {
        QPDFObjectHandle value = box.getArrayItem(index);
        if (!value.isNumber() ||
            !close_number(value.getNumericValue(), expected[index]))
            return false;
    }
    return true;
}

QPDFObjectHandle inherited(QPDFObjectHandle object, char const *key)
{
    std::set<QPDFObjGen> seen;
    while (object.isDictionary()) {
        if (object.hasKey(key))
            return object.getKey(key);
        if (!object.isIndirect() || !seen.insert(object.getObjGen()).second)
            break;
        object = object.getKey("/Parent");
    }
    return QPDFObjectHandle::newNull();
}

bool semantic_equal(QPDFObjectHandle left, QPDFObjectHandle right)
{
    if (left.isNull() || right.isNull())
        return left.isNull() && right.isNull();
    if (left.isStream() || right.isStream()) {
        if (!left.isStream() || !right.isStream())
            return false;
        auto left_data = left.getStreamData();
        auto right_data = right.getStreamData();
        return left_data->getSize() == right_data->getSize() &&
            std::memcmp(left_data->getBuffer(), right_data->getBuffer(),
                        left_data->getSize()) == 0;
    }
    return left.unparseResolved() == right.unparseResolved();
}

bool compare_selected_keys(
    QPDFObjectHandle left,
    QPDFObjectHandle right,
    std::initializer_list<char const *> keys)
{
    if (!left.isDictionary() || !right.isDictionary())
        return false;
    for (char const *key : keys) {
        if (!semantic_equal(left.getKey(key), right.getKey(key)))
            return false;
    }
    return true;
}

bool compare_page_graph(QPDF& before, QPDF& after)
{
    auto const& left_pages = before.getAllPages();
    auto const& right_pages = after.getAllPages();
    if (left_pages.size() != right_pages.size())
        return false;
    for (size_t page_index = 0; page_index < left_pages.size(); ++page_index) {
        QPDFObjectHandle left = left_pages[page_index];
        QPDFObjectHandle right = right_pages[page_index];
        if (!compare_selected_keys(
                left, right, {"/Contents", "/Resources"}))
            return false;
        QPDFObjectHandle left_annots = left.getKey("/Annots");
        QPDFObjectHandle right_annots = right.getKey("/Annots");
        if (left_annots.isNull() || right_annots.isNull()) {
            if (!left_annots.isNull() || !right_annots.isNull())
                return false;
            continue;
        }
        if (!left_annots.isArray() || !right_annots.isArray() ||
            left_annots.getArrayNItems() != right_annots.getArrayNItems())
            return false;
        for (int index = 0; index < left_annots.getArrayNItems(); ++index) {
            QPDFObjectHandle left_annot = left_annots.getArrayItem(index);
            QPDFObjectHandle right_annot = right_annots.getArrayItem(index);
            if (!compare_selected_keys(left_annot, right_annot,
                    {"/Subtype", "/Rect", "/F", "/Contents", "/A",
                     "/Dest", "/AS", "/T", "/FT", "/V"}))
                return false;
            if (left_annot.getKey("/Subtype").isNameAndEquals("/Widget")) {
                if (!compare_selected_keys(
                        left_annot.getKey("/Parent"),
                        right_annot.getKey("/Parent"),
                        {"/FT", "/T", "/V", "/Ff"}))
                    return false;
            }
        }
    }
    return true;
}

int page_number(QPDF& pdf, QPDFObjectHandle object)
{
    auto const& pages = pdf.getAllPages();
    for (size_t index = 0; index < pages.size(); ++index) {
        if (pages[index].isSameObjectAs(object))
            return static_cast<int>(index);
    }
    return -1;
}

bool destination_matches(
    QPDF& pdf,
    QPDFObjectHandle destination,
    int expected_page,
    float expected_x,
    float expected_y,
    float expected_zoom)
{
    if (!destination.isArray() || destination.getArrayNItems() < 5 ||
        page_number(pdf, destination.getArrayItem(0)) != expected_page ||
        !destination.getArrayItem(1).isNameAndEquals("/XYZ"))
        return false;
    for (int index = 2; index < 5; ++index) {
        if (!destination.getArrayItem(index).isNumber())
            return false;
    }
    return close_number(destination.getArrayItem(2).getNumericValue(), expected_x) &&
        close_number(destination.getArrayItem(3).getNumericValue(), expected_y) &&
        close_number(destination.getArrayItem(4).getNumericValue(), expected_zoom);
}

QPDFObjectHandle destination_value(QPDFObjectHandle value)
{
    return value.isArray() ? value : value.getKey("/D");
}

} // namespace

extern "C" int crop_raw_expect_local_cropbox(
    unsigned char const *data, size_t size, int page_index,
    int expect_present, float const expected[4])
{
    try {
        auto pdf = open_pdf(data, size, "crop-local");
        auto const& pages = pdf->getAllPages();
        if (page_index < 0 || static_cast<size_t>(page_index) >= pages.size())
            return 0;
        bool const present = pages[page_index].hasKey("/CropBox");
        return present == (expect_present != 0) &&
            (!present || box_matches(pages[page_index].getKey("/CropBox"), expected));
    } catch (...) {
        return 0;
    }
}

extern "C" int crop_raw_expect_no_local_default_boxes(
    unsigned char const *data, size_t size, int page_index)
{
    try {
        auto pdf = open_pdf(data, size, "crop-defaults");
        auto const& page = pdf->getAllPages().at(static_cast<size_t>(page_index));
        return !page.hasKey("/BleedBox") && !page.hasKey("/TrimBox") &&
            !page.hasKey("/ArtBox");
    } catch (...) {
        return 0;
    }
}

extern "C" int crop_raw_expect_preserved_graph(
    unsigned char const *before, size_t before_size,
    unsigned char const *after, size_t after_size)
{
    try {
        auto left = open_pdf(before, before_size, "crop-before");
        auto right = open_pdf(after, after_size, "crop-after");
        return compare_page_graph(*left, *right);
    } catch (...) {
        return 0;
    }
}

extern "C" int trim_raw_expect_local_mediabox(
    unsigned char const *data, size_t size, int page_index,
    int expect_present, float const expected[4])
{
    try {
        auto pdf = open_pdf(data, size, "trim-media");
        auto const& page = pdf->getAllPages().at(static_cast<size_t>(page_index));
        bool const present = page.hasKey("/MediaBox");
        return present == (expect_present != 0) &&
            (!present || box_matches(page.getKey("/MediaBox"), expected));
    } catch (...) {
        return 0;
    }
}

extern "C" int trim_raw_expect_preserved_cropbox(
    unsigned char const *before, size_t before_size,
    unsigned char const *after, size_t after_size, int page_index)
{
    try {
        auto left = open_pdf(before, before_size, "trim-crop-before");
        auto right = open_pdf(after, after_size, "trim-crop-after");
        auto const& left_page = left->getAllPages().at(static_cast<size_t>(page_index));
        auto const& right_page = right->getAllPages().at(static_cast<size_t>(page_index));
        return left_page.hasKey("/CropBox") == right_page.hasKey("/CropBox") &&
            semantic_equal(inherited(left_page, "/CropBox"),
                           inherited(right_page, "/CropBox"));
    } catch (...) {
        return 0;
    }
}

extern "C" int trim_raw_expect_preserved_graph(
    unsigned char const *before, size_t before_size,
    unsigned char const *after, size_t after_size)
{
    try {
        auto left = open_pdf(before, before_size, "trim-before");
        auto right = open_pdf(after, after_size, "trim-after");
        return compare_page_graph(*left, *right);
    } catch (...) {
        return 0;
    }
}

extern "C" int trim_raw_expect_production_boxes(
    unsigned char const *data, size_t size, int page_index,
    int expect_bleed, float const bleed[4], int expect_trim,
    float const trim[4], int expect_art, float const art[4])
{
    try {
        auto pdf = open_pdf(data, size, "trim-production");
        auto const& page = pdf->getAllPages().at(static_cast<size_t>(page_index));
        auto matches = [&](char const *key, int expected_present,
                           float const expected[4]) {
            bool const present = page.hasKey(key);
            return present == (expected_present != 0) &&
                (!present || box_matches(page.getKey(key), expected));
        };
        return matches("/BleedBox", expect_bleed, bleed) &&
            matches("/TrimBox", expect_trim, trim) &&
            matches("/ArtBox", expect_art, art);
    } catch (...) {
        return 0;
    }
}

extern "C" int trim_raw_expect_outside_relation(
    unsigned char const *data, size_t size,
    float const expected_media[4], float const expected_crop[4])
{
    try {
        auto pdf = open_pdf(data, size, "trim-outside");
        auto const& page = pdf->getAllPages().at(0);
        QPDFObjectHandle media = inherited(page, "/MediaBox");
        QPDFObjectHandle crop = inherited(page, "/CropBox");
        if (!box_matches(media, expected_media) ||
            !box_matches(crop, expected_crop))
            return 0;
        double mx0 = std::min(media.getArrayItem(0).getNumericValue(),
                              media.getArrayItem(2).getNumericValue());
        double my0 = std::min(media.getArrayItem(1).getNumericValue(),
                              media.getArrayItem(3).getNumericValue());
        double mx1 = std::max(media.getArrayItem(0).getNumericValue(),
                              media.getArrayItem(2).getNumericValue());
        double my1 = std::max(media.getArrayItem(1).getNumericValue(),
                              media.getArrayItem(3).getNumericValue());
        double cx0 = std::min(crop.getArrayItem(0).getNumericValue(),
                              crop.getArrayItem(2).getNumericValue());
        double cy0 = std::min(crop.getArrayItem(1).getNumericValue(),
                              crop.getArrayItem(3).getNumericValue());
        double cx1 = std::max(crop.getArrayItem(0).getNumericValue(),
                              crop.getArrayItem(2).getNumericValue());
        double cy1 = std::max(crop.getArrayItem(1).getNumericValue(),
                              crop.getArrayItem(3).getNumericValue());
        return (cx0 < mx0 || cy0 < my0 || cx1 > mx1 || cy1 > my1) &&
            std::max(mx0, cx0) < std::min(mx1, cx1) &&
            std::max(my0, cy0) < std::min(my1, cy1);
    } catch (...) {
        return 0;
    }
}

extern "C" int poster_raw_check_basic_tiles(
    unsigned char const *data, size_t size, int first_tile_page,
    size_t tile_count, float const (*expected_boxes)[4],
    int expected_rotate, float expected_user_unit)
{
    try {
        auto pdf = open_pdf(data, size, "poster-tiles");
        auto const& pages = pdf->getAllPages();
        QPDFObjectHandle first = pages.at(static_cast<size_t>(first_tile_page));
        for (size_t index = 0; index < tile_count; ++index) {
            QPDFObjectHandle page = pages.at(
                static_cast<size_t>(first_tile_page) + index);
            if (!box_matches(page.getKey("/MediaBox"), expected_boxes[index]) ||
                !box_matches(page.getKey("/CropBox"), expected_boxes[index]) ||
                page.hasKey("/BleedBox") || page.hasKey("/TrimBox") ||
                page.hasKey("/ArtBox") || page.hasKey("/AA") ||
                page.hasKey("/StructParents") ||
                !semantic_equal(first.getKey("/Contents"), page.getKey("/Contents")) ||
                !semantic_equal(first.getKey("/Resources"), page.getKey("/Resources")))
                return 0;
            QPDFObjectHandle rotate = page.getKey("/Rotate");
            if ((expected_rotate == 0 && !rotate.isNull()) ||
                (expected_rotate != 0 &&
                 (!rotate.isInteger() || rotate.getIntValue() != expected_rotate)))
                return 0;
            QPDFObjectHandle unit = page.getKey("/UserUnit");
            if ((close_number(expected_user_unit, 1.0) && !unit.isNull()) ||
                (!close_number(expected_user_unit, 1.0) &&
                 (!unit.isNumber() ||
                  !close_number(unit.getNumericValue(), expected_user_unit))))
                return 0;
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int poster_raw_check_interactive(
    unsigned char const *data, size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "poster-interactive");
        QPDFObjectHandle fields =
            pdf->getRoot().getKey("/AcroForm").getKey("/Fields");
        if (!fields.isArray() || fields.getArrayNItems() != 1)
            return 0;
        QPDFObjectHandle parent = fields.getArrayItem(0);
        QPDFObjectHandle kids = parent.getKey("/Kids");
        if (!parent.getKey("/T").isString() ||
            parent.getKey("/T").getUTF8Value() != "poster" ||
            !kids.isArray() || kids.getArrayNItems() != 1)
            return 0;
        QPDFObjectHandle widget = kids.getArrayItem(0);
        if (!widget.getKey("/Subtype").isNameAndEquals("/Widget") ||
            widget.getKey("/T").getUTF8Value() != "text" ||
            widget.getKey("/V").getUTF8Value() != "POSTER-VALUE" ||
            !widget.getKey("/Parent").isSameObjectAs(parent))
            return 0;
        size_t count = 0;
        int widget_page = -1;
        auto const& pages = pdf->getAllPages();
        for (size_t page_index = 0; page_index < pages.size(); ++page_index) {
            QPDFObjectHandle annots = pages[page_index].getKey("/Annots");
            if (!annots.isArray())
                continue;
            for (int index = 0; index < annots.getArrayNItems(); ++index) {
                QPDFObjectHandle candidate = annots.getArrayItem(index);
                if (candidate.getKey("/Subtype").isNameAndEquals("/Widget")) {
                    ++count;
                    if (candidate.isSameObjectAs(widget))
                        widget_page = static_cast<int>(page_index);
                }
            }
        }
        return count == 1 && widget_page == 3 &&
            widget.getKey("/P").isSameObjectAs(pages[3]);
    } catch (...) {
        return 0;
    }
}

extern "C" int poster_raw_check_navigation(
    unsigned char const *data, size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "poster-navigation");
        auto const& pages = pdf->getAllPages();
        if (pages.size() != 5)
            return 0;
        QPDFObjectHandle annots = pages[4].getKey("/Annots");
        if (!annots.isArray() || annots.getArrayNItems() != 5)
            return 0;
        if (!destination_matches(*pdf, annots.getArrayItem(0).getKey("/Dest"),
                0, 50, 225, 1) ||
            !destination_matches(*pdf,
                annots.getArrayItem(1).getKey("/A").getKey("/D"),
                1, 250, 225, 1) ||
            annots.getArrayItem(2).getKey("/Dest").getUTF8Value() != "named.one" ||
            !annots.getArrayItem(3).getKey("/Dest").isNameAndEquals("/legacy.one") ||
            !destination_matches(*pdf, annots.getArrayItem(4).getKey("/Dest"),
                1, 200, 225, 1))
            return 0;
        QPDFObjectHandle first = pdf->getRoot().getKey("/Outlines").getKey("/First");
        QPDFObjectHandle second = first.getKey("/Next");
        if (!destination_matches(*pdf, first.getKey("/Dest"), 2, 50, 75, 1) ||
            !destination_matches(*pdf, second.getKey("/A").getKey("/D"),
                3, 250, 75, 1))
            return 0;
        QPDFObjectHandle names = pdf->getRoot().getKey("/Names")
            .getKey("/Dests").getKey("/Names");
        if (!names.isArray() || names.getArrayNItems() != 4 ||
            !destination_matches(*pdf, destination_value(names.getArrayItem(1)),
                3, 250, 75, 1) ||
            !destination_matches(*pdf, destination_value(names.getArrayItem(3)),
                2, 50, 75, 1))
            return 0;
        QPDFObjectHandle legacy = pdf->getRoot().getKey("/Dests");
        return destination_matches(*pdf,
                   destination_value(legacy.getKey("/legacy.one")),
                   0, 50, 225, 1) &&
            destination_matches(*pdf,
                destination_value(legacy.getKey("/legacy.dict")),
                1, 250, 225, 1);
    } catch (...) {
        return 0;
    }
}

extern "C" int poster_create_catalog_signature_fixture(
    char const *source_path, char const *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Sig")}}));
        QPDFObjectHandle permissions = QPDFObjectHandle::newDictionary();
        permissions.replaceKey("/DocMDP", signature);
        pdf->getRoot().replaceKey("/Perms", permissions);
        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}
