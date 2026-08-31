# PDFium + qpdf Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a pinned, licensed, cross-platform PDFium + qpdf backend foundation while preserving the public ABI and replacing the backend-specific public error name with a backend-neutral source API.

**Architecture:** Import a verified PDFium shared binary through a focused CMake module and qpdf 12.4.0 through the pinned vcpkg manifest. Hide PDFium process lifetime and qpdf C++ exceptions behind private C-compatible adapters, then prove both engines can open the same immutable fixture.

**Tech Stack:** C11 public/library code, C++20 private backend bridges, PDFium public C API 154.0.8021.0, qpdf 12.4.0, CMake Presets, vcpkg, CTest.

**Spec:** `docs/superpowers/specs/2026-08-31-quantapdf-pdfium-qpdf-migration-design.md`

## Global Constraints

- Use PDFium public headers only; never include `core/`, `fpdfsdk/`, or other private PDFium headers.
- PDFium must be release `chromium/8021`, version `154.0.8021.0`, with the exact platform SHA-256 values in the spec.
- PDFium must have V8 and XFA disabled, as proven by the shipped `args.gn`.
- qpdf must resolve as version `12.4.0` from vcpkg baseline `f74a2eade17a628413746557d04db25ccf6e76f9`.
- No C++ exception, PDFium handle, qpdf class, or backend header may enter `include/quantapdf/quantapdf.h`.
- Do not add a runtime backend selector or MuPDF fallback abstraction.
- MuPDF coexistence is migration-branch-only and must be removed in the final migration phase.
- All CMake configure/build/test operations use versioned user presets.

---

### Task 1: Restore a compiler-clean migration baseline

**Files:**
- Modify: `tests/pdf_poster_fault_hook.c`
- Modify: `src/links.c`
- Modify: `include/quantapdf/quantapdf.h`
- Modify: `src/internal.h`
- Modify: `src/document.c`
- Modify: `src/status.c`
- Modify: all current callers/tests of `QUANTAPDF_ERROR_MUPDF` and `quantapdf_status_from_mupdf`

**Interfaces:**
- Consumes: existing test-only poster fault enums and MuPDF exception macros.
- Produces: warning-clean existing sources so new backend failures are not hidden by unrelated `-Werror` failures.

- [ ] **Step 1: Preserve the existing remote RED evidence**

Record in the commit message/body that GitHub run `33370657194` failed with enum comparison warnings on Windows/macOS and `-Wclobbered` on Linux. Do not create a new fixture for compiler diagnostics.

- [ ] **Step 2: Make the enum equality assertions compare their integer representations**

Change each assertion to this form:

```c
_Static_assert(
    (int)QUANTAPDF_TEST_PDF_POSTER_FAULT_NONE ==
        (int)QUANTAPDF_TEST_POSTER_FAULT_NONE,
    "poster fault enums must match");
```

Apply the same explicit cast to all four public/internal enum pairs. Do not merge the enum types; they belong to different private test interfaces.

- [ ] **Step 3: Protect link-loop state across MuPDF longjmp during the transitional build**

Immediately after the existing `fz_var(head)` add:

```c
fz_var(count);
fz_var(index);
```

This is transitional code and is deleted when `links.c` moves to PDFium.

- [ ] **Step 4: Run static source checks**

Rename `QUANTAPDF_ERROR_MUPDF` to `QUANTAPDF_ERROR_BACKEND` while keeping value `7`, rename the transitional internal mapper to `quantapdf_status_from_backend`, and change the status string to `"backend error"`. Apply the replacement to every current caller and test; do not retain a deprecated MuPDF alias.

- [ ] **Step 5: Run static source checks**

Run:

```powershell
git diff --check
rg -n "fz_var\((count|index)\)" src/links.c
rg -n "\(int\)QUANTAPDF_TEST_.*FAULT" tests/pdf_poster_fault_hook.c
rg -n "ERROR_MUPDF|status_from_mupdf|MuPDF error" include src tests
```

Expected: no diff errors, two `fz_var` lines, eight enum casts, and no legacy public/backend-error identifiers or strings.

- [ ] **Step 6: Commit**

```powershell
git add include/quantapdf/quantapdf.h src tests
git commit -m "fix: restore warning-clean migration baseline"
```

---

### Task 2: Add the pinned PDFium artifact resolver

**Files:**
- Create: `cmake/QuantaPDFPdfium.cmake`
- Modify: `CMakeLists.txt`
- Modify: `CMakeUserPresets.json`
- Create: `THIRD_PARTY.md`

**Interfaces:**
- Consumes: host OS/CPU values and the artifact URLs/hashes in the migration spec.
- Produces: imported target `QuantaPDF::PDFium`, `QUANTAPDF_PDFIUM_ROOT`, `QUANTAPDF_PDFIUM_RUNTIME_DIR`, and `QUANTAPDF_PDFIUM_LICENSE_DIR`.

- [ ] **Step 1: Write the resolver contract as configure-time checks**

Create `cmake/QuantaPDFPdfium.cmake` with a function:

```cmake
function(quantapdf_import_pdfium)
  if(TARGET QuantaPDF::PDFium)
    return()
  endif()

  set(_version "154.0.8021.0")
  set(_release "chromium/8021")
  set(_base_url
      "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8021")
  if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_platform "win-x64")
    set(_sha256 "adac8ce034015427b5daa81f8eeddfcc8e84bc2a9f036f007890ff18bd4388c4")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux"
         AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "linux-x64")
    set(_sha256 "685f7930cd184ea22cd77afe707c1cf53b173d18118b6e16cb213c9277d7cdc3")
  elseif(APPLE AND _quantapdf_target_processor MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "mac-x64")
    set(_sha256 "0e770fda56c6726a08fab84c6306ad91eceb10589020ce3a407fad3ebcbe7bb2")
  elseif(APPLE AND _quantapdf_target_processor MATCHES "^(arm64|aarch64)$")
    set(_platform "mac-arm64")
    set(_sha256 "994600fa28974ce09a1c51c35039e808a6bc8ea3839050322c101ab229ad5c96")
  else()
    message(FATAL_ERROR
            "PDFium 154.0.8021.0 has no pinned artifact for "
            "${CMAKE_SYSTEM_NAME}/${_quantapdf_target_processor}")
  endif()
  set(_url "${_base_url}/pdfium-${_platform}.tgz")
endfunction()
```

Before the platform branch, set `_quantapdf_target_processor` from the single entry in `CMAKE_OSX_ARCHITECTURES` when targeting macOS; otherwise use `CMAKE_SYSTEM_PROCESSOR`. Reject a multi-architecture macOS build because the pinned artifacts are single-architecture packages.

Download and extract with this exact status boundary:

```cmake
set(_archive "${CMAKE_BINARY_DIR}/_deps/downloads/pdfium-${_platform}.tgz")
set(_root "${CMAKE_BINARY_DIR}/_deps/pdfium-${_version}-${_platform}")
if(NOT EXISTS "${_archive}")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/downloads")
  file(DOWNLOAD "${_url}" "${_archive}"
       EXPECTED_HASH "SHA256=${_sha256}"
       TLS_VERIFY ON
       STATUS _download_status)
  list(GET _download_status 0 _download_code)
  list(GET _download_status 1 _download_message)
  if(NOT _download_code EQUAL 0)
    file(REMOVE "${_archive}")
    message(FATAL_ERROR "PDFium download failed: ${_download_message}")
  endif()
endif()
file(SHA256 "${_archive}" _actual_sha256)
if(NOT _actual_sha256 STREQUAL _sha256)
  file(REMOVE "${_archive}")
  message(FATAL_ERROR
          "Cached PDFium archive hash mismatch: expected ${_sha256}, "
          "got ${_actual_sha256}; the bad cache entry was removed")
endif()
if(NOT EXISTS "${_root}/VERSION")
  file(MAKE_DIRECTORY "${_root}")
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_root}")
endif()
```

- [ ] **Step 2: Define and validate the imported target**

After extraction, validate `include/fpdfview.h`, `LICENSE`, `licenses/`, and `args.gn`. Reject an `args.gn` that does not contain all three exact lines:

```text
pdf_enable_v8 = false
pdf_enable_xfa = false
pdf_use_partition_alloc = false
```

Create `quantapdf_pdfium` as a `SHARED IMPORTED GLOBAL` target and alias it:

```cmake
add_library(quantapdf_pdfium SHARED IMPORTED GLOBAL)
add_library(QuantaPDF::PDFium ALIAS quantapdf_pdfium)
```

Set `IMPORTED_LOCATION` to `bin/pdfium.dll` and `IMPORTED_IMPLIB` to `lib/pdfium.dll.lib` on Windows; set `IMPORTED_LOCATION` to `lib/libpdfium.so` on Linux and `lib/libpdfium.dylib` on macOS. Set the public include directory to the extracted `include` directory.

Export `QUANTAPDF_PDFIUM_ROOT`, `QUANTAPDF_PDFIUM_RUNTIME_DIR`, and `QUANTAPDF_PDFIUM_LICENSE_DIR` to the caller with `PARENT_SCOPE`; do not rely on function-local variables in later install or test logic.

- [ ] **Step 3: Wire the resolver into the root project without linking production code yet**

Raise the project language boundary and include the module:

```cmake
project(QuantaPDF VERSION 2.0.0 LANGUAGES C CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(QuantaPDFPdfium)
quantapdf_import_pdfium()
```

Do not expose the artifact root through `CMakeUserPresets.json` and do not use an unverified system PDFium fallback.

Set `CMAKE_OSX_ARCHITECTURES` to `x86_64` in `macos-x64-release-user` and to `arm64` in `macos-arm64-release-user`, so artifact selection follows the requested target rather than the CI host CPU.

- [ ] **Step 4: Document the exact third-party boundary**

Create `THIRD_PARTY.md` listing PDFium `154.0.8021.0`/`chromium/8021`, its binary distributor repository, the four hashes, qpdf `12.4.0`, their license families, and the rule that installed notices come from the pinned artifacts/packages.

- [ ] **Step 5: Validate CMake syntax and commit**

Run:

```powershell
cmake --list-presets
cmake --build --list-presets
ctest --list-presets
git diff --check
```

Expected: the same public preset names as before and no diff errors.

```powershell
git add CMakeLists.txt CMakeUserPresets.json cmake/QuantaPDFPdfium.cmake THIRD_PARTY.md docs/superpowers/plans/2026-08-31-quantapdf-pdfium-qpdf-foundation.md
git commit -m "build: pin PDFium backend artifact"
```

---

### Task 3: Add qpdf and its exception-safe C bridge

**Files:**
- Modify: `vcpkg.json`
- Modify: `CMakeLists.txt`
- Create: `src/backend/qpdf_document.h`
- Create: `src/backend/qpdf_document.cpp`

**Interfaces:**
- Consumes: immutable PDF bytes and optional UTF-8 password.
- Produces: the exact opaque bridge API defined in section 4.3 of the migration spec.

- [ ] **Step 1: Add qpdf to the pinned manifest**

Keep `libmupdf` only for transitional sources and add:

```json
{
  "name": "qpdf",
  "default-features": false
}
```

Do not enable OpenSSL, GnuTLS, or Zopfli features in this phase.

- [ ] **Step 2: Write the C bridge header**

`src/backend/qpdf_document.h` must include only `<stddef.h>` and the public status header, declare the opaque type, and expose:

```c
quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document);
quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count);
void quantapdf_qpdf_close(quantapdf_qpdf_document *document);
```

Wrap declarations with `extern "C"` under `__cplusplus`.

- [ ] **Step 3: Implement the bridge with total exception containment**

The private object owns `std::shared_ptr<QPDF>`. Open with:

```cpp
auto pdf = QPDF::create();
pdf->processMemoryFile(
    "quantapdf-memory",
    reinterpret_cast<char const*>(data),
    size,
    password_utf8);
```

Obtain the count from `pdf->getAllPages().size()`, reject values above `INT_MAX`, initialize every out-parameter before work, and catch in this order: `QPDFExc`, `std::bad_alloc`, `std::exception`, `...`. Map qpdf exception codes exactly:

```cpp
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
```

`std::bad_alloc` maps to `NOMEM`; other standard/unknown exceptions map to `BACKEND`. Never store exception-owned string pointers.

- [ ] **Step 4: Link qpdf privately**

Add:

```cmake
find_package(qpdf 12.4 EXACT CONFIG REQUIRED)
target_sources(quantapdf PRIVATE src/backend/qpdf_document.cpp)
target_compile_features(quantapdf PRIVATE cxx_std_20)
target_link_libraries(quantapdf PRIVATE qpdf::libqpdf)
```

Keep the C public compile feature at C11.

- [ ] **Step 5: Run header and dependency checks, then commit**

Run:

```powershell
rg -n "QPDF|std::|<qpdf/" include src --glob "*.h"
git diff --check
```

Expected: qpdf/C++ references occur only in `.cpp`; the private C bridge header remains C-compatible.

```powershell
git add vcpkg.json CMakeLists.txt src/backend/qpdf_document.h src/backend/qpdf_document.cpp
git commit -m "feat: add exception-safe qpdf bridge"
```

---

### Task 4: Add the serialized PDFium runtime and dual-engine smoke test

**Files:**
- Create: `src/backend/pdfium_runtime.h`
- Create: `src/backend/pdfium_runtime.cpp`
- Create: `tests/test_backend_foundation.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: PDFium public API and qpdf bridge from Task 3.
- Produces: private `quantapdf_pdfium_enter()` / `quantapdf_pdfium_leave()` and CTest `quantapdf.backend_foundation`.

- [ ] **Step 1: Define the runtime C interface**

Expose only:

```c
quantapdf_status quantapdf_pdfium_enter(void);
void quantapdf_pdfium_leave(void);
```

The header must not include a PDFium header. `enter()` guarantees initialization and returns with the recursive mutex held; every successful enter must be paired with one leave.

- [ ] **Step 2: Implement one-time initialization and serialization**

Use `std::once_flag`, `std::recursive_mutex`, and a stored initialization status. Inside `std::call_once`, call:

```cpp
FPDF_LIBRARY_CONFIG config = {};
config.version = 2;
config.m_pUserFontPaths = nullptr;
config.m_pIsolate = nullptr;
config.m_v8EmbedderSlot = 0;
FPDF_InitLibraryWithConfig(&config);
```

Do not call JavaScript/form action APIs and do not expose a public shutdown path.

- [ ] **Step 3: Write the dual-engine smoke RED**

`tests/test_backend_foundation.c` reads `ONE_PAGE_PDF` into owned bytes, enters PDFium, calls `FPDF_LoadMemDocument64`, checks `FPDF_GetPageCount(document) == 1`, closes the PDFium document, and leaves the runtime. It then passes the same bytes to `quantapdf_qpdf_open_memory`, checks qpdf page count equals one, and closes it.

Because tests must not include private vendor types through QuantaPDF headers, include `fpdfview.h`, `backend/pdfium_runtime.h`, and `backend/qpdf_document.h` explicitly in this private test target.

- [ ] **Step 4: Wire the runtime and test**

Add both backend `.cpp` files to `quantapdf`, link `QuantaPDF::PDFium`, and give the test private include access to `src`. Register:

```cmake
add_test(NAME quantapdf.backend_foundation COMMAND quantapdf_test_backend_foundation)
set_tests_properties(quantapdf.backend_foundation PROPERTIES
  TIMEOUT 30
  ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}")
```

On Linux set `ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}"`. On macOS set `ENVIRONMENT_MODIFICATION "DYLD_LIBRARY_PATH=path_list_prepend:${QUANTAPDF_PDFIUM_RUNTIME_DIR}"`. Do not copy the library after build.

- [ ] **Step 5: Configure, build, and run the focused test**

From a Visual Studio developer environment run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_backend_foundation
ctest --preset win-release-user -R "^quantapdf.backend_foundation$"
```

Expected: one test passes and both engines report one page from the same bytes.

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt tests/CMakeLists.txt src/backend/pdfium_runtime.h src/backend/pdfium_runtime.cpp tests/test_backend_foundation.c
git commit -m "test: prove PDFium and qpdf backend foundation"
```

---

### Task 5: Package notices and prove the foundation on CI

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`

**Interfaces:**
- Consumes: pinned PDFium root/license variables and qpdf vcpkg package.
- Produces: distributable notices, runtime library installation, and exact-head three-platform evidence.

- [ ] **Step 1: Install PDFium runtime and notices**

Install the PDFium shared library to `bin` on Windows and `lib` on Linux/macOS. Install PDFium `LICENSE` plus the complete `licenses/` directory under `share/quantapdf/licenses/pdfium`. Install `THIRD_PARTY.md` under `share/quantapdf`.

- [ ] **Step 2: Make CI cache identity include both dependency locks**

Keep the vcpkg baseline cache keyed by `vcpkg.json` and add the literal PDFium release `chromium-8021` to the PDFium download cache key. Do not use “latest” URLs or floating branch names.

- [ ] **Step 3: Add artifact audits after configure**

On every platform, fail CI unless generated PDFium `args.gn` contains the three disabled-feature lines and its `VERSION` contains:

```text
MAJOR=154
MINOR=0
BUILD=8021
PATCH=0
```

- [ ] **Step 4: Document the transitional state**

README must state that the migration branch has a PDFium/qpdf foundation while the existing feature implementation is still being replaced. It must not claim that MuPDF has been removed until the final gate passes.

- [ ] **Step 5: Run full verification**

Run locally:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
git diff --check
```

Then push the feature branch and require Linux Release, Linux ASan/UBSan, macOS arm64, and Windows Release to pass on one SHA. A red job blocks the next migration phase.

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt .github/workflows/ci.yml README.md
git commit -m "build: package permissive PDF backend notices"
```
