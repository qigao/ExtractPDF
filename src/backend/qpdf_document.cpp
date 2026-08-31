#include "qpdf_document.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFWriter.hh>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>

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
        if (!value.null() && !value.isNumber())
            return QUANTAPDF_ERROR_FORMAT;
        double const user_unit = value.null() ? 1.0 : value.getNumericValue();
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
