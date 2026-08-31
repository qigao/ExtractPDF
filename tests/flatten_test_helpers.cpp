#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

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
