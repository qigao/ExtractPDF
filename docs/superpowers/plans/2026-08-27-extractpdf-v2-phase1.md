# ExtractPDF v2 Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the ExtractPDF v2 foundation: a stable C11 ABI over MuPDF 1.28.2 with safe document lifecycle, password handling, page counting, deterministic fixtures, CTest, and exact-head Windows/Linux/macOS CI.

**Architecture:** Keep MuPDF entirely behind an opaque `extractpdf_document`. Every handle owns one `fz_context` and one `fz_document`; no mutable process-global or thread-local state is allowed. MuPDF exceptions are caught at the wrapper boundary and translated to the fixed `extractpdf_status` enum.

**Tech Stack:** C11, CMake 3.20+, CTest, MuPDF 1.28.2 public C API, GitHub Actions; MSVC + MuPDF DLL client on Windows, static MuPDF libraries on Linux/macOS.

**Spec:** `docs/superpowers/specs/2026-08-27-extractpdf-v2-design.md`

## Global Constraints

- MuPDF baseline is exactly 1.28.2 for Phase 1 CI.
- Public headers contain no MuPDF types or headers.
- No mutable process-global or thread-local state in ExtractPDF-owned code.
- The foundation is single-threaded; multiple handles may be alive and used interleaved on one thread only.
- All MuPDF calls that can throw are contained by `fz_try` / `fz_always` / `fz_catch`.
- Public paths are UTF-8.
- CTest is the test entry point.
- Open-source repository license is AGPL-3.0-or-later.
- Do not move or edit legacy `libpdf.c` during Phase 1.
- Treat warnings as errors for ExtractPDF-owned code.

---

## File map

- `LICENSE` — full GNU AGPL v3 license text.
- `CMakeLists.txt` — project, library target, dependency discovery, warning policy, tests.
- `cmake/FindMuPDF.cmake` — creates imported target `MuPDF::MuPDF` from `MUPDF_ROOT`.
- `include/extractpdf/extractpdf.h` — complete Phase 1 public ABI.
- `src/internal.h` — private opaque handle definition and error translation declaration.
- `src/status.c` — status-string mapping only.
- `src/document.c` — open/password/page-count/close lifecycle only.
- `tests/CMakeLists.txt` — native test target and fixture path definitions.
- `tests/test_status.c` — ABI/status contract tests that do not require a PDF.
- `tests/test_document.c` — lifecycle/error/isolation/UTF-8 path tests.
- `tests/fixtures/one-page.pdf` — deterministic valid unencrypted fixture.
- `tests/fixtures/two-page.pdf` — deterministic valid two-page fixture.
- `tests/fixtures/encrypted-one-page.pdf` — AES-256, user password `user-pass`, owner password `owner-pass`.
- `tests/fixtures/truncated.pdf` — intentionally malformed/truncated input.
- `.github/workflows/ci.yml` — exact MuPDF 1.28.2 dependency build plus CMake/CTest matrix.
- `README.md` — v2 build/usage/thread/license contract; legacy code explicitly historical.

---

### Task 1: Establish license, public ABI, and dependency-free status tests

**Files:**
- Create: `LICENSE`
- Create: `CMakeLists.txt`
- Create: `include/extractpdf/extractpdf.h`
- Create: `src/status.c`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_status.c`

**Interfaces:**
- Produces the exact public types and symbols used by every later task:
  - `typedef struct extractpdf_document extractpdf_document;`
  - `extractpdf_status extractpdf_open(const char *, const char *, extractpdf_document **);`
  - `extractpdf_status extractpdf_page_count(extractpdf_document *, int *);`
  - `const char *extractpdf_status_string(extractpdf_status);`
  - `void extractpdf_close(extractpdf_document *);`

- [ ] **Step 1: Add AGPL-3.0-or-later root license**

Use the unmodified GNU Affero General Public License version 3 text from `https://www.gnu.org/licenses/agpl-3.0.txt`. Do not invent a custom license header or claim that a DLL wrapper changes MuPDF licensing.

- [ ] **Step 2: Write the public header exactly from the approved spec**

`include/extractpdf/extractpdf.h` must contain the enum values 0 through 7 and the four declarations above. Keep the `_WIN32` / `EXTRACTPDF_SHARED` / `EXTRACTPDF_BUILDING_LIBRARY` export macro from the spec. Do not include any `mupdf/*` header.

- [ ] **Step 3: Write failing status tests**

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

- [ ] **Step 4: Add the minimum CMake needed to run only `test_status`**

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

Tests:

```cmake
add_executable(extractpdf_test_status test_status.c)
target_link_libraries(extractpdf_test_status PRIVATE ExtractPDF::ExtractPDF)
add_test(NAME extractpdf.status COMMAND extractpdf_test_status)
```

- [ ] **Step 5: Run RED before implementing `status.c`**

Run:

```bash
cmake -S . -B build
cmake --build build
```

Expected: link failure for undefined `extractpdf_status_string`.

- [ ] **Step 6: Implement the minimal status mapping**

`src/status.c` is a switch returning the exact immutable strings asserted above, with default `"unknown error"`.

- [ ] **Step 7: Run GREEN**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `extractpdf.status` passes.

- [ ] **Step 8: Commit**

```bash
git add LICENSE CMakeLists.txt include src/status.c tests

git commit -m "feat: establish ExtractPDF v2 public ABI"
```

---

### Task 2: Discover MuPDF 1.28.2 and implement the happy-path document lifecycle

**Files:**
- Create: `cmake/FindMuPDF.cmake`
- Create: `src/internal.h`
- Create: `src/document.c`
- Create: `tests/fixtures/one-page.pdf`
- Create: `tests/fixtures/two-page.pdf`
- Create: `tests/test_document.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes the Phase 1 public header.
- Produces imported target `MuPDF::MuPDF` and working `open/page_count/close` on valid PDFs.

- [ ] **Step 1: Add deterministic valid fixtures**

Generate and commit two tiny PDFs whose only purpose is page counting. They must contain no fonts, JavaScript, attachments, or external resources. Verify before committing:

```bash
mutool info tests/fixtures/one-page.pdf
mutool info tests/fixtures/two-page.pdf
```

Expected page counts: 1 and 2.

- [ ] **Step 2: Add failing happy-path tests**

`tests/test_document.c` must include these assertions:

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

Use compile definitions `ONE_PAGE_PDF` and `TWO_PAGE_PDF` with absolute source-fixture paths from CMake; do not depend on the test working directory.

- [ ] **Step 3: Run RED**

Expected: undefined lifecycle symbols.

- [ ] **Step 4: Implement `FindMuPDF.cmake` with one imported target**

Required discovery contract:

```cmake
set(MUPDF_ROOT "" CACHE PATH "MuPDF 1.28.2 root")
find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h HINTS "${MUPDF_ROOT}/include")
```

On Windows, first look for `mupdfcpp64.lib` under `${MUPDF_ROOT}/platform/win32/x64/Release` and create `MuPDF::MuPDF` as an imported library with `INTERFACE_COMPILE_DEFINITIONS FZ_DLL_CLIENT`. This matches MuPDF's Windows DLL-client contract and keeps the C API inside the official `mupdfcpp64.dll`.

On Linux/macOS, find `libmupdf.a` and `libmupdfthird.a` under `${MUPDF_ROOT}/build/release` or `${MUPDF_ROOT}/lib`, and expose both through `INTERFACE_LINK_LIBRARIES`; add `m` on non-Windows platforms.

Fail configuration with `find_package_handle_standard_args` if headers or required libraries are absent. Do not download MuPDF from project CMake.

- [ ] **Step 5: Add the private handle**

`src/internal.h`:

```c
#include <mupdf/fitz.h>
#include <extractpdf/extractpdf.h>

struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};
```

No other mutable state is added.

- [ ] **Step 6: Implement happy-path lifecycle behind exception boundaries**

`extractpdf_open` must:

1. reject `filename == NULL`, empty filename, or `out_document == NULL` with `EXTRACTPDF_ERROR_ARGUMENT`;
2. set `*out_document = NULL` before any allocation;
3. `calloc` the wrapper;
4. call `fz_new_context(NULL, NULL, FZ_STORE_DEFAULT)` and return `EXTRACTPDF_ERROR_NOMEM` if it returns NULL;
5. inside `fz_try`, call `fz_register_document_handlers(ctx)` then `fz_open_document(ctx, filename)`;
6. if `fz_needs_password(ctx, doc)` is true, authenticate with `password ? password : ""`; failure is handled in Task 3;
7. return the handle only after successful open/authentication.

`extractpdf_page_count` validates both pointers, runs `fz_count_pages` inside `fz_try/fz_catch`, writes the output only on success, and never allows a MuPDF exception to escape.

`extractpdf_close` is null-safe and always drops document before context:

```c
if (!document) return;
if (document->doc) fz_drop_document(document->ctx, document->doc);
if (document->ctx) fz_drop_context(document->ctx);
free(document);
```

- [ ] **Step 7: Link MuPDF and run GREEN**

```bash
cmake -S . -B build -DMUPDF_ROOT=/absolute/path/to/mupdf-1.28.2
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: status tests and valid lifecycle tests pass.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt cmake include src tests

git commit -m "feat: add MuPDF document lifecycle"
```

---

### Task 3: Lock argument, I/O, password, malformed-input, and isolation behavior

**Files:**
- Modify: `src/internal.h`
- Modify: `src/document.c`
- Modify: `tests/test_document.c`
- Create: `tests/fixtures/encrypted-one-page.pdf`
- Create: `tests/fixtures/truncated.pdf`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces stable error translation and all Phase 1 lifecycle edge-case semantics.

- [ ] **Step 1: Generate and commit the encrypted fixture with MuPDF 1.28.2**

From the committed one-page fixture:

```bash
mutool clean -E aes-256 -O owner-pass -U user-pass \
  tests/fixtures/one-page.pdf tests/fixtures/encrypted-one-page.pdf
```

Verify:

```bash
mutool info tests/fixtures/encrypted-one-page.pdf
# Expected: password failure
mutool info -p user-pass tests/fixtures/encrypted-one-page.pdf
# Expected: one page
```

- [ ] **Step 2: Add malformed fixture**

`tests/fixtures/truncated.pdf` is intentionally incomplete and must begin with `%PDF-1.7` but end in the middle of an indirect object. Verify `mutool info` fails non-zero.

- [ ] **Step 3: Add RED tests for all stable error cases**

Required assertions:

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

assert(extractpdf_open(TRUNCATED_PDF, NULL, &doc) == EXTRACTPDF_ERROR_FORMAT);
assert(doc == NULL);

assert(extractpdf_page_count(NULL, &pages) == EXTRACTPDF_ERROR_ARGUMENT);
assert(extractpdf_page_count(doc, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
```

Also add loops for 100 repeated open/count/close operations and a two-handle interleaving test:

```c
extractpdf_document *a = NULL, *b = NULL;
int a_pages = 0, b_pages = 0;
assert(extractpdf_open(ONE_PAGE_PDF, NULL, &a) == EXTRACTPDF_OK);
assert(extractpdf_open(TWO_PAGE_PDF, NULL, &b) == EXTRACTPDF_OK);
assert(extractpdf_page_count(b, &b_pages) == EXTRACTPDF_OK);
assert(extractpdf_page_count(a, &a_pages) == EXTRACTPDF_OK);
assert(a_pages == 1 && b_pages == 2);
extractpdf_close(a);
extractpdf_close(b);
```

- [ ] **Step 4: Add one private error translator**

`src/internal.h` declares:

```c
extractpdf_status extractpdf_status_from_mupdf(int code);
```

Implement mapping in `document.c` or a focused private function:

```c
switch (code) {
case FZ_ERROR_ARGUMENT: return EXTRACTPDF_ERROR_ARGUMENT;
case FZ_ERROR_UNSUPPORTED: return EXTRACTPDF_ERROR_UNSUPPORTED;
case FZ_ERROR_FORMAT:
case FZ_ERROR_SYNTAX: return EXTRACTPDF_ERROR_FORMAT;
case FZ_ERROR_SYSTEM: return EXTRACTPDF_ERROR_IO;
default: return EXTRACTPDF_ERROR_MUPDF;
}
```

Wrapper allocation/context-creation failures return `EXTRACTPDF_ERROR_NOMEM` directly; do not guess OOM from MuPDF error strings.

- [ ] **Step 5: Make every failure unwind deterministically**

In `extractpdf_open`, use locals declared before `fz_try` and protected with `fz_var` where MuPDF requires it. The catch path captures `fz_caught(ctx)`, drops any opened document, drops context, frees wrapper, leaves `*out_document == NULL`, and returns the translated status.

Password rejection is not a thrown exception: if `fz_needs_password` and `fz_authenticate_password` returns 0, unwind and return `EXTRACTPDF_ERROR_PASSWORD`.

- [ ] **Step 6: Run GREEN plus sanitizer on Linux**

```bash
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -DMUPDF_ROOT=/absolute/path/to/mupdf-1.28.2 \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Expected: all tests pass with no sanitizer diagnostics from ExtractPDF-owned code.

- [ ] **Step 7: Commit**

```bash
git add src tests

git commit -m "test: harden document error boundaries"
```

---

### Task 4: Prove UTF-8 path behavior and shared-library ABI construction

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/test_document.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces the platform behavior required for .NET P/Invoke without adding a .NET wrapper yet.

- [ ] **Step 1: Add UTF-8-path RED test**

At test runtime copy `one-page.pdf` to a filename containing non-ASCII UTF-8, for example `extractpdf-测试.pdf`, using CMake before the test or a small portable helper in the test. Open the copied path through `extractpdf_open` and assert page count 1. Delete it after the test.

On Windows, do not call ANSI `fopen` in ExtractPDF implementation. The test exercises MuPDF's documented UTF-8 filename path through `fz_open_document`.

- [ ] **Step 2: Make shared/static construction explicit**

Use CMake's `BUILD_SHARED_LIBS` convention and set:

```cmake
if(BUILD_SHARED_LIBS)
    target_compile_definitions(extractpdf PRIVATE EXTRACTPDF_BUILDING_LIBRARY PUBLIC EXTRACTPDF_SHARED)
endif()
```

The same public header must compile in both modes.

- [ ] **Step 3: Build and test both modes locally where possible**

```bash
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF -DMUPDF_ROOT=...
cmake --build build-static
ctest --test-dir build-static --output-on-failure

cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON -DMUPDF_ROOT=...
cmake --build build-shared
ctest --test-dir build-shared --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tests

git commit -m "test: verify UTF-8 paths and shared ABI"
```

---

### Task 5: Add exact-version cross-platform CI and update README

**Files:**
- Create: `.github/workflows/ci.yml`
- Modify: `README.md`

**Interfaces:**
- Produces the Phase 1 acceptance proof on one exact commit.

- [ ] **Step 1: Add Linux/macOS MuPDF setup**

For both Unix jobs:

```bash
git clone --branch 1.28.2 --depth 1 --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git "$RUNNER_TEMP/mupdf"
make -C "$RUNNER_TEMP/mupdf" -j2 build=release libs
```

Configure ExtractPDF with:

```bash
cmake -S . -B build -DMUPDF_ROOT="$RUNNER_TEMP/mupdf" -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Linux additionally runs the sanitizer configuration from Task 3.

- [ ] **Step 2: Add Windows x64 MuPDF DLL-client setup**

Checkout the same `1.28.2` tag recursively, then build the official solution target:

```powershell
msbuild "$env:RUNNER_TEMP\mupdf\platform\win32\mupdf.sln" /m /t:mupdfcpp /p:Configuration=Release /p:Platform=x64
```

The expected client artifacts are under `platform/win32/x64/Release/`, including `mupdfcpp64.lib` and `mupdfcpp64.dll`. `FindMuPDF.cmake` must add `FZ_DLL_CLIENT` for this mode.

Configure/build/test ExtractPDF with Visual Studio x64 and ensure the MuPDF DLL directory is on `PATH` for CTest:

```powershell
cmake -S . -B build -A x64 -DMUPDF_ROOT="$env:RUNNER_TEMP\mupdf" -DBUILD_SHARED_LIBS=ON
cmake --build build --config Release
$env:PATH = "$env:RUNNER_TEMP\mupdf\platform\win32\x64\Release;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

- [ ] **Step 3: Keep workflow dependency acquisition outside project CMake**

Do not use `FetchContent`, git submodules, or vendored MuPDF in `CMakeLists.txt`. CI owns checkout/build; project CMake consumes `MUPDF_ROOT` only.

- [ ] **Step 4: Rewrite README for v2**

README must state:

- legacy `libpdf.c` is historical MuPDF 1.3 POC code and not the supported v2 API;
- MuPDF 1.28.2 is the tested Phase 1 baseline;
- minimal C usage:

```c
extractpdf_document *doc = NULL;
int pages = 0;
if (extractpdf_open("file.pdf", NULL, &doc) == EXTRACTPDF_OK) {
    extractpdf_page_count(doc, &pages);
    extractpdf_close(doc);
}
```

- `MUPDF_ROOT` build instructions;
- UTF-8 paths;
- single-thread foundation contract;
- AGPL-3.0-or-later baseline and commercial-MuPDF-license caveat;
- text/image APIs are intentionally not part of Phase 1.

- [ ] **Step 5: Push and inspect exact-head workflow results**

Record the head SHA after the README/CI commit. All required jobs must be green on that exact SHA; do not cite an earlier workflow run.

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/ci.yml README.md

git commit -m "ci: verify ExtractPDF v2 foundation"
```

---

## Final Phase 1 verification

- [ ] Public header has no `mupdf` include or MuPDF type.
- [ ] Search finds no mutable file-scope state in `src/`.
- [ ] `ctest` covers null args, missing file, one/two page counts, no/wrong/correct password, malformed PDF, repeated lifecycle, interleaved handles, close(NULL), UTF-8 path.
- [ ] Linux sanitizer run is clean.
- [ ] Static and shared ExtractPDF build modes compile.
- [ ] Windows, Linux, macOS jobs are green on the same head SHA.
- [ ] Root `LICENSE` is AGPL v3 text and README says AGPL-3.0-or-later.
- [ ] Legacy `libpdf.c` is unchanged.
- [ ] Phase 2 text work does not start until all checks above are green.
