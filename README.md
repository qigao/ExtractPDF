# QuantaPDF

**QuantaPDF — PDF made easy.**

QuantaPDF is a compact C11 PDF engine that keeps MuPDF behind a stable C ABI
suitable for native callers and .NET P/Invoke.

The v2 implementation targets **MuPDF 1.28.2**. It replaces the original 2015 MuPDF 1.3 proof of concept with explicit ownership, stable status handling, deterministic tests, CMake/CTest, and exact-head Windows/Linux/macOS CI.

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
- immutable CropBox crop, MediaBox trim, and poster-split transforms
- stable status strings and one allocator-matched `quantapdf_free()` entry point

## API contract

- The public header contains no MuPDF types.
- Each `quantapdf_document` owns one MuPDF context and document.
- `quantapdf_page` and `quantapdf_bitmap` borrow their parent document. The document must outlive all derived page and bitmap handles.
- A bitmap does not borrow its source page after rendering, so the page may be dropped before the bitmap, provided the document remains alive.
- QuantaPDF keeps no mutable process-global or thread-local document state.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Missing or incorrect passwords return `QUANTAPDF_ERROR_PASSWORD`.
- `quantapdf_open` leaves the output handle NULL on failure.
- `quantapdf_close(NULL)`, `quantapdf_drop_page(NULL)`, and `quantapdf_drop_bitmap(NULL)` are safe.
- MuPDF exceptions are caught inside the library and translated to `quantapdf_status`.
- The current runtime contract is deliberately single-threaded. Separate handles may coexist and be used sequentially/interleaved on one thread; concurrent MuPDF calls are not yet part of the contract.

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

All public page rectangles use **Fitz page space** rather than raw PDF object coordinates:

- the CropBox top-left is the page-space origin `(0, 0)`;
- x increases to the right;
- y increases downward;
- values are page points before render scaling (`72 points = 1 inch`).

`quantapdf_page_box_bounds(..., QUANTAPDF_PAGE_BOX_MEDIA, ...)` may therefore have negative coordinates when MediaBox extends outside CropBox.

This same page-space contract is intended for later text geometry, search quads, images, links, and annotations.

Intrinsic format-specific rotation metadata is intentionally not part of the generic Page API. Fitz bounds already describe displayed page geometry; PDF `/Rotate`, if needed by callers, belongs in a later PDF-specific metadata surface. Rendering rotation is explicit and per-call.

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
    { 0 },  /* clip in Fitz page space */
    0       /* alpha */
};

quantapdf_render_page_with_options(page, &options, &bitmap);
```

`struct_size` is part of the ABI contract. Callers should initialize it to the size of the struct they compiled against. Render options are a single append-only structure; the library ignores fields beyond the caller-provided size so older binaries retain their original defaults.

`dpi` is the canonical zoom/resolution input: 72 DPI is scale 1.0, 144 DPI is scale 2.0. Rotation is in degrees and is applied only to that render call. When clipping is enabled, `clip` is expressed in Fitz page space and is transformed by the same DPI/rotation matrix; QuantaPDF renders directly into the clipped device bbox rather than allocating a full-page intermediate image.

`alpha` accepts only 0 or 1:

- `0`: 8-bit interleaved RGB, opaque white untouched pixels;
- `1`: 8-bit interleaved RGBA, transparent untouched pixels.

RGBA samples produced by the renderer use **premultiplied alpha**, matching MuPDF's rendered pixmap contract. `stride` returned by `quantapdf_bitmap_dimensions` is the authoritative byte distance between rows; callers must not assume a different packing rule. The pointer returned by `quantapdf_bitmap_data` is borrowed read-only storage and remains valid only until `quantapdf_drop_bitmap`.

## Thumbnails

```c
quantapdf_render_thumbnail(page, max_width, max_height, &bitmap);
```

renders opaque RGB while preserving the page aspect ratio. The result fits inside the requested pixel box and never upscales beyond the page's 72-DPI size. Thumbnail rendering derives a DPI and reuses the same renderer rather than maintaining a separate raster path.

## Dependency model

The canonical dependency path on **all supported desktop platforms is vcpkg manifest mode**.

- `vcpkg.json` pins the vcpkg registry baseline used by CI.
- `vcpkg-ports/libmupdf` is an overlay port that pins MuPDF **1.28.2** and the MuJS gitlink required by that release.
- MuPDF itself is not copied into this repository; the overlay fetches the pinned upstream sources during the vcpkg build.
- Project CMake consumes only `unofficial::libmupdf::libmupdf`.
- There is no `MUPDF_ROOT`, MuPDF DLL-client, or `mupdfcpp64` build path in v2.

On Windows, MuPDF and its third-party dependencies are static libraries built with the dynamic CRT triplet `x64-windows-static-md`. They are linked **privately** into the shared QuantaPDF wrapper:

```text
MuPDF 1.28.2 static libraries
          |
          | PRIVATE
          v
      quantapdf.dll
          |
          v
   C / C++ / .NET callers
```

Only the `quantapdf_*` ABI is exported by the wrapper.

## Build with CMake Presets and vcpkg

Set `PROJECT_ROOT` to the parent package workspace and `VCPKG_ROOT` to a
vcpkg checkout containing the baseline in `vcpkg.json`. The configure presets
run manifest installation with the repository overlay automatically. Public
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

CTest covers the document lifecycle plus Page + Render contracts, including invalid arguments, page geometry, MediaBox/CropBox coordinates, RGB/RGBA output, versioned render options, DPI, rotation, clipping, thumbnails, encrypted PDFs, malformed input, repeated lifecycle stress, interleaved handles, and UTF-8 paths.

Linux additionally runs AddressSanitizer and UndefinedBehaviorSanitizer.

## CI

Normal pull-request updates use Linux as the fast development loop. Windows and macOS are reserved for explicit `full-ci` checkpoints, manual workflow dispatch, and pushes to `master`.

The workflow persists vcpkg binary packages through GitHub Actions cache, keyed by OS/architecture, pinned vcpkg commit, manifest, and overlay content. This avoids rebuilding the MuPDF/HarfBuzz/FreeType dependency graph on each RED/GREEN iteration while leaving vcpkg's package ABI checks authoritative.

A feature is not considered cross-platform complete until Linux, macOS, and Windows pass on the same exact head SHA. Older green runs do not satisfy acceptance for a newer head.

## License

QuantaPDF is distributed under **AGPL-3.0-or-later**. See `LICENSE`.

MuPDF is an upstream dependency with its own AGPL/commercial licensing options. Downstream users should review the applicable licenses for their distribution model.
