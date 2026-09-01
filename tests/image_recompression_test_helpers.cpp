#include "image_recompression_test_helpers.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <set>
#include <string>

namespace {

QPDFObjectHandle make_image(
    QPDF& pdf,
    int marker,
    int components,
    std::string const& samples)
{
    QPDFObjectHandle image = pdf.newStream(samples);
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(2));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(2));
    dict.replaceKey(
        "/ColorSpace",
        QPDFObjectHandle::newName(
            components == 1 ? "/DeviceGray" : "/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    dict.replaceKey(
        "/QuantaPDFImageId", QPDFObjectHandle::newInteger(marker));
    return image;
}

QPDFObjectHandle make_form(
    QPDF& pdf,
    QPDFObjectHandle image,
    char const *image_name)
{
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey(image_name, image);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    QPDFObjectHandle form = pdf.newStream(
        std::string("q 2 0 0 2 0 0 cm ") + image_name + " Do Q\n");
    QPDFObjectHandle dict = form.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", QPDFObjectHandle::parse("[0 0 2 2]"));
    dict.replaceKey("/Resources", resources);
    return form;
}

QPDFObjectHandle make_appearance(
    QPDF& pdf,
    int marker,
    int components,
    std::string const& samples)
{
    QPDFObjectHandle image = make_image(pdf, marker, components, samples);
    QPDFObjectHandle nested = make_form(pdf, image, "/AppearanceImage");
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/NestedForm", nested);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    QPDFObjectHandle appearance = pdf.newStream(
        "q 10 0 0 10 0 0 cm /NestedForm Do Q\n");
    QPDFObjectHandle dict = appearance.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dict.replaceKey("/BBox", QPDFObjectHandle::parse("[0 0 20 20]"));
    dict.replaceKey("/Resources", resources);
    return appearance;
}

QPDFObjectHandle state_dictionary(QPDFObjectHandle appearance)
{
    QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
    states.replaceKey("/On", appearance);
    return states;
}

void write_fixture(QPDF& pdf, char const *output_path)
{
    QPDFWriter writer(pdf, output_path);
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.write();
}

bool is_dct_image(QPDFObjectHandle image)
{
    if (!image.isStream() || !image.isIndirect()) {
        return false;
    }
    QPDFObjectHandle dict = image.getDict();
    if (!dict.getKey("/Filter").isNameAndEquals("/DCTDecode")) {
        return false;
    }
    QPDFObjectHandle color_space = dict.getKey("/ColorSpace");
    QPDFObjectHandle decode_parms = dict.getKey("/DecodeParms");
    if (color_space.isNameAndEquals("/DeviceRGB")) {
        return decode_parms.isDictionary() &&
            decode_parms.getKey("/ColorTransform").isInteger() &&
            decode_parms.getKey("/ColorTransform").getIntValue() == 0;
    }
    return color_space.isNameAndEquals("/DeviceGray") &&
        decode_parms.isNull();
}

} // namespace

extern "C" int image_recompression_create_positive_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        auto const& pages = pdf->getAllPages();
        if (pages.size() < 2u) {
            return 0;
        }
        QPDFObjectHandle page0 = pages[0];
        QPDFObjectHandle page1 = pages[1];

        std::string const rgb_samples(
            "\x00\x20\x40\x60\x80\xa0\xc0\xe0\xff\x30\x90\xf0", 12);
        std::string const gray_samples("\x00\x55\xaa\xff", 4);
        QPDFObjectHandle shared_rgb = make_image(*pdf, 1, 3, rgb_samples);
        QPDFObjectHandle gray = make_image(*pdf, 2, 1, gray_samples);
        QPDFObjectHandle nested_page_form =
            make_form(*pdf, shared_rgb, "/NestedSharedRGB");

        QPDFObjectHandle page0_xobjects = QPDFObjectHandle::newDictionary();
        page0_xobjects.replaceKey("/SharedRGB", shared_rgb);
        page0_xobjects.replaceKey("/Gray", gray);
        page0_xobjects.replaceKey("/NestedPageForm", nested_page_form);
        QPDFObjectHandle page0_resources = QPDFObjectHandle::newDictionary();
        page0_resources.replaceKey("/XObject", page0_xobjects);
        page0.replaceKey("/Resources", page0_resources);
        page0.replaceKey(
            "/Contents",
            pdf->newStream(
                "q 20 0 0 20 10 10 cm /SharedRGB Do Q\n"
                "q 20 0 0 20 40 10 cm /Gray Do Q\n"
                "q 20 0 0 20 70 10 cm /NestedPageForm Do Q\n"));

        QPDFObjectHandle page1_xobjects = QPDFObjectHandle::newDictionary();
        page1_xobjects.replaceKey("/SharedRGB", shared_rgb);
        QPDFObjectHandle page1_resources = QPDFObjectHandle::newDictionary();
        page1_resources.replaceKey("/XObject", page1_xobjects);
        page1.replaceKey("/Resources", page1_resources);
        page1.replaceKey(
            "/Contents",
            pdf->newStream("q 20 0 0 20 10 10 cm /SharedRGB Do Q\n"));

        QPDFObjectHandle direct_ap = QPDFObjectHandle::newDictionary();
        direct_ap.replaceKey(
            "/N", make_appearance(*pdf, 3, 3, rgb_samples));
        direct_ap.replaceKey(
            "/R", make_appearance(*pdf, 4, 1, gray_samples));
        direct_ap.replaceKey(
            "/D", make_appearance(*pdf, 5, 3, rgb_samples));
        QPDFObjectHandle direct_annotation = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Annot")},
                {"/Subtype", QPDFObjectHandle::newName("/Square")},
                {"/Rect", QPDFObjectHandle::parse("[10 40 30 60]")},
                {"/AP", direct_ap}}));

        QPDFObjectHandle state_ap = QPDFObjectHandle::newDictionary();
        state_ap.replaceKey(
            "/N",
            state_dictionary(make_appearance(*pdf, 6, 1, gray_samples)));
        state_ap.replaceKey(
            "/R",
            state_dictionary(make_appearance(*pdf, 7, 3, rgb_samples)));
        state_ap.replaceKey(
            "/D",
            state_dictionary(make_appearance(*pdf, 8, 1, gray_samples)));
        QPDFObjectHandle widget = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Annot")},
                {"/Subtype", QPDFObjectHandle::newName("/Widget")},
                {"/Rect", QPDFObjectHandle::parse("[40 40 60 60]")},
                {"/AP", state_ap}}));

        QPDFObjectHandle annotations = QPDFObjectHandle::newArray();
        annotations.appendItem(direct_annotation);
        annotations.appendItem(widget);
        page0.replaceKey("/Annots", annotations);

        write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int image_recompression_check_positive_output(
    const unsigned char *data,
    size_t size,
    size_t expected_image_count)
{
    try {
        auto pdf = QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "image-recompression-positive-output",
            reinterpret_cast<char const *>(data),
            size);
        auto const& pages = pdf->getAllPages();
        if (pages.size() < 2u) {
            return 0;
        }
        QPDFObjectHandle shared0 = pages[0]
            .getKey("/Resources")
            .getKey("/XObject")
            .getKey("/SharedRGB");
        QPDFObjectHandle shared1 = pages[1]
            .getKey("/Resources")
            .getKey("/XObject")
            .getKey("/SharedRGB");
        if (!shared0.isSameObjectAs(shared1)) {
            return 0;
        }

        std::set<QPDFObjGen> seen;
        for (QPDFObjectHandle object: pdf->getAllObjects()) {
            if (!object.isStream()) {
                continue;
            }
            QPDFObjectHandle marker =
                object.getDict().getKey("/QuantaPDFImageId");
            if (marker.isNull()) {
                continue;
            }
            if (!marker.isInteger() || !is_dct_image(object)) {
                return 0;
            }
            seen.insert(object.getObjGen());
        }
        return seen.size() == expected_image_count;
    } catch (...) {
        return 0;
    }
}
