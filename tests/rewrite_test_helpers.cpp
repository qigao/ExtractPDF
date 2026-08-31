#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <string>

extern "C" int rewrite_create_gc_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);

        QPDFObjectHandle garbage = pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /QuantaPDFGarbage true >>"));
        QPDFObjectHandle garbage_stream = pdf->newStream("dead-stream");
        garbage_stream.getDict().replaceKey(
            "/QuantaPDFGarbageStream",
            QPDFObjectHandle::newBool(true));
        QPDFObjectHandle reachable = pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /QuantaPDFReachable true >>"));
        pdf->getRoot().replaceKey("/QuantaPDFReachable", reachable);
        (void)garbage;

        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(true);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int rewrite_marker_mask(
    const unsigned char *data,
    size_t size)
{
    try {
        auto pdf = QPDF::create();
        pdf->processMemoryFile(
            "rewrite-marker-check",
            reinterpret_cast<char const *>(data),
            size);
        int mask = 0;
        for (QPDFObjectHandle object : pdf->getAllObjects()) {
            QPDFObjectHandle dictionary = object.isStream()
                ? object.getDict()
                : object;
            if (!dictionary.isDictionary())
                continue;
            if (dictionary.getKey("/QuantaPDFGarbage").isBool() &&
                dictionary.getKey("/QuantaPDFGarbage").getBoolValue())
                mask |= 1;
            if (dictionary.getKey("/QuantaPDFGarbageStream").isBool() &&
                dictionary.getKey("/QuantaPDFGarbageStream").getBoolValue())
                mask |= 2;
            if (dictionary.getKey("/QuantaPDFReachable").isBool() &&
                dictionary.getKey("/QuantaPDFReachable").getBoolValue())
                mask |= 4;
        }
        return mask;
    } catch (...) {
        return -1;
    }
}

extern "C" int rewrite_create_catalog_signature_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Type", QPDFObjectHandle::newName("/Sig")},
                {"/ByteRange", QPDFObjectHandle::parse("[0 1 2 3]")},
                {"/Contents", QPDFObjectHandle::newString("signed")}}));
        QPDFObjectHandle permissions = QPDFObjectHandle::newDictionary();
        permissions.replaceKey("/DocMDP", signature);
        pdf->getRoot().replaceKey("/Perms", permissions);

        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int rewrite_create_metadata_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle info = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Title", QPDFObjectHandle::newUnicodeString(
                    "QuantaPDF Rewrite")},
                {"/Author", QPDFObjectHandle::newUnicodeString(
                    "QuantaPDF")}}));
        pdf->getTrailer().replaceKey("/Info", info);

        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int rewrite_create_strict_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->processFile(source_path);
        (void)pdf->getAllPages();
        (void)pdf->getAllObjects();

        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(false);
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}
