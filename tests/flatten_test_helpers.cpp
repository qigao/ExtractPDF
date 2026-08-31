#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstring>
#include <memory>
#include <string>

namespace {

std::shared_ptr<QPDF> open_pdf(
    unsigned char const *data,
    size_t size,
    char const *description)
{
    auto pdf = QPDF::create();
    pdf->setAttemptRecovery(false);
    pdf->processMemoryFile(
        description, reinterpret_cast<char const *>(data), size);
    return pdf;
}

bool has_marker(
    QPDFObjectHandle const& xobjects,
    char const *alias,
    char const *marker)
{
    QPDFObjectHandle appearance = xobjects.getKey(alias);
    return appearance.isStream() && appearance.isIndirect() &&
        appearance.getDict().getKey("/StateMarker").isNameAndEquals(marker);
}

std::string page_content(QPDFObjectHandle const& page)
{
    std::string result;
    QPDFObjectHandle contents = page.getKey("/Contents");
    if (contents.isStream()) {
        auto data = contents.getStreamData();
        result.append(
            reinterpret_cast<char const *>(data->getBuffer()),
            data->getSize());
    } else if (contents.isArray()) {
        int const count = contents.getArrayNItems();
        for (int index = 0; index < count; ++index) {
            QPDFObjectHandle stream = contents.getArrayItem(index);
            if (!stream.isStream())
                return std::string();
            auto data = stream.getStreamData();
            result.append(
                reinterpret_cast<char const *>(data->getBuffer()),
                data->getSize());
        }
    }
    return result;
}

} // namespace

extern "C" int quantapdf_flatten_raw_check_combined(
    unsigned char const *data,
    size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "flatten-combined-output");
        if (!pdf->getRoot().getKey("/AcroForm").isNull())
            return 0;
        auto const& pages = pdf->getAllPages();
        if (pages.size() != 1u || !pages[0].getKey("/Annots").isNull())
            return 0;
        QPDFObjectHandle xobjects =
            pages[0].getKey("/Resources").getKey("/XObject");
        if (!xobjects.isDictionary() ||
            !has_marker(xobjects, "/EPB0", "/Widget") ||
            !has_marker(xobjects, "/EPB1", "/Square") ||
            !xobjects.getKey("/EPB2").isNull())
            return 0;
        std::string const content = page_content(pages[0]);
        size_t const widget = content.find("/EPB0 Do");
        size_t const square = content.find("/EPB1 Do");
        return widget != std::string::npos && square != std::string::npos &&
            widget < square;
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_flatten_raw_check_form_closure(
    unsigned char const *data,
    size_t size,
    int ancestor_survives)
{
    try {
        auto pdf = open_pdf(data, size, "flatten-form-output");
        QPDFObjectHandle root = pdf->getRoot();
        QPDFObjectHandle acroform = root.getKey("/AcroForm");
        QPDFObjectHandle fields = acroform.getKey("/Fields");
        QPDFObjectHandle audit_root = root.getKey("/AuditRootKids");
        QPDFObjectHandle audit_mid = root.getKey("/AuditMidKids");
        QPDFObjectHandle keep = acroform.getKey("/KeepRef");
        if (!fields.isArray() || fields.getArrayNItems() != 1 ||
            !audit_root.isArray() || !audit_mid.isArray() ||
            !keep.isDictionary())
            return 0;
        QPDFObjectHandle root_field = fields.getArrayItem(0);
        QPDFObjectHandle root_kids = root_field.getKey("/Kids");
        if (!root_kids.isArray() || root_kids.getArrayNItems() != 1)
            return 0;

        if (ancestor_survives) {
            if (!root_kids.isSameObjectAs(audit_root))
                return 0;
            QPDFObjectHandle mid = root_kids.getArrayItem(0);
            QPDFObjectHandle mid_kids = mid.getKey("/Kids");
            if (!mid_kids.isArray() || mid_kids.getArrayNItems() != 1 ||
                mid_kids.isSameObjectAs(audit_mid) ||
                !mid_kids.getArrayItem(0).isSameObjectAs(keep) ||
                audit_mid.getArrayNItems() != 2)
                return 0;
            QPDFObjectHandle removed = audit_mid.getArrayItem(0);
            if (!removed.getKey("/Kids").isNull())
                return 0;
        } else {
            if (root_kids.isSameObjectAs(audit_root) ||
                !root_kids.getArrayItem(0).isSameObjectAs(keep) ||
                audit_root.getArrayNItems() != 2 ||
                audit_mid.getArrayNItems() != 1)
                return 0;
            QPDFObjectHandle removed = audit_mid.getArrayItem(0);
            if (!removed.getKey("/Kids").isNull())
                return 0;
        }
        auto const& pages = pdf->getAllPages();
        return pages.size() == 1u && pages[0].getKey("/Annots").isNull();
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_flatten_raw_check_calculation_order(
    unsigned char const *data,
    size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "flatten-calculation-order-output");
        QPDFObjectHandle root = pdf->getRoot();
        QPDFObjectHandle acroform = root.getKey("/AcroForm");
        QPDFObjectHandle fields = acroform.getKey("/Fields");
        QPDFObjectHandle audit_fields = root.getKey("/AuditFields");
        QPDFObjectHandle order = acroform.getKey("/CO");
        QPDFObjectHandle audit_order = root.getKey("/AuditCO");
        QPDFObjectHandle keep_a = acroform.getKey("/KeepA");
        QPDFObjectHandle keep_b = acroform.getKey("/KeepB");
        if (!fields.isArray() || fields.getArrayNItems() != 2 ||
            !audit_fields.isArray() || audit_fields.getArrayNItems() != 3 ||
            fields.isSameObjectAs(audit_fields) ||
            !fields.getArrayItem(0).isSameObjectAs(keep_a) ||
            !fields.getArrayItem(1).isSameObjectAs(keep_b) ||
            !order.isArray() || order.getArrayNItems() != 2 ||
            !audit_order.isArray() || audit_order.getArrayNItems() != 3 ||
            order.isSameObjectAs(audit_order) ||
            !order.getArrayItem(0).isSameObjectAs(keep_b) ||
            !order.getArrayItem(1).isSameObjectAs(keep_a))
            return 0;
        auto const& pages = pdf->getAllPages();
        return pages.size() == 1u && pages[0].getKey("/Annots").isNull();
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_flatten_raw_check_contents(
    unsigned char const *data,
    size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "flatten-contents-output");
        auto const& pages = pdf->getAllPages();
        if (pages.size() != 4u)
            return 0;

        QPDFObjectHandle page0_xobjects =
            pages[0].getKey("/Resources").getKey("/XObject");
        if (!page0_xobjects.isDictionary() ||
            page0_xobjects.getKey("/EPB0").isNull() ||
            page0_xobjects.getKey("/EPB1").isNull() ||
            !pages[0].getKey("/Annots").isNull())
            return 0;

        QPDFObjectHandle page1_xobjects =
            pages[1].getKey("/Resources").getKey("/XObject");
        if (!page1_xobjects.isDictionary() ||
            !page1_xobjects.getKey("/EPB0").isSameObjectAs(
                page1_xobjects.getKey("/Keep")) ||
            page1_xobjects.getKey("/EPB1").isNull() ||
            !page1_xobjects.getKey("/EPB2").isNull())
            return 0;

        QPDFObjectHandle inherited =
            pages[2].getKey("/Parent").getKey("/Resources");
        QPDFObjectHandle local = pages[2].getKey("/Resources");
        if (!local.isDictionary() || !inherited.isDictionary() ||
            local.isSameObjectAs(inherited) ||
            local.getKey("/ProcSet").isNull())
            return 0;

        QPDFObjectHandle annotations = pages[2].getKey("/Annots");
        if (!annotations.isArray() || annotations.getArrayNItems() != 1 ||
            !annotations.getArrayItem(0).getKey("/Subtype").isNameAndEquals(
                "/Link"))
            return 0;

        return pages[3].getKey("/Annots").isNull() &&
            page_content(pages[3]).find("/EPB2 Do") != std::string::npos;
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_flatten_make_variant(
    char const *source_path,
    char const *output_path,
    int variant)
{
    try {
        auto pdf = QPDF::create();
        pdf->setAttemptRecovery(false);
        pdf->processFile(source_path);
        auto const& pages = pdf->getAllPages();
        if (pages.empty())
            return 0;
        QPDFObjectHandle annotations = pages[0].getKey("/Annots");
        if (!annotations.isArray() || annotations.getArrayNItems() == 0)
            return 0;
        QPDFObjectHandle annotation = annotations.getArrayItem(0);
        if (variant == 1) {
            QPDFObjectHandle normal = annotation.getKey("/AP").getKey("/N");
            if (!normal.isStream())
                return 0;
            normal.replaceStreamData(
                "not encoded",
                QPDFObjectHandle::newName("/BogusDecode"),
                QPDFObjectHandle::newNull());
        } else if (variant == 2) {
            annotation.replaceKey("/CA", QPDFObjectHandle::newReal("0.5"));
        } else if (variant == 3) {
            annotation.replaceKey(
                "/CA", QPDFObjectHandle::newString("malformed"));
        } else if (variant == 4) {
            QPDFObjectHandle contents = pages[0].getKey("/Contents");
            if (!contents.isStream())
                return 0;
            contents.replaceStreamData(
                "not encoded",
                QPDFObjectHandle::newName("/BogusDecode"),
                QPDFObjectHandle::newNull());
        } else if (variant == 5) {
            QPDFObjectHandle normal = annotation.getKey("/AP").getKey("/N");
            if (!normal.isStream())
                return 0;
            normal.replaceStreamData(
                std::string("\x78\x9c\x00", 3),
                QPDFObjectHandle::newName("/FlateDecode"),
                QPDFObjectHandle::newNull());
        } else if (variant == 6) {
            QPDFObjectHandle normal = annotation.getKey("/AP").getKey("/N");
            if (!normal.isStream())
                return 0;
            normal.replaceStreamData(
                "BogusOperator\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 7 || variant == 8) {
            QPDFObjectHandle contents = pages[0].getKey("/Contents");
            if (!contents.isStream())
                return 0;
            contents.replaceStreamData(
                variant == 7 ? "Q\n" : "BT\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 9) {
            if (pages.size() < 2u)
                return 0;
            QPDFObjectHandle widgets = pages[1].getKey("/Annots");
            if (!widgets.isArray() || widgets.getArrayNItems() == 0)
                return 0;
            QPDFObjectHandle widget = widgets.getArrayItem(0);
            if (!widget.getKey("/Subtype").isNameAndEquals("/Widget"))
                return 0;
            widget.replaceKey("/P", pages[0]);
        } else if (variant == 10 || variant == 12) {
            QPDFObjectHandle normal = annotation.getKey("/AP").getKey("/N");
            if (!normal.isStream())
                return 0;
            normal.replaceStreamData(
                variant == 10 ? "q\n1 cm\nQ\n" : "(text) Tj\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 11 || variant == 13) {
            QPDFObjectHandle contents = pages[0].getKey("/Contents");
            if (!contents.isStream())
                return 0;
            contents.replaceStreamData(
                variant == 11 ? "(bad) 0 0 1 0 0 cm\n" : "1 1 l\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 14) {
            QPDFObjectHandle contents = pages[0].getKey("/Contents");
            if (!contents.isStream())
                return 0;
            contents.replaceStreamData(
                "BT\n0 0 m\nET\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 15) {
            QPDFObjectHandle normal = annotation.getKey("/AP").getKey("/N");
            if (!normal.isStream())
                return 0;
            std::string content =
                "q\nBI\n/W 1\n/H 1\n/BPC 8\n/CS /RGB\nID\n";
            content.append("\xff\x00\x00", 3);
            content += "\nEI\nQ\n";
            normal.replaceStreamData(
                content,
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else if (variant == 16 || variant == 17) {
            QPDFObjectHandle contents = pages[0].getKey("/Contents");
            if (!contents.isStream())
                return 0;
            contents.replaceStreamData(
                variant == 16 ?
                    "BT\n1 w\nET\n" :
                    "q\n/Span BMC\nQ\nEMC\n",
                QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        } else {
            return 0;
        }
        QPDFWriter writer(*pdf, output_path);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int quantapdf_flatten_raw_check_number_format(
    unsigned char const *data,
    size_t size)
{
    try {
        auto pdf = open_pdf(data, size, "flatten-number-format-output");
        auto const& pages = pdf->getAllPages();
        if (pages.size() != 1u || !pages[0].getKey("/Annots").isNull())
            return 0;
        std::string const content = page_content(pages[0]);
        return content.find("/EPB0 Do") != std::string::npos &&
            content.find("nan") == std::string::npos &&
            content.find("inf") == std::string::npos &&
            content.find("e+") == std::string::npos &&
            content.find("e-") == std::string::npos;
    } catch (...) {
        return 0;
    }
}
