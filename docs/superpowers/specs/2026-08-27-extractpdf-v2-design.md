# ExtractPDF v2 design

Date: 2026-08-27
Status: approved direction; implementation not started
Target baseline: MuPDF 1.28.2

## Context

The current repository is a 2015 MuPDF 1.3 proof of concept. Its single `libpdf.c` mixes a CLI and DLL ABI, uses process-global MuPDF state, has unsafe fixed buffers, and has no build/test/CI surface. Porting it line-by-line would preserve the wrong architecture.

ExtractPDF v2 keeps the validated product requirement — a small native C library that lets callers such as .NET open PDFs and extract page content — but replaces the implementation and ABI.

MuPDF 1.28.2 is the initial tested baseline. The implementation must use MuPDF's documented stable C API and put every MuPDF call that can throw behind `fz_try`/`fz_always`/`fz_catch` boundaries.

References:

- https://mupdf.readthedocs.io/en/latest/reference/c/introduction.html
- https://mupdf.readthedocs.io/en/latest/reference/c/overview.html
- https://mupdf.com/releases/history

## Goals

1. Provide a small, stable C ABI suitable for C, C++, and .NET P/Invoke.
2. Eliminate process-global document/context state.
3. Make ownership and error behavior explicit.
4. Support encrypted PDFs without ambiguous success/failure.
5. Make open, page count, text extraction, image extraction, and close independently testable.
6. Build on Windows, Linux, and macOS with CMake.
7. Keep the core library free of CLI/file-output policy.
8. Establish CI and regression fixtures before replacing the legacy implementation.

## Non-goals for the first implementation slice

- PDF editing or writing.
- OCR.
- JavaScript execution.
- Multi-threaded access to a single document handle.
- Raw extraction of every orphan PDF image object.
- Preserving the old exported function names.
- Bundling MuPDF source into this repository.

## Chosen approach

Use a thin modern wrapper around MuPDF rather than patching the old implementation or creating a large framework.

The public ABI is opaque and version-independent from MuPDF:

```c
typedef struct extractpdf_document extractpdf_document;
```

Each handle owns the MuPDF state required for one open document. No document, image list, text page, file pointer, or error state is global.

The first implementation is deliberately single-handle/single-thread usage. Separate handles may exist simultaneously, but v2 does not promise concurrent MuPDF calls until a shared lock/clone-context design is added and tested. This matches MuPDF's documented threading constraints instead of pretending the underlying context is freely concurrent.

## Public API

The initial header is `include/extractpdf/extractpdf.h`.

```c
#ifndef EXTRACTPDF_EXTRACTPDF_H
#define EXTRACTPDF_EXTRACTPDF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(EXTRACTPDF_SHARED)
#  if defined(EXTRACTPDF_BUILDING_LIBRARY)
#    define EXTRACTPDF_API __declspec(dllexport)
#  else
#    define EXTRACTPDF_API __declspec(dllimport)
#  endif
#else
#  define EXTRACTPDF_API
#endif

typedef struct extractpdf_document extractpdf_document;

typedef enum extractpdf_status {
    EXTRACTPDF_OK = 0,
    EXTRACTPDF_ERROR_ARGUMENT = 1,
    EXTRACTPDF_ERROR_IO = 2,
    EXTRACTPDF_ERROR_PASSWORD = 3,
    EXTRACTPDF_ERROR_FORMAT = 4,
    EXTRACTPDF_ERROR_UNSUPPORTED = 5,
    EXTRACTPDF_ERROR_NOMEM = 6,
    EXTRACTPDF_ERROR_MUPDF = 7
} extractpdf_status;

EXTRACTPDF_API extractpdf_status extractpdf_open(
    const char *filename,
    const char *password,
    extractpdf_document **out_document);

EXTRACTPDF_API extractpdf_status extractpdf_page_count(
    extractpdf_document *document,
    int *out_page_count);

EXTRACTPDF_API const char *extractpdf_last_error(
    const extractpdf_document *document);

EXTRACTPDF_API void extractpdf_close(
    extractpdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
```

### API rules

- Input paths are UTF-8 at the public boundary. Windows path handling must be implemented deliberately rather than leaking ANSI `fopen` semantics into the ABI.
- `password == NULL` means no password supplied.
- `extractpdf_open` sets `*out_document = NULL` on every failure.
- A password-protected document returns `EXTRACTPDF_ERROR_PASSWORD` unless authentication succeeds.
- Page indexes in future page APIs are zero-based.
- `extractpdf_close(NULL)` is allowed and is a no-op.
- `extractpdf_last_error` returns diagnostic text owned by the handle and valid until the next operation on that handle or close. Status codes, not strings, define program behavior.
- No exported function lets a MuPDF exception cross the C ABI.

## Internal document object

The implementation owns all state through the opaque object:

```c
struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
    char last_error[512];
};
```

The exact structure is private and may change without ABI breakage.

Open lifecycle:

```text
validate arguments
    -> allocate wrapper
    -> fz_new_context
    -> fz_register_document_handlers
    -> fz_open_document
    -> if fz_needs_password: authenticate
    -> success: return handle
```

Any failure unwinds in reverse order. `fz_drop_document` happens before `fz_drop_context`.

## Error translation

MuPDF errors are caught inside the implementation and translated into `extractpdf_status` plus a diagnostic message.

The stable contract is intentionally coarse:

- bad caller arguments -> `EXTRACTPDF_ERROR_ARGUMENT`
- path/open failures -> `EXTRACTPDF_ERROR_IO`
- authentication failure -> `EXTRACTPDF_ERROR_PASSWORD`
- malformed PDF -> `EXTRACTPDF_ERROR_FORMAT`
- unsupported operation/content -> `EXTRACTPDF_ERROR_UNSUPPORTED`
- allocation failure -> `EXTRACTPDF_ERROR_NOMEM`
- anything else caught from MuPDF -> `EXTRACTPDF_ERROR_MUPDF`

The implementation may inspect `fz_caught(ctx)` where useful, but callers must not depend on MuPDF numeric error values.

## Text extraction contract

Text extraction is phase 2, after the lifecycle API is green.

The native library returns UTF-8 content; it does not accept an output filename. Memory returned across the ABI must be released by an exported `extractpdf_free` function so callers never mix allocators.

Planned shape:

```c
EXTRACTPDF_API extractpdf_status extractpdf_extract_text(
    extractpdf_document *document,
    int page_index,
    char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API void extractpdf_free(void *memory);
```

`out_size` excludes the terminating NUL. Empty pages succeed with an allocated empty string or a documented zero-length representation; the implementation tests will lock this behavior before publishing the function.

## Image extraction contract

The default v2 meaning of "images on a page" is **images encountered in page content**, not every object in the PDF xref table. This avoids the legacy mismatch where one API walked page resources while another scanned all PDF objects and could return orphan/unreferenced images.

Phase 3 will expose a callback or enumerator with metadata and bytes. The first implementation should favor page-observed images through MuPDF's public page/device/structured-text facilities. Raw PDF image-XObject extraction, if needed later, must be a separately named advanced API because it has different semantics.

The core API will not choose output filenames or silently convert every image to PNG/PAM. Format policy belongs to the caller or an optional CLI layer.

## Build layout

```text
ExtractPDF/
├── CMakeLists.txt
├── cmake/
│   └── FindMuPDF.cmake
├── include/
│   └── extractpdf/
│       └── extractpdf.h
├── src/
│   ├── document.c
│   ├── text.c          # phase 2
│   └── image.c         # phase 3
├── tests/
│   ├── CMakeLists.txt
│   ├── test_document.c
│   └── fixtures/
├── tools/
│   └── extractpdf.c    # optional CLI, after core behavior is stable
└── legacy/
    └── libpdf.c        # legacy code moved intact only when migration lands
```

The library is C11. CMake builds both a normal library target and tests with CTest. MuPDF remains an external dependency discovered through a CMake module/root hint; this repository does not vendor MuPDF.

## Test strategy

The lifecycle slice is test-driven. Required RED/GREEN cases before adding extraction:

1. null/invalid argument rejection
2. missing file -> IO error
3. valid one-page PDF -> open succeeds
4. page count is correct
5. password-required PDF without password -> password error
6. wrong password -> password error
7. correct password -> open succeeds
8. malformed/truncated PDF -> clean error, no process abort
9. repeated open/close -> no retained state
10. two handles opened sequentially/interleaved -> no state contamination
11. `extractpdf_close(NULL)` -> safe

Sanitizer jobs should cover Linux when practical. Warnings are treated as errors for ExtractPDF-owned C code.

## CI

GitHub Actions should run CMake configure/build/CTest on:

- Ubuntu
- Windows
- macOS

CI must use a known MuPDF 1.28.2 dependency and must not rely on a developer-machine path. Dependency acquisition belongs to CI setup; project CMake only consumes an installed/extracted MuPDF tree.

## Licensing

MuPDF's open-source distribution is AGPL, with commercial licensing available from Artifex. ExtractPDF must add an explicit repository license/provenance decision before publishing a binary release. The v2 implementation must not imply that wrapping MuPDF in a DLL changes MuPDF's licensing obligations.

No MuPDF source is copied into this repository by this design.

## Migration

The legacy implementation remains untouched while v2 becomes green. Once the new lifecycle, text, and image behavior has coverage:

1. move the original `libpdf.c` to `legacy/` without semantic edits;
2. update the README to describe v2 and identify legacy code as historical;
3. optionally provide a narrow compatibility shim only if an actual downstream caller still needs the old names;
4. do not preserve unsafe legacy buffer/output conventions merely for source compatibility.

## Acceptance criteria for v2 foundation

The foundation is complete when an exact commit passes all of the following:

- public header contains no MuPDF types;
- no mutable process-global state exists in ExtractPDF-owned code;
- open/password/page-count/close tests pass;
- malformed and encrypted fixtures fail safely;
- Windows/Linux/macOS builds use the same C ABI;
- CTest is the test entry point;
- README documents dependency and license constraints;
- CI is green on the exact head commit.
