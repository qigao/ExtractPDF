#include "pdfium_document.h"

#include "pdfium_runtime.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>

struct quantapdf_pdfium_document {
    FPDF_DOCUMENT handle;
};

struct quantapdf_pdfium_page {
    FPDF_DOCUMENT document;
    int page_index;
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

quantapdf_status ensure_page_handle(quantapdf_pdfium_page *page) noexcept
{
    if (page->handle != nullptr)
        return QUANTAPDF_OK;
    page->handle = FPDF_LoadPage(page->document, page->page_index);
    if (page->handle == nullptr)
        return status_from_pdfium(FPDF_GetLastError());
    return QUANTAPDF_OK;
}

bool checked_bitmap_size(int width, int height, int components,
                         size_t *out_stride, size_t *out_size) noexcept
{
    size_t const row = static_cast<size_t>(width) *
        static_cast<size_t>(components);

    if (width <= 0 || height <= 0 || components <= 0 ||
        row > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        static_cast<size_t>(height) >
            std::numeric_limits<size_t>::max() / row)
        return false;
    *out_stride = row;
    *out_size = row * static_cast<size_t>(height);
    return true;
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
        page->document = nullptr;
        page->page_index = -1;
        page->handle = nullptr;
        status = quantapdf_pdfium_enter();
        if (status != QUANTAPDF_OK)
            return status;
        pdfium_scope const scope(status);
        if (page_index >= FPDF_GetPageCount(document->handle))
            return QUANTAPDF_ERROR_ARGUMENT;
        page->document = document->handle;
        page->page_index = page_index;
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
    FS_SIZEF size = {};

    if (page == nullptr || out_bounds == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    if (!FPDF_GetPageSizeByIndexF(
            page->document, page->page_index, &size))
        return status_from_pdfium(FPDF_GetLastError());
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width <= 0.0f || size.height <= 0.0f)
        return QUANTAPDF_ERROR_FORMAT;
    out_bounds->x0 = 0.0f;
    out_bounds->y0 = 0.0f;
    out_bounds->x1 = size.width;
    out_bounds->y1 = size.height;
    return QUANTAPDF_OK;
}

extern "C" quantapdf_status quantapdf_pdfium_render_page(
    quantapdf_pdfium_page *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    int alpha,
    quantapdf_pdfium_bitmap *out_bitmap)
{
    quantapdf_status status;
    FS_SIZEF page_size = {};
    FS_MATRIX matrix = {};
    FS_RECTF device_clip = {};
    FPDF_BITMAP pdfium_bitmap = nullptr;
    double const radians = static_cast<double>(rotation_degrees) *
        3.14159265358979323846 / 180.0;
    double cosine = std::cos(radians);
    double sine = std::sin(radians);
    double const scale = static_cast<double>(dpi) / 72.0;
    double source_x0;
    double source_y0;
    double source_x1;
    double source_y1;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    int width;
    int height;
    int components;
    int source_stride;
    size_t stride;
    size_t size;
    unsigned char *data = nullptr;

    if (out_bitmap == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    std::memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (page == nullptr || !std::isfinite(dpi) || dpi <= 0.0f ||
        !std::isfinite(rotation_degrees) || (alpha != 0 && alpha != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    if (std::abs(cosine) < 1.0e-12)
        cosine = 0.0;
    if (std::abs(sine) < 1.0e-12)
        sine = 0.0;

    status = quantapdf_pdfium_enter();
    if (status != QUANTAPDF_OK)
        return status;
    pdfium_scope const scope(status);
    status = ensure_page_handle(page);
    if (status != QUANTAPDF_OK)
        return status;
    if (!FPDF_GetPageSizeByIndexF(page->document, page->page_index, &page_size))
        return status_from_pdfium(FPDF_GetLastError());

    source_x0 = clip == nullptr ? 0.0 : clip->x0;
    source_y0 = clip == nullptr ? 0.0 : clip->y0;
    source_x1 = clip == nullptr ? page_size.width : clip->x1;
    source_y1 = clip == nullptr ? page_size.height : clip->y1;

    {
        double const source_width = (source_x1 - source_x0) * scale;
        double const source_height = (source_y1 - source_y0) * scale;
        double const xs[] = {
            0.0,
            cosine * source_width,
            -sine * source_height,
            cosine * source_width - sine * source_height
        };
        double const ys[] = {
            0.0,
            sine * source_width,
            cosine * source_height,
            sine * source_width + cosine * source_height
        };
        min_x = *std::min_element(std::begin(xs), std::end(xs));
        max_x = *std::max_element(std::begin(xs), std::end(xs));
        min_y = *std::min_element(std::begin(ys), std::end(ys));
        max_y = *std::max_element(std::begin(ys), std::end(ys));
    }

    if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
        !std::isfinite(max_x) || !std::isfinite(max_y) ||
        max_x - min_x > static_cast<double>(std::numeric_limits<int>::max()) ||
        max_y - min_y > static_cast<double>(std::numeric_limits<int>::max()))
        return QUANTAPDF_ERROR_ARGUMENT;

    width = static_cast<int>(std::ceil(max_x) - std::floor(min_x));
    height = static_cast<int>(std::ceil(max_y) - std::floor(min_y));
    components = alpha != 0 ? 4 : 3;
    if (!checked_bitmap_size(width, height, components, &stride, &size))
        return QUANTAPDF_ERROR_ARGUMENT;

    pdfium_bitmap = FPDFBitmap_Create(width, height, alpha);
    if (pdfium_bitmap == nullptr)
        return QUANTAPDF_ERROR_NOMEM;
    FPDFBitmap_FillRect(
        pdfium_bitmap, 0, 0, width, height,
        alpha != 0 ? 0x00000000u : 0xffffffffu);

    matrix.a = static_cast<float>(cosine * scale);
    matrix.b = static_cast<float>(sine * scale);
    matrix.c = static_cast<float>(sine * scale);
    matrix.d = static_cast<float>(-cosine * scale);
    matrix.e = static_cast<float>(
        -cosine * source_x0 * scale -
        sine * (static_cast<double>(page_size.height) - source_y0) * scale -
        std::floor(min_x));
    matrix.f = static_cast<float>(
        -sine * source_x0 * scale +
        cosine * (static_cast<double>(page_size.height) - source_y0) * scale -
        std::floor(min_y));
    device_clip.left = 0.0f;
    device_clip.top = 0.0f;
    device_clip.right = static_cast<float>(width);
    device_clip.bottom = static_cast<float>(height);
    FPDF_RenderPageBitmapWithMatrix(
        pdfium_bitmap, page->handle, &matrix, &device_clip,
        FPDF_ANNOT | FPDF_LCD_TEXT);

    try {
        data = static_cast<unsigned char *>(std::malloc(size));
        if (data == nullptr) {
            FPDFBitmap_Destroy(pdfium_bitmap);
            return QUANTAPDF_ERROR_NOMEM;
        }
        auto const *source = static_cast<unsigned char const *>(
            FPDFBitmap_GetBuffer(pdfium_bitmap));
        source_stride = FPDFBitmap_GetStride(pdfium_bitmap);
        if (source == nullptr || source_stride <= 0) {
            std::free(data);
            FPDFBitmap_Destroy(pdfium_bitmap);
            return QUANTAPDF_ERROR_BACKEND;
        }
        for (int y = 0; y < height; ++y) {
            auto const *source_row = source +
                static_cast<size_t>(source_stride) * static_cast<size_t>(y);
            auto *target_row = data + stride * static_cast<size_t>(y);
            for (int x = 0; x < width; ++x) {
                auto const *pixel = source_row + static_cast<size_t>(x) * 4u;
                auto *target = target_row +
                    static_cast<size_t>(x) * static_cast<size_t>(components);
                target[0] = pixel[2];
                target[1] = pixel[1];
                target[2] = pixel[0];
                if (alpha != 0)
                    target[3] = pixel[3];
            }
        }
    } catch (...) {
        std::free(data);
        FPDFBitmap_Destroy(pdfium_bitmap);
        return QUANTAPDF_ERROR_BACKEND;
    }

    FPDFBitmap_Destroy(pdfium_bitmap);
    out_bitmap->data = data;
    out_bitmap->size = size;
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->stride = static_cast<int>(stride);
    out_bitmap->components = components;
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
    if (page->handle != nullptr)
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
