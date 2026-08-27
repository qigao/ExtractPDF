# ExtractPDF v2 design

Date: 2026-08-27  
Status: Phase 1 implementation in verification  
Target baseline: MuPDF 1.28.2

## Context

The repository started as a 2015 MuPDF 1.3 proof of concept whose single `libpdf.c` mixed CLI and DLL concerns, used process-global MuPDF state, and had no build/test/CI contract. ExtractPDF v2 preserves the product requirement—a small native C library for callers such as .NET—but replaces that architecture rather than porting it line by line.

## Goals

1. Stable C11 ABI for C, C++, and .NET P/Invoke.
2. Opaque document ownership with no MuPDF types in public headers.
3. No mutable process-global or thread-local document state.
4. Explicit password, error, and cleanup behavior.
5. Deterministic CTest coverage before text/image extraction.
6. One dependency model across Windows, Linux, and macOS.
7. Exact-head cross-platform CI.

## Non-goals for Phase 1

- PDF editing/writing.
- OCR.
- JavaScript execution.
- Concurrent MuPDF calls.
- Text or image extraction APIs.
- Preserving legacy exported names or unsafe buffer conventions.

## Public ABI

`include/extractpdf/extractpdf.h` exposes an opaque handle:

```c
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
EXTRACTPDF_API const char *extractpdf_status_string(extractpdf_status status);
EXTRACTPDF_API void extractpdf_close(extractpdf_document *document);
```

Windows exports use `EXTRACTPDF_SHARED` plus `EXTRACTPDF_BUILDING_LIBRARY`; callers never see MuPDF import/export macros.

### ABI rules

- Input paths are UTF-8.
- `password == NULL` means no password supplied.
- `extractpdf_open` sets `*out_document = NULL` on failure.
- Missing/incorrect passwords return `EXTRACTPDF_ERROR_PASSWORD`.
- `extractpdf_close(NULL)` is a no-op.
- Status codes, not strings, define program behavior.
- No MuPDF exception crosses the ABI.
- Phase 1 is single-threaded. Multiple handles may coexist and be used sequentially/interleaved on one thread; concurrency requires a separate future contract.

## Internal ownership

```c
struct extractpdf_document {
    fz_context *ctx;
    fz_document *doc;
};
```

Each handle owns one context and one document. Open validates arguments, allocates the wrapper, creates/registers the MuPDF context, opens the document, authenticates if needed, and returns the handle. Failure unwinds in reverse order. Close drops the document before the context and then frees the wrapper.

All MuPDF calls that can throw are contained by `fz_try`/`fz_catch` boundaries.

## Error translation

The public error contract is deliberately coarse:

- bad caller arguments -> `EXTRACTPDF_ERROR_ARGUMENT`
- path/system failures -> `EXTRACTPDF_ERROR_IO`
- authentication failure -> `EXTRACTPDF_ERROR_PASSWORD`
- malformed/syntax document -> `EXTRACTPDF_ERROR_FORMAT`
- unsupported content/operation -> `EXTRACTPDF_ERROR_UNSUPPORTED`
- wrapper/context allocation failure -> `EXTRACTPDF_ERROR_NOMEM`
- other caught MuPDF errors -> `EXTRACTPDF_ERROR_MUPDF`

Callers do not depend on MuPDF numeric error values.

## Canonical dependency architecture

All supported desktop platforms use the repository's vcpkg manifest plus overlay port.

```text
vcpkg.json
    + vcpkg-ports/libmupdf (MuPDF 1.28.2)
                       |
                       v
          unofficial::libmupdf::libmupdf
                       |
                       | PRIVATE
                       v
                  ExtractPDF
```

The overlay pins MuPDF 1.28.2 and the matching MuJS gitlink required by `source/fitz/regexp.c`. Optional upstream features that are outside Phase 1 and otherwise require additional embedded/submodule resources are disabled explicitly (including JavaScript, Markdown, and hyphenation).

MuPDF source is fetched from upstream by vcpkg; it is not committed into this repository.

### Windows

Windows uses `x64-windows-static-md`: MuPDF and transitive dependencies are static libraries using the dynamic MSVC CRT. They are linked privately into `extractpdf.dll`.

```text
static MuPDF 1.28.2 + static third-party libs
                    |
                    v
              extractpdf.dll
                    |
                    v
             public extractpdf_* ABI
```

The overlay retains a host-side `libmupdf` build dependency solely to provide MuPDF's `bin2coff` build tool for embedding fonts. This host tool is not a runtime dependency and does not change the static-MuPDF/shared-ExtractPDF boundary.

There is intentionally no `mupdfcpp64`, `FZ_DLL_CLIENT`, or MuPDF DLL path in the v2 build.

### Linux/macOS

Linux and macOS consume the same overlay target through their native vcpkg triplets. CI builds ExtractPDF static on these platforms; Linux also runs ASan/UBSan.

## Test strategy

Phase 1 CTest coverage locks:

1. invalid/null argument rejection;
2. missing file -> I/O error;
3. one- and two-page counts;
4. encrypted PDF with no/wrong/correct password;
5. malformed/truncated input;
6. 100 repeated open/count/close lifecycles;
7. two interleaved independent handles;
8. `extractpdf_close(NULL)`;
9. UTF-8 path behavior.

Windows shared-library tests stage `extractpdf.dll` beside the test executables so CTest validates the ABI rather than relying on an accidental machine PATH. MSVC compiles the UTF-8 fixture test with `/utf-8`. Tests have explicit timeouts to turn loader/lifecycle hangs into bounded failures.

## CI

The pull-request workflow is the canonical feature-branch proof. `master` runs again on push. Every desktop job bootstraps the same pinned vcpkg commit and uses the repository overlay. Linux, macOS, and Windows must pass on the exact PR head SHA; older runs do not satisfy acceptance.

## Phase 2: text

After Phase 1 is green, text extraction returns UTF-8 memory through the ABI and adds an exported deallocator so callers never mix allocators. A planned shape is:

```c
EXTRACTPDF_API extractpdf_status extractpdf_extract_text(
    extractpdf_document *document,
    int page_index,
    char **out_utf8,
    size_t *out_size);
EXTRACTPDF_API void extractpdf_free(void *memory);
```

Empty-page representation must be locked by tests before publishing the API.

## Phase 3: images

The default meaning of "images on a page" is images encountered in page content, not every object in the PDF xref table. Raw PDF image-XObject access, if ever needed, is a separately named advanced API. The core library does not choose output filenames or silently impose an output image format.

## Legacy migration

The root `libpdf.c` remains untouched during Phase 1. After the lifecycle, text, and image surfaces are covered, it may be moved intact to `legacy/`. A compatibility shim is added only for a demonstrated downstream need; unsafe legacy conventions are not preserved by default.

## Licensing

The repository baseline is **AGPL-3.0-or-later**. MuPDF is fetched as an upstream dependency and has its own licensing options. No dependency packaging choice changes the public ExtractPDF ABI.

## Phase 1 acceptance

Phase 1 is complete only when one exact head satisfies all of the following:

- AGPL license/README are present;
- public header contains no MuPDF types;
- no mutable ExtractPDF-owned global/TLS document state exists;
- open/password/page-count/close/error tests pass;
- malformed and encrypted inputs fail safely;
- UTF-8 path behavior passes on all supported platforms;
- Windows proves static MuPDF privately linked into shared ExtractPDF;
- Linux sanitizer tests are clean;
- project CMake has one canonical vcpkg package target and no MuPDF DLL-client fallback;
- Linux/macOS/Windows CI is green on the exact head.
