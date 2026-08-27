# ExtractPDF v2 Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the ExtractPDF v2 foundation: a stable C11 ABI over MuPDF 1.28.2 with safe document lifecycle, password handling, page counting, deterministic fixtures, CTest, and exact-head Windows/Linux/macOS CI.

**Architecture:** MuPDF stays entirely behind an opaque `extractpdf_document`. Each handle owns one `fz_context` and one `fz_document`; ExtractPDF owns no mutable process-global or thread-local state. MuPDF exceptions are caught inside wrapper functions and translated to the fixed `extractpdf_status` enum.

**Tech Stack:** C11, CMake 3.20+, CTest, MuPDF 1.28.2 public C API, GitHub Actions; MSVC + MuPDF DLL client on Windows, static MuPDF libraries on Linux/macOS.

**Spec:** `docs/superpowers/specs/2026-08-27-extractpdf-v2-design.md`

## Global Constraints

- MuPDF CI baseline is exactly 1.28.2.
- Public headers contain no MuPDF type or include.
- No mutable process-global or thread-local state in `src/`.
- Phase 1 is single-threaded; separate handles may be used interleaved on one thread only.
- Any MuPDF call that can throw is inside `fz_try`/`fz_always`/`fz_catch`.
- Public paths are UTF-8.
- CTest is the only test entry point.
- Open-source repository license is AGPL-3.0-or-later.
- Legacy `libpdf.c` is untouched in Phase 1.
- ExtractPDF-owned code builds with warnings as errors.

## File map

- `LICENSE` — GNU AGPL v3 license text.
- `CMakeLists.txt` — project/library/test wiring and warning policy.
- `cmake/FindMuPDF.cmake` — creates imported target `MuPDF::MuPDF` from `MUPDF_ROOT`.
- `include/extractpdf/extractpdf.h` — complete Phase 1 public ABI.
- `src/internal.h` — private handle and MuPDF error translator declaration.
- `src/status.c` — immutable status strings only.
- `src/document.c` — open/password/page-count/close lifecycle only.
- `tests/CMakeLists.txt` — native tests and absolute fixture paths.
- `tests/test_status.c` — status ABI tests.
- `tests/test_document.c` — lifecycle/error/isolation/UTF-8 tests.
- `tests/fixtures/one-page.pdf` — valid 1-page PDF.
- `tests/fixtures/two-page.pdf` — valid 2-page PDF.
- `tests/fixtures/encrypted-one-page.pdf` — AES-256, user `user-pass`, owner `owner-pass`.
- `tests/fixtures/truncated.pdf` — malformed input that `mutool info` rejects.
- `.github/workflows/ci.yml` — exact MuPDF 1.28.2 build plus CMake/CTest matrix.
- `README.md` — supported v2 contract and legacy status.

---

### Task 1: License, public ABI, status mapping

**Files:** Create `LICENSE`, `CMakeLists.txt`, `include/extractpdf/extractpdf.h`, `src/status.c`, `tests/CMakeLists.txt`, `tests/test_status.c`.

**Produces:**

```c
typedef struct extractpdf_document extractpdf_document;
extractpdf_status extractpdf_open(const char *, const char *, extractpdf_document **);
extractpdf_status extractpdf_page_count(extractpdf_document *, int *);
const char *extractpdf_status_string(extractpdf_status);
void extractpdf_close(extractpdf_document *);
```

- [ ] **Step 1: Add the AGPL license**

Use the unmodified GNU Affero General Public License version 3 text from `https://www.gnu.org/licenses/agpl-3.0.txt`. Repository notices/README identify the project as AGPL-3.0-or-later.

- [ ] **Step 2: Add the public header from the approved spec**

Keep enum values `EXTRACTPDF_OK = 0` through `EXTRACTPDF_ERROR_MUPDF = 7`, the four declarations above, `extern "C"`, and the `_WIN32` / `EXTRACTPDF_SHARED` / `EXTRACTPDF_BUILDING_LIBRARY` export macro. Do not include MuPDF.

- [ ] **Step 3: Create an intentionally empty `src/status.c` and the RED test**

`tests/test_status.c`:

```c
#include <extractpdf/extractpdf.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_OK), "ok") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_ARGUMENT), "invalid argument") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_IO), "I/O error") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_PASSWORD), "password required or invalid") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_FORMAT), "invalid document format") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_UNSUPPORTED), "unsupported operation or content") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_NOMEM), "out of memory") == 0);
    assert(strcmp(extractpdf_status_string(EXTRACTPDF_ERROR_MUPDF), "MuPDF error") == 0);
    assert(strcmp(extractpdf_status_string((extractpdf_status)999), "unknown error") == 0);
    return 0;
}
```

- [ ] **Step 4: Add minimum CMake and verify RED**

Top level:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ExtractPDF VERSION 2.0.0 LANGUAGES C)
option(EXTRACTPDF_BUILD_TESTS "Build ExtractPDF tests" ON)

add_library(extractpdf src/status.c)
add_library(ExtractPDF::ExtractPDF ALIAS extractpdf)
target_compile_features(extractpdf PUBLIC c_std_11)
target_include_directories(extractpdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
if(MSVC)
  target_compile_options(extractpdf PRIVATE /W4 /WX)
else()
  target_compile_options(extractpdf PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(EXTRACTPDF_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
```

`tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_status test_status.c)
target_link_libraries(extractpdf_test_status PRIVATE ExtractPDF::ExtractPDF)
add_test(NAME extractpdf.status COMMAND extractpdf_test_status)
```

Run:

```bash
cmake -S . -B build
cmake --build build
```

Expected: link failure for undefined `extractpdf_status_string`.

- [ ] **Step 5: Implement only the status switch and verify GREEN**

`src/status.c` returns exactly the strings asserted above; default returns `"unknown error"`.

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `extractpdf.status` passes.

- [ ] **Step 6: Commit**

```bash
git add LICENSE CMakeLists.txt include src/status.c tests
git commit -m "feat: establish ExtractPDF v2 public ABI"
```

---

### Task 2: MuPDF discovery and valid document lifecycle

**Files:** Create `cmake/FindMuPDF.cmake`, `src/internal.h`, `src/document.c`, `tests/fixtures/one-page.pdf`, `tests/fixtures/two-page.pdf`, `tests/test_document.c`; modify top/test CMake.

**Produces:** `MuPDF::MuPDF` and working valid-PDF `open/page_count/close`.

- [ ] **Step 1: Commit deterministic 1-page and 2-page fixtures**

They contain no fonts, JavaScript, attachments, or external resources. Verify with MuPDF 1.28.2:

```bash
mutool info tests/fixtures/one-page.pdf
mutool info tests/fixtures/two-page.pdf
```

Expected counts: 1 and 2.

- [ ] **Step 2: Add RED lifecycle assertions**

```c
extractpdf_document *doc = NULL;
int pages = -1;
assert(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
assert(doc != NULL);
assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
assert(pages == 1);
extractpdf_close(doc);

doc = NULL;
pages = -1;
assert(extractpdf_open(TWO_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
assert(pages == 2);
extractpdf_close(doc);
extractpdf_close(NULL);
```

`ONE_PAGE_PDF` and `TWO_PAGE_PDF` are absolute paths supplied by CMake compile definitions.

Expected RED: undefined lifecycle symbols.

- [ ] **Step 3: Implement `FindMuPDF.cmake`**

Start with:

```cmake
set(MUPDF_ROOT "" CACHE PATH "MuPDF 1.28.2 root")
find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h HINTS "${MUPDF_ROOT}/include")
```

Windows mode: locate `${MUPDF_ROOT}/platform/win32/x64/Release/mupdfcpp64.lib`, create imported `MuPDF::MuPDF`, expose `MUPDF_INCLUDE_DIR`, and add `FZ_DLL_CLIENT` as an interface compile definition.

Linux/macOS mode: locate `libmupdf.a` and `libmupdfthird.a` under `${MUPDF_ROOT}/build/release` or `${MUPDF_ROOT}/lib`; expose both as `INTERFACE_LINK_LIBRARIES`, plus `m` on non-Windows.

Use `find_package_handle_standard_args` so configuration fails clearly when required headers/libraries are missing. Project CMake never downloads MuPDF.

- [ ] **Step 4: Add the private handle**

`src/internal.h`:

```c
#include <mupdf/fitz.h>
#include <extractpdf/extractpdf.h>

struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};

extractpdf_status extractpdf_status_from_mupdf(int code);
```

- [ ] **Step 5: Implement valid lifecycle**

`extractpdf_open` validates `filename`, non-empty path, and `out_document`; when `out_document` is non-NULL set `*out_document = NULL` before allocation. Allocate with `calloc`. `fz_new_context(NULL, NULL, FZ_STORE_DEFAULT)` returning NULL maps to `EXTRACTPDF_ERROR_NOMEM`.

Inside `fz_try`: `fz_register_document_handlers(ctx)`, `fz_open_document(ctx, filename)`, `fz_needs_password`, and when needed `fz_authenticate_password(ctx, doc, password ? password : "")`. For Task 2, wrong/no password may return `EXTRACTPDF_ERROR_PASSWORD`; Task 3 locks all error mappings.

`extractpdf_page_count` validates both arguments, calls `fz_count_pages` inside `fz_try/fz_catch`, and writes `*out_page_count` only on success.

`extractpdf_close`:

```c
if (!document) return;
if (document->doc) fz_drop_document(document->ctx, document->doc);
if (document->ctx) fz_drop_context(document->ctx);
free(document);
```

- [ ] **Step 6: Link `extractpdf` privately to `MuPDF::MuPDF`, then verify GREEN**

```bash
cmake -S . -B build -DMUPDF_ROOT=/absolute/path/to/mupdf-1.28.2
cmake --build build
ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt cmake src tests
git commit -m "feat: add MuPDF document lifecycle"
```

---

### Task 3: Error boundaries, passwords, malformed input, handle isolation

**Files:** Modify `src/internal.h`, `src/document.c`, `tests/test_document.c`, `tests/CMakeLists.txt`; create encrypted/truncated fixtures.

- [ ] **Step 1: Generate the encrypted fixture with MuPDF 1.28.2**

```bash
mutool clean -E aes-256 -O owner-pass -U user-pass \
  tests/fixtures/one-page.pdf tests/fixtures/encrypted-one-page.pdf
mutool info tests/fixtures/encrypted-one-page.pdf
# expected non-zero: password required
mutool info -p user-pass tests/fixtures/encrypted-one-page.pdf
# expected one page
```

- [ ] **Step 2: Add a malformed fixture that MuPDF 1.28.2 rejects**

Start with `%PDF-1.7`, truncate in the middle of an indirect object, then verify:

```bash
mutool info tests/fixtures/truncated.pdf
```

Expected: non-zero. Do not accept a fixture that MuPDF repairs and opens.

- [ ] **Step 3: Add RED argument/error/password tests**

```c
extractpdf_document *doc = (extractpdf_document *)0x1;
int pages = 123;

assert(extractpdf_open(NULL, NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
assert(doc == NULL);
assert(extractpdf_open("", NULL, &doc) == EXTRACTPDF_ERROR_ARGUMENT);
assert(doc == NULL);
assert(extractpdf_open(ONE_PAGE_PDF, NULL, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
assert(extractpdf_open(MISSING_PDF, NULL, &doc) == EXTRACTPDF_ERROR_IO);
assert(doc == NULL);
assert(extractpdf_open(ENCRYPTED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_PASSWORD);
assert(doc == NULL);
assert(extractpdf_open(ENCRYPTED_PDF, "wrong", &doc) == EXTRACTPDF_ERROR_PASSWORD);
assert(doc == NULL);
assert(extractpdf_open(ENCRYPTED_PDF, "user-pass", &doc) == EXTRACTPDF_OK);
extractpdf_close(doc);
doc = NULL;
assert(extractpdf_open(TRUNCATED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_FORMAT);
assert(doc == NULL);
assert(extractpdf_page_count(NULL, &pages) == EXTRACTPDF_ERROR_ARGUMENT);

assert(extractpdf_open(ONE_PAGE_PDF, NULL, &doc) == EXTRACTPDF_OK);
assert(extractpdf_page_count(doc, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
extractpdf_close(doc);
```

Also add 100 repeated open/count/close iterations and an interleaved two-handle test proving counts remain 1 and 2 independently.

- [ ] **Step 4: Implement the stable translator**

```c
extractpdf_status extractpdf_status_from_mupdf(int code)
{
    switch (code) {
    case FZ_ERROR_ARGUMENT: return EXTRACTPDF_ERROR_ARGUMENT;
    case FZ_ERROR_UNSUPPORTED: return EXTRACTPDF_ERROR_UNSUPPORTED;
    case FZ_ERROR_FORMAT:
    case FZ_ERROR_SYNTAX: return EXTRACTPDF_ERROR_FORMAT;
    case FZ_ERROR_SYSTEM: return EXTRACTPDF_ERROR_IO;
    default: return EXTRACTPDF_ERROR_MUPDF;
    }
}
```

Wrapper allocation and `fz_new_context` failure map directly to `EXTRACTPDF_ERROR_NOMEM`.

- [ ] **Step 5: Make open failure unwinding explicit**

Locals modified across MuPDF exception boundaries are declared before `fz_try` and protected with `fz_var` when required by MuPDF. On catch: capture `fz_caught(ctx)`, drop any opened `fz_document`, drop context, free wrapper, leave `*out_document == NULL`, and return the translated status. Wrong/missing password is a normal return path: unwind and return `EXTRACTPDF_ERROR_PASSWORD`.

- [ ] **Step 6: GREEN + sanitizer**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-asan -DMUPDF_ROOT=/absolute/path/to/mupdf-1.28.2 \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Expected: all tests pass and no sanitizer diagnostics from ExtractPDF-owned code.

- [ ] **Step 7: Commit**

```bash
git add src tests
git commit -m "test: harden document error boundaries"
```

---

### Task 4: UTF-8 paths and shared/static ABI construction

**Files:** Modify `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_document.c`.

- [ ] **Step 1: Create the UTF-8 test fixture path at configure time**

In `tests/CMakeLists.txt`:

```cmake
set(UTF8_PDF "${CMAKE_CURRENT_BINARY_DIR}/extractpdf-测试.pdf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf" "${UTF8_PDF}" COPYONLY)
```

Pass the absolute path to `test_document.c` as `UTF8_PDF`. Do not use `fopen` in the test helper, so the test measures ExtractPDF/MuPDF UTF-8 path behavior rather than CRT locale behavior.

- [ ] **Step 2: Add RED assertion**

```c
doc = NULL;
pages = -1;
assert(extractpdf_open(UTF8_PDF, NULL, &doc) == EXTRACTPDF_OK);
assert(extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK);
assert(pages == 1);
extractpdf_close(doc);
```

- [ ] **Step 3: Make shared/static mode explicit**

```cmake
if(BUILD_SHARED_LIBS)
  target_compile_definitions(extractpdf PRIVATE EXTRACTPDF_BUILDING_LIBRARY PUBLIC EXTRACTPDF_SHARED)
endif()
```

ExtractPDF implementation must not add ANSI file-opening code; `fz_open_document` receives the UTF-8 public path unchanged.

- [ ] **Step 4: Verify both modes**

```bash
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF -DMUPDF_ROOT=...
cmake --build build-static
ctest --test-dir build-static --output-on-failure
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON -DMUPDF_ROOT=...
cmake --build build-shared
ctest --test-dir build-shared --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests
git commit -m "test: verify UTF-8 paths and shared ABI"
```

---

### Task 5: Exact-version cross-platform CI and README

**Files:** Create `.github/workflows/ci.yml`; modify `README.md`.

- [ ] **Step 1: Linux/macOS dependency setup**

Each Unix job:

```bash
git clone --branch 1.28.2 --depth 1 --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git "$RUNNER_TEMP/mupdf"
make -C "$RUNNER_TEMP/mupdf" -j2 build=release libs
cmake -S . -B build -DMUPDF_ROOT="$RUNNER_TEMP/mupdf" -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Linux also runs the sanitizer configuration from Task 3.

- [ ] **Step 2: Windows x64 dependency setup**

Checkout the same 1.28.2 tag recursively and run:

```powershell
msbuild "$env:RUNNER_TEMP\mupdf\platform\win32\mupdf.sln" /m /t:mupdfcpp /p:Configuration=Release /p:Platform=x64
cmake -S . -B build -A x64 -DMUPDF_ROOT="$env:RUNNER_TEMP\mupdf" -DBUILD_SHARED_LIBS=ON
cmake --build build --config Release
$env:PATH = "$env:RUNNER_TEMP\mupdf\platform\win32\x64\Release;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

Expected MuPDF client artifacts are `platform/win32/x64/Release/mupdfcpp64.lib` and `mupdfcpp64.dll`; client compilation uses `FZ_DLL_CLIENT`.

- [ ] **Step 3: README contract**

README states that root `libpdf.c` is historical MuPDF 1.3 POC code and not the supported v2 API; MuPDF 1.28.2 is the Phase 1 baseline; `MUPDF_ROOT` is required; paths are UTF-8; Phase 1 is single-threaded; AGPL-3.0-or-later is the open-source baseline; commercial MuPDF licensing is a separate explicit arrangement; text/image APIs are intentionally not in Phase 1.

Include this minimal example:

```c
extractpdf_document *doc = NULL;
int pages = 0;
if (extractpdf_open("file.pdf", NULL, &doc) == EXTRACTPDF_OK) {
    extractpdf_page_count(doc, &pages);
    extractpdf_close(doc);
}
```

- [ ] **Step 4: Commit and push**

```bash
git add .github/workflows/ci.yml README.md
git commit -m "ci: verify ExtractPDF v2 foundation"
git push
```

- [ ] **Step 5: Exact-head acceptance**

Record `git rev-parse HEAD`. Required Windows/Linux/macOS jobs must all be green on that exact SHA. Do not use an older successful run as evidence.

---

## Final Phase 1 verification

- [ ] Root `LICENSE` contains GNU AGPL v3 text and README identifies AGPL-3.0-or-later.
- [ ] Public header contains no MuPDF include/type.
- [ ] Search finds no mutable file-scope/thread-local state in `src/`.
- [ ] CTest covers null arguments, missing file, 1/2-page counts, no/wrong/correct password, malformed PDF, repeated lifecycle, interleaved handles, `close(NULL)`, and UTF-8 path.
- [ ] Linux sanitizer run is clean.
- [ ] Static and shared ExtractPDF modes compile.
- [ ] Windows/Linux/macOS jobs pass on one exact head SHA.
- [ ] `libpdf.c` is byte-for-byte unchanged.
- [ ] Phase 2 text work starts only after every item above is green.
