# ExtractPDF Immutable Output File Save Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `extractpdf_output_save_file(...)` so an immutable `extractpdf_output` can be written to a UTF-8 filesystem path without moving filename policy into PDF composition.

**Architecture:** The new API is a terminal adapter over the existing immutable `data + size` snapshot. It performs no MuPDF work: Windows strictly converts UTF-8 to UTF-16 before wide-character binary open, while Linux/macOS use the UTF-8 byte path directly; all platforms exact-write, flush, close, and return the existing ExtractPDF status vocabulary.

**Tech Stack:** C11, stdio, Win32 `MultiByteToWideChar` on Windows, CMake 3.20+, CTest, GitHub Actions, Linux ASan/UBSan, Windows DLL build, macOS static build.

**Spec:** `docs/superpowers/specs/2026-08-28-extractpdf-output-save-file-design.md`

## Global Constraints

- Stack on #27 / PR #28 exact GREEN head `a6d35b6f5d68207cf9c628b2b38c35addb868d82`; do not retarget during implementation unless the existing stack is explicitly integrated first.
- Public ABI is exactly:

```c
EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);
```

- The function is a terminal file adapter only. It must not invoke MuPDF, create an `fz_context`, reparse/reserialize PDF bytes, or alter export/merge composition behavior.
- `output` is non-consuming and immutable on success and failure. The caller continues to own it until `extractpdf_drop_output(...)`.
- `output == NULL`, `filename == NULL`, or `filename[0] == '\0'` returns `EXTRACTPDF_ERROR_ARGUMENT`.
- Windows public paths are strict UTF-8: invalid UTF-8 returns `EXTRACTPDF_ERROR_ARGUMENT`; temporary UTF-16 buffer allocation failure returns `EXTRACTPDF_ERROR_NOMEM`.
- Windows opens with a wide-character binary write path; do not use narrow `fopen()` for public UTF-8 paths.
- Linux/macOS use `fopen(filename, "wb")` and do not add a cross-platform UTF-8 validator.
- Missing targets are created; existing targets are truncated and overwritten; parent directories are not created.
- A successful save requires every `output->size` byte to be written, `fflush(...) == 0`, and `fclose(...) == 0`.
- Open/write/flush/close failures return `EXTRACTPDF_ERROR_IO`.
- V1 is deliberately non-atomic and non-durable: no temp-file/rename transaction, rollback, `fsync`, `fdatasync`, `FlushFileBuffers`, `F_FULLFSYNC`, ACL policy, symlink policy, or power-loss guarantee.
- On I/O failure the target may be absent, empty, truncated, or partially written; the input `extractpdf_output` remains valid and reusable.
- Do not change `struct extractpdf_output`, `src/pdf_export.c`, `src/pdf_output.c`, `src/pdf_merge.c`, or any PDF preservation/composition semantics.
- No callback sink, file-handle API, output options struct, save session, or directory-creation helper belongs in this slice.
- MuPDF remains pinned to 1.28.2 through the existing vcpkg model; no dependency or workflow-version changes are required.
- Development is Linux-first. Because this adds a public DLL export and a Windows-specific UTF-8→UTF-16 production path, final acceptance requires Linux normal + ASan/UBSan and then same-head Linux/macOS/Windows `full-ci`.
- Do not merge PRs without explicit user authorization.

---

## File Structure

```text
include/extractpdf/extractpdf.h
    public output-save declaration only

src/output_file.c
    argument validation
    platform-specific UTF-8 file open
    exact byte-write loop
    flush + close error handling
    no MuPDF calls

CMakeLists.txt
    register src/output_file.c

tests/test_output_file.c
    public save-file contract
    ASCII exact-byte/truncate proof
    UTF-8 filename proof
    deterministic missing-parent I/O failure
    Windows invalid-UTF-8 proof
    output ownership preservation

tests/CMakeLists.txt
    register extractpdf.output_file
    define build-directory test paths
    add MSVC /utf-8
    add Windows DLL copy target
```

No new PDF fixture is created.

---

### Task 1: Add the strict save-file RED

**Files:**
- Create: `tests/test_output_file.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `extractpdf_open`, `extractpdf_export_pages`, `extractpdf_output_data`, `extractpdf_drop_output`, `extractpdf_page_count`, `extractpdf_load_page`, `extractpdf_page_bounds`, `extractpdf_extract_text`.
- Produces: one deterministic CTest `extractpdf.output_file` that specifies the public file-save contract before production code exists.

- [ ] **Step 1: Create `tests/test_output_file.c`**

Use this complete test structure:

```c
#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int write_stale_file(
    const char *path,
    const unsigned char *data,
    size_t size)
{
    static const unsigned char stale_tail[] =
        "THIS-STALE-TRAILING-DATA-MUST-BE-TRUNCATED";
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    if (fwrite(stale_tail, 1, sizeof(stale_tail), file) != sizeof(stale_tail)) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void expect_exact_file_bytes(
    const char *path,
    const unsigned char *expected,
    size_t expected_size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *actual = NULL;
    int next;

    CHECK(file != NULL);
    CHECK(expected_size > 0);

    actual = (unsigned char *)malloc(expected_size);
    CHECK(actual != NULL);
    CHECK(fread(actual, 1, expected_size, file) == expected_size);
    CHECK(memcmp(actual, expected, expected_size) == 0);

    next = fgetc(file);
    CHECK(next == EOF);
    CHECK(feof(file) != 0);
    CHECK(ferror(file) == 0);
    CHECK(fclose(file) == 0);
    free(actual);
}

static void expect_page(
    extractpdf_document *document,
    int page_index,
    const char *needle,
    float width,
    float height)
{
    extractpdf_page *page = NULL;
    extractpdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;

    CHECK(extractpdf_load_page(document, page_index, &page) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_bounds(page, &bounds) == EXTRACTPDF_OK);
    CHECK(bounds.x0 == 0.0f);
    CHECK(bounds.y0 == 0.0f);
    CHECK(bounds.x1 == width);
    CHECK(bounds.y1 == height);
    CHECK(extractpdf_extract_text(page, &text, &text_size) == EXTRACTPDF_OK);
    CHECK(text != NULL);
    CHECK(strstr(text, needle) != NULL);

    extractpdf_free(text);
    extractpdf_drop_page(page);
}

static void expect_saved_pdf(
    const char *path,
    const char *first_text,
    float first_width,
    float first_height,
    const char *second_text,
    float second_width,
    float second_height)
{
    extractpdf_document *document = NULL;
    int page_count = 0;

    CHECK(extractpdf_open(path, NULL, &document) == EXTRACTPDF_OK);
    CHECK(extractpdf_page_count(document, &page_count) == EXTRACTPDF_OK);
    CHECK(page_count == 2);
    expect_page(document, 0, first_text, first_width, first_height);
    expect_page(document, 1, second_text, second_width, second_height);
    extractpdf_close(document);
}

static void remove_missing_parent_if_empty(void)
{
    (void)remove(MISSING_PARENT_PDF);
#ifdef _WIN32
    (void)_rmdir(MISSING_PARENT_DIR);
#else
    (void)rmdir(MISSING_PARENT_DIR);
#endif
}

int main(void)
{
    extractpdf_document *source = NULL;
    extractpdf_output *output = NULL;
    const unsigned char *before_data = NULL;
    const unsigned char *after_data = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
    int indices[] = {2, 0};

    (void)remove(ASCII_OUTPUT_PDF);
    remove_missing_parent_if_empty();

    CHECK(extractpdf_output_save_file(NULL, ASCII_OUTPUT_PDF) ==
          EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(extractpdf_open(COMPOSITION_PDF, NULL, &source) == EXTRACTPDF_OK);
    CHECK(extractpdf_export_pages(source, indices, 2, &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);
    extractpdf_close(source);
    source = NULL;

    CHECK(extractpdf_output_data(output, &before_data, &before_size) ==
          EXTRACTPDF_OK);
    CHECK(before_data != NULL);
    CHECK(before_size > 0);

    CHECK(extractpdf_output_save_file(output, NULL) ==
          EXTRACTPDF_ERROR_ARGUMENT);
    CHECK(extractpdf_output_save_file(output, "") ==
          EXTRACTPDF_ERROR_ARGUMENT);

    CHECK(write_stale_file(ASCII_OUTPUT_PDF, before_data, before_size));
    CHECK(extractpdf_output_save_file(output, ASCII_OUTPUT_PDF) ==
          EXTRACTPDF_OK);
    expect_exact_file_bytes(ASCII_OUTPUT_PDF, before_data, before_size);
    expect_saved_pdf(
        ASCII_OUTPUT_PDF,
        "PAGE-C", 300.0f, 150.0f,
        "PAGE-A", 200.0f, 200.0f);

    CHECK(extractpdf_output_save_file(output, UTF8_OUTPUT_PDF) ==
          EXTRACTPDF_OK);
    expect_saved_pdf(
        UTF8_OUTPUT_PDF,
        "PAGE-C", 300.0f, 150.0f,
        "PAGE-A", 200.0f, 200.0f);

    remove_missing_parent_if_empty();
    CHECK(extractpdf_output_save_file(output, MISSING_PARENT_PDF) ==
          EXTRACTPDF_ERROR_IO);

#ifdef _WIN32
    {
        const char invalid_utf8[] = { (char)0xC3, (char)0x28, '\0' };
        CHECK(extractpdf_output_save_file(output, invalid_utf8) ==
              EXTRACTPDF_ERROR_ARGUMENT);
    }
#endif

    CHECK(extractpdf_output_data(output, &after_data, &after_size) ==
          EXTRACTPDF_OK);
    CHECK(after_data != NULL);
    CHECK(after_size == before_size);
    CHECK(memcmp(after_data, before_data, before_size) == 0);

    extractpdf_drop_output(output);
    (void)remove(ASCII_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
```

The test deliberately does not use narrow Windows `fopen()` on `UTF8_OUTPUT_PDF`; it verifies that path only through the public UTF-8 `extractpdf_open(...)` API.

The `MISSING_PARENT_DIR` cleanup makes reruns deterministic: if an empty directory from an interrupted local run exists, remove it before requiring the missing-parent save to fail.

- [ ] **Step 2: Register the RED target in `tests/CMakeLists.txt`**

Add immediately after `extractpdf.pdf_merge`:

```cmake
set(OUTPUT_FILE_ASCII "${CMAKE_CURRENT_BINARY_DIR}/composition-save-output.pdf")
set(OUTPUT_FILE_UTF8 "${CMAKE_CURRENT_BINARY_DIR}/composition-save-测试.pdf")
set(OUTPUT_FILE_MISSING_PARENT_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/extractpdf-save-missing-parent")
set(OUTPUT_FILE_MISSING_PARENT
  "${OUTPUT_FILE_MISSING_PARENT_DIR}/output.pdf")

add_executable(extractpdf_test_output_file test_output_file.c)
target_link_libraries(extractpdf_test_output_file PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_output_file PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  ASCII_OUTPUT_PDF="${OUTPUT_FILE_ASCII}"
  UTF8_OUTPUT_PDF="${OUTPUT_FILE_UTF8}"
  MISSING_PARENT_DIR="${OUTPUT_FILE_MISSING_PARENT_DIR}"
  MISSING_PARENT_PDF="${OUTPUT_FILE_MISSING_PARENT}")
if(MSVC)
  target_compile_options(extractpdf_test_output_file PRIVATE /utf-8)
endif()
add_test(NAME extractpdf.output_file COMMAND extractpdf_test_output_file)
set_tests_properties(extractpdf.output_file PROPERTIES TIMEOUT 30)
```

Also append `extractpdf_test_output_file` to the existing `if(WIN32 AND BUILD_SHARED_LIBS)` target list so `extractpdf.dll` is copied beside the new test executable.

- [ ] **Step 3: Commit the test-only RED**

```bash
git add tests/test_output_file.c tests/CMakeLists.txt
git commit -m "test: define immutable output file save contract"
```

Do not add the public declaration, root CMake source registration, or `src/output_file.c` in this commit.

Record the exact commit SHA returned by the commit. This is the RED SHA for all later evidence.

- [ ] **Step 4: Open the draft stacked PR**

Create a draft PR with:

```text
base: feat/pdf-merge-outputs
head: feat/output-save-file
draft: true
tracks: #29 / #2
```

The body must state that the exact current head is intentional RED and that `extractpdf_output_save_file(...)` is not declared or implemented yet. Record the numeric PR number returned by GitHub and reuse that same PR throughout the remainder of the plan.

- [ ] **Step 5: Verify the RED is exactly the missing public save symbol**

Use the PR-triggered workflow on the exact RED SHA.

Expected Linux boundary:

```text
pinned MuPDF install                ✅
configure                           ✅
libextractpdf.a                     ✅
all pre-existing test targets       ✅
extractpdf_test_output_file         fails only because
                                    extractpdf_output_save_file is absent
```

Expected diagnostic class:

```text
implicit declaration of function 'extractpdf_output_save_file'
undefined reference to 'extractpdf_output_save_file'
```

If any existing test target fails, or the new test fails for fixture/path/test-code reasons, fix the RED before adding production code. The intended RED must be exact.

---

### Task 2: Implement the platform-aware terminal file adapter and turn RED GREEN

**Files:**
- Modify: `include/extractpdf/extractpdf.h`
- Create: `src/output_file.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: private `struct extractpdf_output { unsigned char *data; size_t size; }` from `src/internal.h`.
- Produces: public non-consuming `extractpdf_output_save_file(...)` with strict UTF-8 Windows path handling and exact stdio completion semantics.

- [ ] **Step 1: Add the public declaration**

In `include/extractpdf/extractpdf.h`, place the declaration after `extractpdf_output_data(...)` and before unrelated page APIs:

```c
EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);
```

Do not add a new status code, options struct, callback, public file handle, or Windows type.

- [ ] **Step 2: Create `src/output_file.c` with platform-specific open logic**

Create:

```c
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <stdint.h>
#include <windows.h>
#include <wchar.h>
#endif

static extractpdf_status extractpdf_open_output_file(
    const char *filename,
    FILE **out_file)
{
    *out_file = NULL;

#ifdef _WIN32
    {
        wchar_t *wide_filename = NULL;
        int wide_count;
        int converted;
        errno_t open_error;

        wide_count = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            filename,
            -1,
            NULL,
            0);
        if (wide_count <= 0)
            return EXTRACTPDF_ERROR_ARGUMENT;

        if ((size_t)wide_count > SIZE_MAX / sizeof(*wide_filename))
            return EXTRACTPDF_ERROR_NOMEM;

        wide_filename = (wchar_t *)malloc(
            (size_t)wide_count * sizeof(*wide_filename));
        if (wide_filename == NULL)
            return EXTRACTPDF_ERROR_NOMEM;

        converted = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            filename,
            -1,
            wide_filename,
            wide_count);
        if (converted <= 0) {
            free(wide_filename);
            return EXTRACTPDF_ERROR_ARGUMENT;
        }

        open_error = _wfopen_s(out_file, wide_filename, L"wb");
        free(wide_filename);

        if (open_error != 0 || *out_file == NULL)
            return EXTRACTPDF_ERROR_IO;
    }
#else
    *out_file = fopen(filename, "wb");
    if (*out_file == NULL)
        return EXTRACTPDF_ERROR_IO;
#endif

    return EXTRACTPDF_OK;
}
```

The file may include `internal.h` to access the private output snapshot. It must not include MuPDF headers directly and must not call any MuPDF API.

- [ ] **Step 3: Implement exact-write, flush, and close semantics in the same file**

Append:

```c
extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename)
{
    FILE *file = NULL;
    extractpdf_status status;
    size_t offset = 0;

    if (output == NULL || filename == NULL || filename[0] == '\0')
        return EXTRACTPDF_ERROR_ARGUMENT;

    status = extractpdf_open_output_file(filename, &file);
    if (status != EXTRACTPDF_OK)
        return status;

    while (offset < output->size) {
        size_t written = fwrite(
            output->data + offset,
            1,
            output->size - offset,
            file);

        if (written == 0) {
            status = EXTRACTPDF_ERROR_IO;
            break;
        }
        offset += written;
    }

    if (status == EXTRACTPDF_OK && fflush(file) != 0)
        status = EXTRACTPDF_ERROR_IO;

    if (fclose(file) != 0 && status == EXTRACTPDF_OK)
        status = EXTRACTPDF_ERROR_IO;

    return status;
}
```

Do not delete a partial target on failure. Do not retry by reopening. Do not call `fsync`, `FlushFileBuffers`, rename, or create parent directories.

The private output invariant already guarantees a non-empty complete PDF snapshot for library-created outputs, so do not add new public validation for `output->data` or `output->size`.

- [ ] **Step 4: Register `src/output_file.c` in root `CMakeLists.txt`**

Keep the source grouping focused. Add the terminal adapter after the PDF composition sources, for example:

```cmake
  src/pdf_export.c
  src/pdf_output.c
  src/pdf_range.c
  src/pdf_merge.c
  src/output_file.c)
```

Do not edit `.github/workflows/ci.yml`, vcpkg files, or unrelated source lists.

- [ ] **Step 5: Build and run only the new contract test first**

Use the same Linux dependency configuration as CI:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --target extractpdf_test_output_file --parallel 2
ctest --test-dir build -R '^extractpdf\.output_file$' --output-on-failure
```

Expected: `extractpdf.output_file` passes the ASCII exact-byte/truncate, UTF-8 filename, missing-parent I/O, and output-lifetime cases.

- [ ] **Step 6: Run all normal Linux tests**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: every existing test remains green, including all PDF composition tests and the new output-file test.

- [ ] **Step 7: Run Linux ASan/UBSan from a fresh build directory**

```bash
cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: all sanitizer CTests pass with no sanitizer diagnostics.

- [ ] **Step 8: Commit the GREEN implementation**

```bash
git add \
  include/extractpdf/extractpdf.h \
  src/output_file.c \
  CMakeLists.txt
git commit -m "feat: save immutable outputs to UTF-8 paths"
```

Record the exact commit SHA returned by this commit as the GREEN SHA.

Preserve the useful TDD history:

```text
spec
plan
RED contract test
GREEN terminal adapter
```

Do not squash during normal feature execution.

---

### Task 3: Exact-head cross-platform verification and Phase 4 roadmap closure

**Files:**
- No source/test changes expected.
- Update the draft Save PR, issue #29, and umbrella #2 only after exact-head verification succeeds.

**Interfaces:**
- Consumes: exact GREEN SHA from Task 2.
- Produces: auditable Linux normal/sanitizer proof plus same-head Linux/macOS/Windows `full-ci`, with Phase 4 marked complete but all stacked PRs still unmerged.

- [ ] **Step 1: Verify the final feature scope before trusting CI**

Compare PR #28 exact base `a6d35b6f5d68207cf9c628b2b38c35addb868d82` to the GREEN SHA.

The save slice must add only:

```text
docs/superpowers/specs/2026-08-28-extractpdf-output-save-file-design.md
docs/superpowers/plans/2026-08-28-extractpdf-output-save-file.md
include/extractpdf/extractpdf.h
src/output_file.c
CMakeLists.txt
tests/test_output_file.c
tests/CMakeLists.txt
```

Any change to `src/pdf_export.c`, `src/pdf_output.c`, `src/pdf_merge.c`, `src/internal.h`, vcpkg files, CI workflow files, or unrelated modules is a scope blocker and must be removed before final verification.

Also confirm the draft Save PR remains:

```text
base ref: feat/pdf-merge-outputs
base sha: a6d35b6f5d68207cf9c628b2b38c35addb868d82
head ref: feat/output-save-file
head sha: exact GREEN SHA
draft: true
merged: false
```

- [ ] **Step 2: Verify the exact GREEN SHA's normal PR workflow**

Read the workflow run attached to the GREEN SHA and require Linux success for:

```text
Configure static build                ✅
Build static library and tests        ✅
Test static build                     ✅
Configure sanitizer build             ✅
Build sanitizer configuration         ✅
Test sanitizer configuration          ✅
```

The normal pull-request run may skip macOS/Windows. Do not mark Phase 4 complete yet.

- [ ] **Step 3: Update the draft PR body with RED/GREEN evidence**

Record:

```text
public ABI
RED exact SHA + workflow number/run id + missing-symbol boundary
GREEN exact SHA + workflow number/run id + Linux normal/sanitizer success
non-consuming ownership
ASCII exact-byte/truncate proof
UTF-8 path proof
missing-parent IO proof
Windows strict UTF-8 contract
non-atomic/non-durable boundary
exact file scope
```

Keep the PR draft and unmerged.

- [ ] **Step 4: Apply the existing `full-ci` label without changing the head**

Use the existing PR label. Do not edit `.github/workflows/ci.yml`.

The resulting run must use the same exact GREEN SHA from Step 2.

- [ ] **Step 5: Verify same-head Linux/macOS/Windows acceptance**

Require:

```text
Linux:
  static configure/build/tests             ✅
  ASan/UBSan configure/build/tests          ✅

macOS:
  configure/build/tests                     ✅
  UTF-8 save/reopen test                    ✅

Windows:
  DLL configure/build/tests                 ✅
  extractpdf_output_save_file exported      ✅
  UTF-8 filename save/reopen                ✅
  invalid UTF-8 -> ARGUMENT                 ✅
```

The Windows CTest result is the evidence for both DLL export/link behavior and the platform-specific UTF-8 conversion branch.

If any platform fails, invoke `superpowers:systematic-debugging`, identify the root cause, add the smallest necessary regression/fix, and repeat Linux + same-head full-ci on the new exact SHA.

- [ ] **Step 6: Update issue #29 with final implementation evidence**

Keep #29 open only for stacked integration bookkeeping. Record:

```text
implementation PR number
RED SHA/workflow evidence
GREEN SHA/Linux workflow evidence
same-head full-ci workflow evidence
final seven-file save-slice scope
ownership/error/file semantics
Windows UTF-8 behavior
non-atomic/durability boundary
```

Do not close #29 while its stacked implementation PR remains unintegrated.

- [ ] **Step 7: Close the Phase 4 checklist in umbrella #2**

Only after same-head full-ci succeeds, change:

```text
[ ] Save/write with explicit ownership and error handling
```

to a checked line referencing `#29` and the exact numeric implementation PR number returned in Task 1.

Add one concise Phase 4 save/write proof paragraph containing the exact GREEN SHA, Linux workflow evidence, and same-head full-ci evidence.

Then label Phase 4 PDF Composition complete in prose. Leave Phase 5 and Phase 6 untouched/open, and leave the umbrella issue itself open.

- [ ] **Step 8: Final completion gate**

Freshly verify all four conditions:

1. Save PR is open, draft, unmerged, based on `feat/pdf-merge-outputs`, with head exactly equal to GREEN SHA.
2. Same-head full-ci jobs are all successful on Linux/macOS/Windows.
3. Compare against PR #28 exact base contains only the seven scoped files listed in Step 1.
4. #29 and #2 contain evidence matching the exact GREEN SHA; Phase 4 save/write is checked, while Phase 5/6 and stacked integration remain open.

Only then report the Save/write implementation and Phase 4 feature roadmap complete. Do not merge PR #20/#22/#24/#26/#28 or the Save PR without explicit authorization.
