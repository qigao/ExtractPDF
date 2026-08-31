#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <zlib.h>

#include <stdexcept>
#include <string>
#include <vector>

static void rewrite_write_fixture(QPDF& pdf, const char *output_path)
{
    QPDFWriter writer(pdf, output_path);
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.write();
}

static QPDFObjectHandle rewrite_signature(QPDF& pdf)
{
    return pdf.makeIndirectObject(QPDFObjectHandle::newDictionary({
        {"/Type", QPDFObjectHandle::newName("/Sig")},
        {"/ByteRange", QPDFObjectHandle::parse("[0 1 2 3]")},
        {"/Contents", QPDFObjectHandle::newString("signed")}}));
}

extern "C" int rewrite_create_policy_fixture(
    const char *source_path,
    const char *output_path,
    int kind)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle root = pdf->getRoot();
        QPDFObjectHandle signature = rewrite_signature(*pdf);

        if (kind == 1 || kind == 2) {
            QPDFObjectHandle permissions = QPDFObjectHandle::newDictionary();
            permissions.replaceKey(kind == 1 ? "/UR" : "/UR3", signature);
            root.replaceKey("/Perms", permissions);
        } else if (kind == 3) {
            root.replaceKey("/Perms", QPDFObjectHandle::newName("/Bad"));
        } else if (kind == 4) {
            root.replaceKey(
                "/AcroForm",
                QPDFObjectHandle::newDictionary({
                    {"/Fields", QPDFObjectHandle::newName("/Bad")}}));
        } else if (kind == 5) {
            QPDFObjectHandle field = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Kids", QPDFObjectHandle::newName("/Bad")}}));
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(field);
            root.replaceKey(
                "/AcroForm",
                QPDFObjectHandle::newDictionary({{"/Fields", fields}}));
        } else if (kind == 6 || kind == 7) {
            QPDFObjectHandle parent = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary());
            QPDFObjectHandle child_dictionary =
                QPDFObjectHandle::newDictionary({{"/Parent", parent}});
            if (kind == 6) {
                parent.replaceKey("/FT", QPDFObjectHandle::newName("/Sig"));
                child_dictionary.replaceKey("/V", signature);
            } else {
                parent.replaceKey("/V", signature);
                child_dictionary.replaceKey(
                    "/FT", QPDFObjectHandle::newName("/Sig"));
            }
            QPDFObjectHandle child = pdf->makeIndirectObject(child_dictionary);
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(child);
            parent.replaceKey("/Kids", kids);
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(parent);
            root.replaceKey(
                "/AcroForm",
                QPDFObjectHandle::newDictionary({{"/Fields", fields}}));
        } else {
            return 0;
        }

        rewrite_write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int rewrite_create_root_parent_signature_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = rewrite_signature(*pdf);
        QPDFObjectHandle external_parent = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/FT", QPDFObjectHandle::newName("/Sig")},
                {"/V", signature}}));
        QPDFObjectHandle root_field = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/T", QPDFObjectHandle::newUnicodeString("root")},
                {"/Parent", external_parent}}));
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(root_field);
        pdf->getRoot().replaceKey(
            "/AcroForm",
            QPDFObjectHandle::newDictionary({{"/Fields", fields}}));
        rewrite_write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int rewrite_create_mismatched_parent_signature_fixture(
    const char *source_path,
    const char *output_path)
{
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = rewrite_signature(*pdf);
        QPDFObjectHandle external_parent = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/FT", QPDFObjectHandle::newName("/Sig")},
                {"/V", signature}}));
        QPDFObjectHandle child = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/T", QPDFObjectHandle::newUnicodeString("child")},
                {"/Parent", external_parent}}));
        QPDFObjectHandle kids = QPDFObjectHandle::newArray();
        kids.appendItem(child);
        QPDFObjectHandle traversed_parent = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/T", QPDFObjectHandle::newUnicodeString("parent")},
                {"/FT", QPDFObjectHandle::newName("/Tx")},
                {"/Kids", kids}}));
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(traversed_parent);
        pdf->getRoot().replaceKey(
            "/AcroForm",
            QPDFObjectHandle::newDictionary({{"/Fields", fields}}));
        rewrite_write_fixture(*pdf, output_path);
        return 1;
    } catch (...) {
        return 0;
    }
}

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
        QPDFObjectHandle shared = pdf->makeIndirectObject(
            QPDFObjectHandle::parse("<< /QuantaPDFShared true >>"));
        pdf->getRoot().replaceKey("/QuantaPDFSharedA", shared);
        pdf->getRoot().replaceKey("/QuantaPDFSharedB", shared);

        char const stream_plaintext[] = "preserve-this-encoded-stream";
        uLongf encoded_size = compressBound(sizeof(stream_plaintext) - 1u);
        std::vector<unsigned char> encoded(encoded_size);
        if (compress2(
                encoded.data(), &encoded_size,
                reinterpret_cast<Bytef const *>(stream_plaintext),
                sizeof(stream_plaintext) - 1u,
                Z_BEST_COMPRESSION) != Z_OK)
            return 0;
        encoded.resize(encoded_size);
        QPDFObjectHandle reachable_stream = pdf->newStream();
        reachable_stream.replaceStreamData(
            std::string(
                reinterpret_cast<char const *>(encoded.data()),
                encoded.size()),
            QPDFObjectHandle::newName("/FlateDecode"),
            QPDFObjectHandle::parse("<< /Predictor 1 >>"));
        pdf->getRoot().replaceKey(
            "/QuantaPDFReachableStream", reachable_stream);
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

struct rewrite_graph_snapshot {
    bool shared = false;
    std::string filter;
    std::string decode_parms;
    std::string raw_stream;
};

static rewrite_graph_snapshot rewrite_capture_graph(
    unsigned char const *data,
    size_t size)
{
    auto pdf = QPDF::create();
    pdf->processMemoryFile(
        "rewrite-graph-check",
        reinterpret_cast<char const *>(data),
        size);
    QPDFObjectHandle root = pdf->getRoot();
    QPDFObjectHandle left = root.getKey("/QuantaPDFSharedA");
    QPDFObjectHandle right = root.getKey("/QuantaPDFSharedB");
    QPDFObjectHandle stream = root.getKey("/QuantaPDFReachableStream");
    if (!left.isIndirect() || !right.isIndirect() ||
        !left.isSameObjectAs(right) || !stream.isStream())
        throw std::runtime_error("rewrite graph invariant missing");
    std::shared_ptr<Buffer> raw = stream.getRawStreamData();
    rewrite_graph_snapshot snapshot;
    snapshot.shared = true;
    snapshot.filter = stream.getDict().getKey("/Filter").unparseResolved();
    snapshot.decode_parms =
        stream.getDict().getKey("/DecodeParms").unparseResolved();
    snapshot.raw_stream.assign(raw->data(), raw->size());
    return snapshot;
}

extern "C" int rewrite_graph_invariants(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size)
{
    try {
        rewrite_graph_snapshot left =
            rewrite_capture_graph(before, before_size);
        rewrite_graph_snapshot right =
            rewrite_capture_graph(after, after_size);
        return left.shared && right.shared &&
            left.filter == "/FlateDecode" &&
            left.filter == right.filter &&
            left.decode_parms == "<< /Predictor 1 >>" &&
            left.decode_parms == right.decode_parms &&
            left.raw_stream == right.raw_stream;
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
