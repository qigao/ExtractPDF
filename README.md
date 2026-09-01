# QuantaPDF

**QuantaPDF — PDF made easy.**

QuantaPDF is a compact native PDF kernel with a versioned C11 ABI suitable for
native callers and .NET P/Invoke.

> **Backends:** QuantaPDF uses pinned PDFium `154.0.8021.0` for document
> inspection, rendering, and extraction, plus qpdf `12.4.0` for structural
> transforms and editing. Both remain private implementation dependencies.

## Current v2 ABI

The v2 ABI uses the `quantapdf_*` symbol/type prefix and `QUANTAPDF_*`
constants throughout. `<quantapdf/quantapdf.h>` is the sole public header;
the previous namespace and compatibility wrappers are not retained.

Document lifecycle remains the root of the API:

```c
#include <quantapdf/quantapdf.h>

quantapdf_document *doc = NULL;
int pages = 0;

if (quantapdf_open("file.pdf", NULL, &doc) == QUANTAPDF_OK) {
    if (quantapdf_page_count(doc, &pages) == QUANTAPDF_OK) {
        /* use pages */
    }
    quantapdf_close(doc);
}
```

Page + Render adds opaque page and bitmap handles:

```c
quantapdf_page *page = NULL;
quantapdf_bitmap *bitmap = NULL;
int width, height, stride, components;

if (quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK) {
    if (quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK) {
        quantapdf_bitmap_dimensions(
            bitmap, &width, &height, &stride, &components);
        /* inspect bitmap data */
        quantapdf_drop_bitmap(bitmap);
    }
    quantapdf_drop_page(page);
}
```

The current supported surface includes:

- document open / password authentication / page count / close
- opaque page load / drop
- page bounds plus MediaBox/CropBox bounds
- RGB and RGBA page rendering
- DPI/zoom, rotation, and page-space clipping
- aspect-preserving thumbnail rendering
- bitmap dimensions and borrowed sample access
- plain UTF-8 text extraction
- immutable structured-text snapshots and geometric text search
- page-image enumeration and image bitmap rendering
- URI/internal links, annotations, document metadata, and outlines
- immutable AcroForm snapshots and isolated PDF edit sessions
- page export/range export, output merging, and file saving
- catalog-reachable document security audit and immutable policy sanitization
- immutable CropBox crop, MediaBox trim, poster-split, interactive-content
  flattening, and lossless rewrite/GC transforms
- stable status strings and one allocator-matched `quantapdf_free()` entry point

## API contract

- The public header contains no PDFium or qpdf types.
- Backend handles and C++ exceptions remain private to the library.
- `quantapdf_page` and `quantapdf_bitmap` borrow their parent document. The document must outlive all derived page and bitmap handles.
- A bitmap does not borrow its source page after rendering, so the page may be dropped before the bitmap, provided the document remains alive.
- PDFium process state is private, initialized once, and serialized by the
  backend runtime because PDFium's public API is not thread-safe.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Missing or incorrect passwords return `QUANTAPDF_ERROR_PASSWORD`.
- `quantapdf_open` accepts PDF input; non-PDF data returns
  `QUANTAPDF_ERROR_FORMAT`.
- `quantapdf_open` leaves the output handle NULL on failure.
- `quantapdf_close(NULL)`, `quantapdf_drop_page(NULL)`, and `quantapdf_drop_bitmap(NULL)` are safe.
- qpdf and standard C++ exceptions are caught inside the private bridge and
  translated to `quantapdf_status`.
- ABI v2 requires external serialization: applications must not execute
  `quantapdf_*` calls concurrently, including calls that use different
  documents or otherwise unrelated handles.

Snapshot/output ownership is explicit:

- `quantapdf_text_page`, `quantapdf_link_page`, `quantapdf_outline`,
  `quantapdf_annotation_page`, `quantapdf_form`, and `quantapdf_output`
  own their copied observations and remain valid for their documented snapshot lifetime;
- pointers returned by snapshot string/data accessors are borrowed until the
  corresponding snapshot/output is dropped;
- strings returned through `char **` outputs are caller-owned and must be
  released with `quantapdf_free()`;
- `quantapdf_pdf_edit` owns a private PDF graph and never mutates its source
  `quantapdf_document`.

## ABI-sized structures

The installed header publishes `QUANTAPDF_VERSION_MAJOR`,
`QUANTAPDF_VERSION_MINOR`, `QUANTAPDF_VERSION_PATCH`, and
`QUANTAPDF_ABI_VERSION`. ABI version 2 follows these compatibility rules:

- existing exported functions, enum numeric values, structure fields, and
  ownership rules are not removed, reordered, or reinterpreted;
- new functions and enum values may be appended;
- incompatible changes require a new ABI version and shared-library major
  version.

`struct_size` has two distinct contracts:

- Single option/info structures are append-only. A library reads or writes
  only fields covered by the caller-provided size.
- Structures traversed as C array elements have fixed V1 layouts because an
  array has no independent element-stride metadata. These currently include
  `quantapdf_page_crop`, `quantapdf_page_trim`,
  `quantapdf_page_poster_split`, `quantapdf_search_result`, and
  `quantapdf_form_value_input`. Their accepted `struct_size` range is the
  matching `QUANTAPDF_*_V1_MIN_SIZE` through `QUANTAPDF_*_V1_SIZE`; values
  larger than the fixed V1 layout are rejected. Future extensions require a
  new type/API or an API carrying an explicit element stride.

## Page coordinates

All public page rectangles use **displayed page space** rather than raw PDF object coordinates:

- the CropBox top-left is the page-space origin `(0, 0)`;
- x increases to the right;
- y increases downward;
- values are page points before render scaling (`72 points = 1 inch`).

`quantapdf_page_box_bounds(..., QUANTAPDF_PAGE_BOX_MEDIA, ...)` may therefore have negative coordinates when MediaBox extends outside CropBox.

This same page-space contract is intended for later text geometry, search quads, images, links, and annotations.

Intrinsic format-specific rotation metadata is intentionally not part of the generic Page API. Page bounds already describe displayed geometry, including `/Rotate` and `/UserUnit`; raw rotation metadata, if needed by callers, belongs in a later PDF-specific metadata surface. Rendering rotation is explicit and per-call.

## Rendering

The convenience call:

```c
quantapdf_render_page(page, &bitmap);
```

renders the full page at 72 DPI, zero additional rotation, opaque DeviceRGB, and a white background.

For explicit rendering use a versioned options struct:

```c
quantapdf_render_options options = {
    sizeof(quantapdf_render_options),
    144.0f, /* dpi */
    0.0f,   /* rotation_degrees */
    0,      /* clip_enabled */
    { 0 },  /* clip in displayed page space */
    0       /* alpha */
};

quantapdf_render_page_with_options(page, &options, &bitmap);
```

`struct_size` is part of the ABI contract. Callers should initialize it to the size of the struct they compiled against. Render options are a single append-only structure; the library ignores fields beyond the caller-provided size so older binaries retain their original defaults.

`dpi` is the canonical zoom/resolution input: 72 DPI is scale 1.0, 144 DPI is scale 2.0. Rotation is in degrees and is applied only to that render call. When clipping is enabled, `clip` is expressed in displayed page space and is transformed by the same DPI/rotation matrix; QuantaPDF renders directly into the clipped device bbox rather than allocating a full-page intermediate image.

`alpha` accepts only 0 or 1:

- `0`: 8-bit interleaved RGB, opaque white untouched pixels;
- `1`: 8-bit interleaved RGBA, transparent untouched pixels.

RGBA samples produced by the renderer use **premultiplied alpha**. `stride` returned by `quantapdf_bitmap_dimensions` is the authoritative byte distance between rows; callers must not assume a different packing rule. The pointer returned by `quantapdf_bitmap_data` is borrowed read-only storage and remains valid only until `quantapdf_drop_bitmap`.

## Thumbnails

```c
quantapdf_render_thumbnail(page, max_width, max_height, &bitmap);
```

renders opaque RGB while preserving the page aspect ratio. The result fits inside the requested pixel box and never upscales beyond the page's 72-DPI size. Thumbnail rendering derives a DPI and reuses the same renderer rather than maintaining a separate raster path.

## Document audit and sanitization

Initialize the size-tagged audit result before calling the audit API, then test
the independent finding bits that matter to your policy:

```c
quantapdf_audit_result audit = {
    .struct_size = sizeof(quantapdf_audit_result),
    .findings = 0
};

if (quantapdf_document_audit(doc, &audit) == QUANTAPDF_OK) {
    if (audit.findings & QUANTAPDF_AUDIT_JAVASCRIPT_ACTION) {
        /* JavaScript action or JavaScript name tree is reachable. */
    }
    if (audit.findings & QUANTAPDF_AUDIT_SIGNATURE) {
        /* A signature structure is present; validity is not checked. */
    }
    if (audit.findings & QUANTAPDF_AUDIT_ENCRYPTION) {
        /* The input is encrypted, even if it was opened with a password. */
    }
}
```

Sanitization returns a new immutable output and never mutates the opened
document:

```c
quantapdf_output *output = NULL;

if (quantapdf_sanitize(doc, QUANTAPDF_SANITIZE_ALL, &output) ==
    QUANTAPDF_OK) {
    const unsigned char *data = NULL;
    size_t size = 0;

    quantapdf_output_data(output, &data, &size);
    /* data[0..size) is borrowed read-only storage owned by output. */

    quantapdf_close(doc);
    doc = NULL;
    /* data remains valid because output owns its bytes independently. */

    quantapdf_drop_output(output);
}
```

`QUANTAPDF_SANITIZE_ALL` removes the seven sanitizable active-content classes.
Safe internal `/GoTo` navigation is not reported as dangerous and is not
removed. Signed or encrypted inputs are intentionally rejected by
`quantapdf_sanitize()` with `QUANTAPDF_ERROR_UNSUPPORTED` because rewriting
would invalidate a signature or silently decrypt the document.

The audit is a strict, catalog-reachable structural inspection of conventional
PDF security and active-content mechanisms. It is not antivirus or malware
detection, content keyword scanning, cryptographic signature validation, or
proof that arbitrary custom object edges are safe. Malformed conventional
structures return `QUANTAPDF_ERROR_FORMAT`; a bounded traversal or a requested
removal that cannot be proven complete returns `QUANTAPDF_ERROR_UNSUPPORTED`
without publishing output.

## Lossless rewrite and garbage collection

```c
quantapdf_output *rewritten = NULL;

if (quantapdf_rewrite_lossless(doc, &rewritten) == QUANTAPDF_OK) {
    quantapdf_output_save_file(rewritten, "canonical.pdf");
    quantapdf_drop_output(rewritten);
}
```

`quantapdf_rewrite_lossless()` performs a deterministic full rewrite with a
fixed policy. It removes indirect objects unreachable from the trailer graph,
rebuilds the cross-reference state, preserves existing stream encodings, and
does not deduplicate distinct reachable objects. Repeated calls on identical
input—and rewriting a reopened result—produce byte-identical output.

This API is deliberately strict. It does not repair damaged PDFs, recompress
images, or flatten interactive content. It neither preserves nor creates
encryption: encrypted input is rejected with `QUANTAPDF_ERROR_UNSUPPORTED`.
Inputs requiring structural recovery return `QUANTAPDF_ERROR_FORMAT`, and
already signed inputs return `QUANTAPDF_ERROR_UNSUPPORTED`. The returned
output owns its bytes independently of the source document.

## Flatten interactive content

```c
quantapdf_output *flattened = NULL;

if (quantapdf_flatten_interactive(
        doc,
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS,
        &flattened) == QUANTAPDF_OK) {
    quantapdf_output_save_file(flattened, "flattened.pdf");
    quantapdf_drop_output(flattened);
}
```

`quantapdf_flatten_interactive()` bakes existing normal appearance streams
into page content. Annotation and Widget selection are independent flags, and
the source document remains immutable. Widget flattening also prunes the
affected AcroForm field tree and calculation order.

The transform never synthesizes appearances, applies redactions, executes
form actions, or flattens Links. It fails closed for missing or malformed
appearances, malformed or unbalanced changed-page content, non-unit annotation
opacity, contradictory Widget page ownership, unsupported interactive
semantics, visible Links on changed pages, tagged PDFs that require structure
updates, encryption, and signatures.
Output is deterministic and idempotent.

## Dependency model

The backend foundation has two pinned dependency paths:

- `cmake/QuantaPDFPdfium.cmake` downloads the exact platform PDFium artifact,
  verifies its SHA-256, version, disabled V8/XFA settings, and license payload,
  and rejects unsupported platforms.
- `vcpkg.json` resolves qpdf `12.4.0` from the pinned registry baseline with
  optional OpenSSL, GnuTLS, and Zopfli features disabled.
- PDFium and qpdf remain private implementation dependencies; only the
  `quantapdf_*` C ABI is public.

The target architecture is:

```text
PDFium public C API ----+
                       +--> QuantaPDF private adapters --> quantapdf_* C ABI
qpdf C++ object graph --+
```

Only the `quantapdf_*` ABI is exported by the wrapper.

## Build with CMake Presets and vcpkg

Set `PROJECT_ROOT` to the parent package workspace and `VCPKG_ROOT` to a
vcpkg checkout containing the baseline in `vcpkg.json`. The configure presets
run manifest installation automatically. Public
configure, build, and test entry points live in the versioned
`CMakeUserPresets.json`.

### Linux x64

```sh
export PROJECT_ROOT=/opt
export VCPKG_ROOT=/opt/vcpkg

cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
```

### macOS

Use `macos-arm64-release-user` on Apple Silicon or
`macos-x64-release-user` on Intel, then run the same configure, build, and
CTest preset sequence.

### Windows x64 DLL

Run from an x64 Visual Studio Developer Command Prompt so `cl`, Ninja, and
the MSVC runtime are available:

```bat
set PROJECT_ROOT=C:\projects\cpp
set VCPKG_ROOT=C:\tools\vcpkg

cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
```

Use `win-dev-user` for the isolated Debug + AddressSanitizer profile.

The test build stages `quantapdf.dll` beside every Windows test executable. Consumers are responsible for deploying `quantapdf.dll` beside their executable or otherwise making it available through normal Windows DLL search rules.

## Tests

CTest covers the document lifecycle plus Page + Render contracts, including invalid arguments, page geometry, MediaBox/CropBox coordinates, RGB/RGBA output, versioned render options, DPI, rotation, clipping, thumbnails, encrypted PDFs, malformed input, repeated lifecycle stress, interleaved handles, UTF-8 paths, deterministic lossless rewrite/GC, and strict interactive-content flattening.

Linux additionally runs AddressSanitizer and UndefinedBehaviorSanitizer.

## CI

Normal pull-request updates use Linux as the fast development loop. Windows and macOS are reserved for explicit `full-ci` checkpoints, manual workflow dispatch, and pushes to `master`.

The workflow persists vcpkg binary packages and the exact hash-verified PDFium
archive. Cache identities include OS/architecture, the pinned vcpkg commit,
manifest content, and literal PDFium release `chromium-8021`.

A feature is not considered cross-platform complete until Linux, macOS, and Windows pass on the same exact head SHA. Older green runs do not satisfy acceptance for a newer head.

## License

QuantaPDF source is distributed under the **Apache License 2.0**. See
`LICENSE` and `THIRD_PARTY.md`.

The PDFium and qpdf backend dependencies use permissive licenses. Their pinned
license payloads and redistribution notices are installed with QuantaPDF; see
`THIRD_PARTY.md` for details.
