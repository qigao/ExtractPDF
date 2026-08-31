#include "pdfium_runtime.h"

#include <fpdfview.h>

#include <exception>
#include <mutex>
#include <new>

namespace {

std::once_flag pdfium_once;
std::recursive_mutex pdfium_mutex;
quantapdf_status pdfium_initialization_status = QUANTAPDF_ERROR_BACKEND;

void initialize_pdfium()
{
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
    pdfium_initialization_status = QUANTAPDF_OK;
}

} // namespace

extern "C" quantapdf_status quantapdf_pdfium_enter(void)
{
    try {
        pdfium_mutex.lock();
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }

    try {
        std::call_once(pdfium_once, initialize_pdfium);
    } catch (std::bad_alloc const&) {
        pdfium_mutex.unlock();
        return QUANTAPDF_ERROR_NOMEM;
    } catch (std::exception const&) {
        pdfium_mutex.unlock();
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        pdfium_mutex.unlock();
        return QUANTAPDF_ERROR_BACKEND;
    }

    if (pdfium_initialization_status != QUANTAPDF_OK) {
        quantapdf_status const status = pdfium_initialization_status;
        pdfium_mutex.unlock();
        return status;
    }

    return QUANTAPDF_OK;
}

extern "C" void quantapdf_pdfium_leave(void)
{
    try {
        pdfium_mutex.unlock();
    } catch (...) {
    }
}
