# ExtractPDF v2 design

Date: 2026-08-27  
Status: v2 foundation implemented; Page + Render implemented; Content in progress  
Target baseline: MuPDF 1.28.2

## Context

The repository started as a 2015 MuPDF 1.3 proof of concept whose single `libpdf.c` mixed CLI and DLL concerns, used process-global MuPDF state, and had no build/test/CI contract. ExtractPDF v2 preserves the product requirement—a small native C library for callers such as .NET—but replaces that architecture rather than porting it line by line.

## Goals

1. Stable C11 ABI for C, C++, and .NET P/Invoke.
2. Opaque ownership with no MuPDF types in public headers.
3. No mutable process-global or thread-local document state.
4. Explicit password, error, allocation, and cleanup behavior.
5. Deterministic CTest coverage before publishing each capability.
6. One pinned dependency model across Windows, Linux, and macOS.
7. Linux-first RED/GREEN development with exact-head cross-platform feature checkpoints.
8. Small primitives that compose into higher-level PDF operations.

## Foundation ABI

The document handle owns one MuPDF context and document:

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

### Foundation rules

- Input paths are UTF-8.
- `password == NULL` means no password supplied.
- `extractpdf_open` sets `*out_document = NULL` on failure.
- Missing/incorrect passwords return `EXTRACTPDF_ERROR_PASSWORD`.
- `extractpdf_close(NULL)` is a no-op.
- Status codes, not diagnostic strings, define behavior.
- No MuPDF exception crosses the ABI.
- The runtime contract remains single-threaded until concurrency is separately designed and tested.

## Phase 2 — Page + Render

Page operations are anchored by an opaque page handle:

```c
typedef struct extractpdf_page extractpdf_page;
typedef struct extractpdf_bitmap extractpdf_bitmap;

typedef struct extractpdf_rect {
    float x0;
    float y0;
    float x1;
    float y1;
} extractpdf_rect;
```

The Page + Render surface includes:

- page load/drop;
- page bounds and MediaBox/CropBox bounds;
- full-page RGB/RGBA rendering;
- DPI/zoom;
- per-call rotation;
- page-space clipping;
- aspect-preserving thumbnails;
- bitmap dimensions and borrowed sample access.

### Derived-handle ownership

`extractpdf_page` and `extractpdf_bitmap` borrow their parent `extractpdf_document`; the document must outlive them. A bitmap no longer depends on the page object after rendering, so the page may be dropped before the bitmap as long as the document remains alive.

This intentionally keeps Phase 2 ownership simple and explicit rather than adding hidden document reference counting. If independent child-handle lifetime becomes a demonstrated requirement, that is a separate ABI/lifetime design decision.

### Coordinate contract

Public geometry is **Fitz page space**:

```text
CropBox top-left = (0, 0)
x → right
y → down
72 page points = 1 inch before rendering
```

MuPDF uses CropBox to establish the displayed-page origin, so MediaBox bounds may be negative relative to that origin. Raw PDF box coordinates are not exposed through the generic page geometry API.

This coordinate model is the shared basis for later text geometry, search quads, image placement, links, and annotations.

The generic Page API deliberately does not expose a format-independent intrinsic rotation property. Fitz bounds already represent displayed page geometry, while PDF `/Rotate` is format-specific metadata. Rendering rotation remains explicit and per-call.

### Render options ABI

```c
typedef struct extractpdf_render_options {
    size_t struct_size;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    extractpdf_rect clip;
    int alpha;
} extractpdf_render_options;
```

The struct is append-only. `struct_size` tells the library which fields the caller compiled against; fields beyond that size are ignored and retain their historical defaults.

`dpi` is the canonical scale representation: 72 DPI = scale 1.0. Rotation and clipping are per-call state; there is no persistent document zoom state.

When clipping, ExtractPDF transforms the page-space clip through the same render matrix and renders directly into the resulting device bbox. It does not render a full-page intermediate and crop afterward.

`alpha=0` produces opaque DeviceRGB on white. `alpha=1` produces rendered DeviceRGB + alpha. Samples are 8-bit interleaved RGB/RGBA; RGBA produced by rendering is premultiplied alpha. Callers use the returned stride rather than assuming row packing.

Thumbnail rendering derives a DPI from the requested maximum pixel box, preserves aspect ratio, never upscales above 72-DPI page size, and calls the same internal render primitive.

## Phase 3 — Content

Content builds on `extractpdf_page` and reuses the Phase 2 coordinate model.

Planned order:

1. plain UTF-8 text;
2. structured text blocks/lines/spans with geometry;
3. search returning page-space quads/bounds;
4. page-observed images and geometry;
5. links and destinations.

The first plain-text slice returns ExtractPDF-owned, NUL-terminated UTF-8 memory and an explicit byte length. The exported deallocator keeps allocator ownership inside the DLL and lets the returned text outlive its page/document.

Structured text and search should use MuPDF's structured-text layer rather than parsing PDF content streams directly.

## Later phases

### PDF composition

- subset/export selected pages;
- split;
- reorder/delete/duplicate;
- merge;
- output abstraction and save/write.

Prefer a small page-selection/export primitive that can express multiple higher-level operations rather than independent engines for every command.

### Interactive PDF

- metadata;
- outlines;
- annotations;
- forms/widgets.

### Rewrite/transform

- crop/trim;
- poster split;
- flatten/bake;
- optimize/garbage collect;
- image recompression;
- encryption/re-encryption.

The authoritative feature checklist is issue #2.

## Error translation

The public error contract remains deliberately coarse:

- bad caller arguments -> `EXTRACTPDF_ERROR_ARGUMENT`
- path/system failures -> `EXTRACTPDF_ERROR_IO`
- authentication failure -> `EXTRACTPDF_ERROR_PASSWORD`
- malformed/syntax document -> `EXTRACTPDF_ERROR_FORMAT`
- unsupported content/operation -> `EXTRACTPDF_ERROR_UNSUPPORTED`
- wrapper/context allocation failure -> `EXTRACTPDF_ERROR_NOMEM`
- other caught MuPDF errors -> `EXTRACTPDF_ERROR_MUPDF`

Callers do not depend on MuPDF numeric error values.

## Canonical dependency architecture

All supported desktop platforms use the repository's pinned vcpkg manifest plus overlay port.

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

The overlay pins MuPDF 1.28.2 and the matching MuJS gitlink required by `source/fitz/regexp.c`. Optional upstream features outside the current scope that otherwise require additional resources are disabled explicitly.

MuPDF source is fetched from upstream by vcpkg; it is not committed into this repository.

### Windows

Windows uses `x64-windows-static-md`: MuPDF and transitive dependencies are static libraries using the dynamic MSVC CRT and are linked privately into `extractpdf.dll`.

The overlay retains a host-side `libmupdf` build dependency solely to provide MuPDF's `bin2coff` build tool for embedding resources. This host tool is not a runtime dependency.

There is intentionally no `mupdfcpp64`, `FZ_DLL_CLIENT`, or MuPDF DLL path in v2.

### Linux/macOS

Linux and macOS consume the same overlay target through native vcpkg triplets. CI currently builds ExtractPDF static on these platforms; Linux also runs ASan/UBSan.

## Test and CI strategy

Every capability follows RED -> minimal GREEN with deterministic CTest coverage.

Normal pull-request updates run Linux only. Windows/macOS run for explicit `full-ci` feature checkpoints, manual workflow dispatch, and `master` pushes. A feature is cross-platform complete only when all supported jobs pass on the same exact head SHA.

The workflow persists the vcpkg **binary package cache**, not `vcpkg_installed`. Cache namespaces include OS/architecture and pinned vcpkg state; vcpkg package ABI checks remain authoritative. Binary archives are saved immediately after a successful dependency install so an intentional later RED does not force the next iteration to rebuild MuPDF/HarfBuzz/FreeType.

Windows shared-library tests stage `extractpdf.dll` beside every test executable so CTest validates the exported ABI rather than relying on machine PATH state.

## Legacy implementation

The root `libpdf.c` remains the untouched MuPDF 1.3 historical proof of concept during the v2 migration. A compatibility shim is added only for a demonstrated downstream need; unsafe legacy conventions are not preserved by default.

## Licensing

The repository baseline is **AGPL-3.0-or-later**. MuPDF is fetched as an upstream dependency and has its own AGPL/commercial licensing options. Dependency packaging does not remove those obligations.
