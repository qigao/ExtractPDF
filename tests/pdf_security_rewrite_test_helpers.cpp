#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>

extern "C" {

typedef struct quantapdf_security_inspection {
    int encrypted;
    int revision;
    int version;
    int stream_aesv3;
    int string_aesv3;
    int file_aesv3;
    int extension_level;
    int encrypt_metadata;
    int allow_accessibility;
    int allow_copy;
    int allow_assemble;
    int allow_fill_forms;
    int allow_annotate_and_fill_forms;
    int allow_modify_other;
    int allow_print_low_resolution;
    int allow_print_high_quality;
    unsigned char id1[16];
    unsigned char id2[16];
    unsigned char encryption_key[32];
    size_t encryption_key_size;
} quantapdf_security_inspection;

int quantapdf_security_inspect_pdf(
    unsigned char const *data,
    size_t size,
    char const *password,
    quantapdf_security_inspection *out)
{
    if (data == nullptr || size == 0 || out == nullptr)
        return 0;
    *out = {};
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "quantapdf-security-inspection",
            reinterpret_cast<char const *>(data),
            size,
            password == nullptr ? "" : password);
        int permissions = 0;
        QPDF::encryption_method_e stream_method = QPDF::e_none;
        QPDF::encryption_method_e string_method = QPDF::e_none;
        QPDF::encryption_method_e file_method = QPDF::e_none;
        out->encrypted = pdf->isEncrypted(
            out->revision,
            permissions,
            out->version,
            stream_method,
            string_method,
            file_method) ? 1 : 0;
        out->stream_aesv3 = stream_method == QPDF::e_aesv3;
        out->string_aesv3 = string_method == QPDF::e_aesv3;
        out->file_aesv3 = file_method == QPDF::e_aesv3;
        out->extension_level = pdf->getExtensionLevel();
        out->encrypt_metadata = 1;
        if (out->encrypted) {
            QPDFObjectHandle encrypt = pdf->getTrailer().getKey("/Encrypt");
            QPDFObjectHandle value = encrypt.getKey("/EncryptMetadata");
            if (value.isBool())
                out->encrypt_metadata = value.getBoolValue() ? 1 : 0;
            out->allow_accessibility = pdf->allowAccessibility();
            out->allow_copy = pdf->allowExtractAll();
            out->allow_assemble = pdf->allowModifyAssembly();
            out->allow_fill_forms = pdf->allowModifyForm();
            out->allow_annotate_and_fill_forms =
                pdf->allowModifyAnnotation();
            out->allow_modify_other = pdf->allowModifyOther();
            out->allow_print_low_resolution = pdf->allowPrintLowRes();
            out->allow_print_high_quality = pdf->allowPrintHighRes();
            QPDFObjectHandle identifiers =
                pdf->getTrailer().getKey("/ID");
            if (identifiers.isArray() &&
                identifiers.getArrayNItems() == 2 &&
                identifiers.getArrayItem(0).isString() &&
                identifiers.getArrayItem(1).isString()) {
                std::string const id1 =
                    identifiers.getArrayItem(0).getStringValue();
                std::string const id2 =
                    identifiers.getArrayItem(1).getStringValue();
                if (id1.size() >= sizeof(out->id1))
                    std::memcpy(out->id1, id1.data(), sizeof(out->id1));
                if (id2.size() >= sizeof(out->id2))
                    std::memcpy(out->id2, id2.data(), sizeof(out->id2));
            }
            std::string const key = pdf->getEncryptionKey();
            out->encryption_key_size = key.size();
            if (key.size() >= sizeof(out->encryption_key))
                std::memcpy(
                    out->encryption_key,
                    key.data(),
                    sizeof(out->encryption_key));
        }
        return pdf->anyWarnings() ? 0 : 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_metadata_fixture(
    char const *source_path,
    char const *output_path)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle metadata = pdf->newStream(
            "<x:xmpmeta>QUANTAPDF_METADATA_CLEAR_MARKER</x:xmpmeta>");
        metadata.getDict().replaceKey(
            "/Type", QPDFObjectHandle::newName("/Metadata"));
        metadata.getDict().replaceKey(
            "/Subtype", QPDFObjectHandle::newName("/XML"));
        pdf->getRoot().replaceKey("/Metadata", metadata);
        QPDFObjectHandle info = pdf->makeIndirectObject(
            QPDFObjectHandle::newDictionary({
                {"/Title", QPDFObjectHandle::newUnicodeString(
                    "QuantaPDF Security Metadata")}}));
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

int quantapdf_security_create_signature_fixture(
    char const *source_path,
    char const *output_path,
    int kind,
    int encrypt)
{
    if (source_path == nullptr || output_path == nullptr ||
        kind < 1 || kind > 4)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle signature = QPDFObjectHandle::newNull();
        if (kind != 4) {
            signature = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Type", QPDFObjectHandle::newName(
                        kind == 2 ? "/DocTimeStamp" : "/Sig")},
                    {"/Contents", QPDFObjectHandle::newString("signed")}}));
        }
        if (kind == 1) {
            pdf->getRoot().replaceKey(
                "/QuantaPDFReachableSignature", signature);
        } else if (kind == 3) {
            QPDFObjectHandle permissions = QPDFObjectHandle::newDictionary({
                {"/DocMDP", signature}});
            pdf->getRoot().replaceKey("/Perms", permissions);
        } else if (kind == 4) {
            QPDFObjectHandle field = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/FT", QPDFObjectHandle::newName("/Sig")},
                    {"/V", QPDFObjectHandle::newString("malformed")}}));
            QPDFObjectHandle fields = QPDFObjectHandle::newArray();
            fields.appendItem(field);
            pdf->getRoot().replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", fields}}));
        }

        QPDFWriter writer(*pdf, output_path);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setStreamDataMode(qpdf_s_preserve);
        writer.setPreserveUnreferencedObjects(true);
        if (encrypt) {
            writer.setR6EncryptionParameters(
                "sig-user", "sig-owner", true, false, false, false,
                false, false, qpdf_r3p_none, true);
        } else {
            writer.setDeterministicID(true);
        }
        writer.write();
        return 1;
    } catch (...) {
        return 0;
    }
}

int quantapdf_security_create_id_fixture(
    char const *source_path,
    char const *output_path,
    int malformed)
{
    if (source_path == nullptr || output_path == nullptr)
        return 0;
    try {
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFWriter writer(*pdf, output_path);
        writer.setDeterministicID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        if (!malformed)
            return 1;

        std::ifstream input(output_path, std::ios::binary);
        std::string bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        input.close();
        size_t const begin = bytes.find("/ID [");
        if (begin == std::string::npos)
            return 0;
        size_t const end = bytes.find(']', begin);
        if (end == std::string::npos)
            return 0;
        bytes.replace(begin, end - begin + 1u, "/ID [() ()]");
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return output.good() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

}
