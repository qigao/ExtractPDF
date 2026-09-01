#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>

#include <cstddef>

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
        }
        return pdf->anyWarnings() ? 0 : 1;
    } catch (...) {
        return 0;
    }
}

}
