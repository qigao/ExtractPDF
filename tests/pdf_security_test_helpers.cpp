#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

static QPDFObjectHandle pdf_security_action(QPDF& pdf, const char *name)
{
    return pdf.makeIndirectObject(QPDFObjectHandle::newDictionary({
        {"/S", QPDFObjectHandle::newName(name)}}));
}

static void pdf_security_write(
    QPDF& pdf,
    const char *output_path,
    bool preserve_unreferenced = false)
{
    QPDFWriter writer(pdf, output_path);
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.setPreserveUnreferencedObjects(preserve_unreferenced);
    writer.write();
}

extern "C" int pdf_security_create_fixture(
    const char *source_path,
    const char *output_path,
    int kind)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle root = pdf->getRoot();
        auto pages = pdf->getAllPages();
        if (pages.empty())
            return 0;

        if (kind == 1) {
            root.replaceKey("/OpenAction", pdf_security_action(*pdf, "/GoTo"));
        } else if (kind == 2) {
            root.replaceKey(
                "/OpenAction", pdf_security_action(*pdf, "/JavaScript"));
            root.replaceKey(
                "/Names",
                QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary()}}));
        } else if (kind == 3) {
            QPDFObjectHandle owner = QPDFObjectHandle::newDictionary({
                {"/A", pdf_security_action(*pdf, "/Launch")}});
            root.replaceKey("/QuantaPDFActionOwner", owner);
        } else if (kind == 4) {
            QPDFObjectHandle additional = QPDFObjectHandle::newDictionary({
                {"/E", pdf_security_action(*pdf, "/URI")}});
            root.replaceKey(
                "/QuantaPDFAdditionalOwner",
                QPDFObjectHandle::newDictionary({{"/AA", additional}}));
        } else if (kind == 5) {
            QPDFObjectHandle action = pdf_security_action(*pdf, "/GoTo");
            QPDFObjectHandle next = QPDFObjectHandle::newArray();
            next.appendItem(pdf_security_action(*pdf, "/Named"));
            action.replaceKey("/Next", next);
            root.replaceKey("/OpenAction", action);
        } else if (kind == 6) {
            QPDFObjectHandle embedded = pdf->newStream("embedded");
            embedded.getDict().replaceKey(
                "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            QPDFObjectHandle associated = QPDFObjectHandle::newArray();
            associated.appendItem(embedded);
            root.replaceKey("/AF", associated);
            root.replaceKey(
                "/Names",
                QPDFObjectHandle::newDictionary({
                    {"/EmbeddedFiles", QPDFObjectHandle::newDictionary()}}));
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/FileAttachment")}}));
            pages[0].replaceKey("/Annots", annots);
        } else if (kind == 7) {
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            root.replaceKey(
                "/AcroForm",
                QPDFObjectHandle::newDictionary({
                    {"/Fields", fields},
                    {"/XFA", QPDFObjectHandle::newString("xfa")}}));
        } else if (kind == 8) {
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/RichMedia")}}));
            pages[0].replaceKey("/Annots", annots);
        } else if (kind == 9) {
            QPDFObjectHandle garbage = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/A", pdf_security_action(*pdf, "/JavaScript")}}));
            (void)garbage;
        } else if (kind == 10) {
            root.replaceKey(
                "/QuantaPDFMalformedAction",
                QPDFObjectHandle::newDictionary({
                    {"/A", QPDFObjectHandle::newName("/Bad")}}));
        } else if (kind == 11) {
            root.replaceKey(
                "/QuantaPDFMalformedAdditional",
                QPDFObjectHandle::newDictionary({
                    {"/AA", QPDFObjectHandle::newDictionary({
                        {"/E", QPDFObjectHandle::newDictionary()}})}}));
        } else if (kind == 12) {
            root.replaceKey("/Names", QPDFObjectHandle::newName("/Bad"));
        } else if (kind == 13) {
            pages[0].replaceKey(
                "/Annots", QPDFObjectHandle::newName("/Bad"));
        } else if (kind == 14) {
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newName("/Bad"));
            pages[0].replaceKey("/Annots", annots);
        } else {
            return 0;
        }

        pdf_security_write(*pdf, output_path, kind == 9);
        return 1;
    } catch (...) {
        return 0;
    }
}
