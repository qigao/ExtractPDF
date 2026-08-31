#include "pdfium_document.h"

#include "pdfium_runtime.h"

#include <fpdfview.h>

#include <cmath>
#include <exception>
#include <memory>
#include <new>

struct quantapdf_pdfium_document {
    FPDF_DOCUMENT handle;
};

struct quantapdf_pdfium_page {
    FPDF_PAGE handle;
};

namespace {

class pdfium_scope final {
public:
    explicit pdfium_scope(quantapdf_status status) noexcept
        : entered_(status == QUANTAPDF_OK)
    {
    }

    ~pdfium_scope()
    {
        if (entered_)
            quantapdf_pdfium_leave();
    }

    pdfium_scope(pdfium_scope const&) = delete;
    pdfium_scope& operator=(pdfium_scope const&) = delete;

private:
    bool entered_;
};

quantapdf_status status_from_pdfium(unsigned long error) noexcept
{
    switch (error) {
    case FPDF_ERR_FILE:
        return QUANTAPDF_ERROR_IO;
    case FPDF_ERR_FORMAT:
    case FPDF_ERR_PAGE:
        return QUANTAPDF_ERROR_FORMAT;
    case FPDF_ERR_PASSWORD:
        return QUANTAPDF_ERROR_PASSWORD;
    case FPDF_ERR_SECURITY:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    case FPDF_ERR_SUCCESS:
    case FPDF_ERR_UNKNOWN:
    default:
        return QUANTAPDF_ERROR_BACKEND;
    }
}

} // namespace

extern "C" quantapdf_status quantapdf_pdfium_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_pdfium_document **out_document)
{
    quantapdf_status status;

    if (out_document == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = nullptr;
    if (data == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto document = std::make_unique<quantapdf_pdfium_document>();
        document->handle = nullptr;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        document->handle = FPDF_LoadMemDocument64(
            data, size, password_utf8);
        if (document->handle == nullptr)
            return status_from_pdfium(FPDF_GetLastError());
        *out_document = document.release();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_page_count(
    quantapdf_pdfium_document *document,
    int *out_page_count)
{
    quantapdf_status status;
    int count;

    if (document == nullptr || out_page_count == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    count = FPDF_GetPageCount(document->handle);
    if (count < 0)
        return QUANTAPDF_ERROR_BACKEND;
    *out_page_count = count;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_pdfium_load_page(
    quantapdf_pdfium_document *document,
    int page_index,
    quantapdf_pdfium_page **out_page)
{
    quantapdf_status status;

    if (out_page == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = nullptr;
    if (document == nullptr || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto page = std::make_unique<quantapdf_pdfium_page>();
        page->handle = nullptr;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        if (page_index >= FPDF_GetPageCount(document->handle))
            return QUANTAPDF_ERROR_ARGUMENT;
        page->handle = FPDF_LoadPage(document->handle, page_index);
        if (page->handle == nullptr)
            return status_from_pdfium(FPDF_GetLastError());
        *out_page = page.release();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_pdfium_page_bounds(
    quantapdf_pdfium_page *page,
    quantapdf_rect *out_bounds)
{
    quantapdf_status status;
    float width;
    float height;

    if (page == nullptr || out_bounds == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    width = FPDF_GetPageWidthF(page->handle);
    height = FPDF_GetPageHeightF(page->handle);
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f)
        return QUANTAPDF_ERROR_FORMAT;
    out_bounds->x0 = 0.0f;
    out_bounds->y0 = 0.0f;
    out_bounds->x1 = width;
    out_bounds->y1 = height;
    return QUANTAPDF_OK;
}

extern "C" void quantapdf_pdfium_drop_page(quantapdf_pdfium_page *page)
{
    quantapdf_status status;

    if (page == nullptr)
        return;
    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return;
    pdfium_scope const scope(status);
    FPDF_ClosePage(page->handle);
    delete page;
}

extern "C" void quantapdf_pdfium_close(
    quantapdf_pdfium_document *document)
{
    quantapdf_status status;

    if (document == nullptr)
        return;
    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return;
    pdfium_scope const scope(status);
    FPDF_CloseDocument(document->handle);
    delete document;
}
