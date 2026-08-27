# ExtractPDF

ExtractPDF is a small C11 library that keeps MuPDF behind a stable C ABI suitable for native callers and .NET P/Invoke.

The v2 implementation targets **MuPDF 1.28.2**. It replaces the original 2015 MuPDF 1.3 proof of concept with explicit ownership, stable status handling, deterministic tests, CMake/CTest, and exact-head Windows/Linux/macOS CI.

## Current v2 API

Document lifecycle remains the root of the API:

```c
#include <extractpdf/extractpdf.h>

extractpdf_document *doc = NULL;
int pages = 0;

if (extractpdf_open("file.pdf", NULL, &doc) == EXTRACTPDF_OK) {
    if (extractpdf_page_count(doc, &pages) == EXTRACTPDF_OK) {
        /* use pages */
    }
    extractpdf_close(doc);
}
```

Page + Render adds opaque page and bitmap handles:

```c
extractpdf_page *page = NULL;
extractpdf_bitmap *bitmap = NULL;
int width, height, stride, components;

if (extractpdf_load_page(doc, 0, &page) == EXTRACTPDF_OK) {
    if (extractpdf_render_page(page, &bitmap) == EXTRACTPDF_OK) {
        extractpdf_bitmap_dimensions(
            bitmap, &width, &height, &stride, &components);
        /* inspect bitmap data */
        extractpdf_drop_bitmap(bitmap);
    }
    extractpdf_drop_page(page);
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
- stable status strings

Plain UTF-8 text, structured text/search, page images, and links are tracked as the next Content phase in issue #2.

## API contract

- The public header contains no MuPDF types.
- Each `extractpdf_document` owns one MuPDF context and document.
- `extractpdf_page` and `extractpdf_bitmap` borrow their parent document. The document must outlive all derived page and bitmap handles.
- A bitmap does not borrow its source page after rendering, so the page may be dropped before the bitmap, provided the document remains alive.
- ExtractPDF keeps no mutable process-global or thread-local document state.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Missing or incorrect passwords return `EXTRACTPDF_ERROR_PASSWORD`.
- `extractpdf_open` leaves the output handle NULL on failure.
- `extractpdf_close(NULL)`, `extractpdf_drop_page(NULL)`, and `extractpdf_drop_bitmap(NULL)` are safe.
- MuPDF exceptions are caught inside the library and translated to `extractpdf_status`.
- The current runtime contract is deliberately single-threaded. Separate handles may coexist and be used sequentially/interleaved on one thread; concurrent MuPDF calls are not yet part of the contract.

## Page coordinates

All public page rectangles use **Fitz page space** rather than raw PDF object coordinates:

- the CropBox top-left is the page-space origin `(0, 0)`;
- x increases to the right;
- y increases downward;
- values are page points before render scaling (`72 points = 1 inch`).

`extractpdf_page_box_bounds(..., EXTRACTPDF_PAGE_BOX_MEDIA, ...)` may therefore have negative coordinates when MediaBox extends outside CropBox.

This same page-space contract is intended for later text geometry, search quads, images, links, and annotations.

Intrinsic format-specific rotation metadata is intentionally not part of the generic Page API. Fitz bounds already describe displayed page geometry; PDF `/Rotate`, if needed by callers, belongs in a later PDF-specific metadata surface. Rendering rotation is explicit and per-call.

## Rendering

The convenience call:

```c
extractpdf_render_page(page, &bitmap);
```

renders the full page at 72 DPI, zero additional rotation, opaque DeviceRGB, and a white background.

For explicit rendering use a versioned options struct:

```c
extractpdf_render_options options = {
    sizeof(extractpdf_render_options),
    144.0f, /* dpi */
    0.0f,   /* rotation_degrees */
    0,      /* clip_enabled */
    { 0 },  /* clip in Fitz page space */
    0       /* alpha */
};

extractpdf_render_page_with_options(page, &options, &bitmap);
```

`struct_size` is part of the ABI contract. Callers should initialize it to the size of the struct they compiled against. New fields are append-only; the library ignores fields beyond the caller-provided size so older binaries retain their original defaults.

`dpi` is the canonical zoom/resolution input: 72 DPI is scale 1.0, 144 DPI is scale 2.0. Rotation is in degrees and is applied only to that render call. When clipping is enabled, `clip` is expressed in Fitz page space and is transformed by the same DPI/rotation matrix; ExtractPDF renders directly into the clipped device bbox rather than allocating a full-page intermediate image.

`alpha` accepts only 0 or 1:

- `0`: 8-bit interleaved RGB, opaque white untouched pixels;
- `1`: 8-bit interleaved RGBA, transparent untouched pixels.

RGBA samples produced by the renderer use **premultiplied alpha**, matching MuPDF's rendered pixmap contract. `stride` returned by `extractpdf_bitmap_dimensions` is the authoritative byte distance between rows; callers must not assume a different packing rule. The pointer returned by `extractpdf_bitmap_data` is borrowed read-only storage and remains valid only until `extractpdf_drop_bitmap`.

## Thumbnails

```c
extractpdf_render_thumbnail(page, max_width, max_height, &bitmap);
```

renders opaque RGB while preserving the page aspect ratio. The result fits inside the requested pixel box and never upscales beyond the page's 72-DPI size. Thumbnail rendering derives a DPI and reuses the same renderer rather than maintaining a separate raster path.

## Dependency model

The canonical dependency path on **all supported desktop platforms is vcpkg manifest mode**.

- `vcpkg.json` pins the vcpkg registry baseline used by CI.
- `vcpkg-ports/libmupdf` is an overlay port that pins MuPDF **1.28.2** and the MuJS gitlink required by that release.
- MuPDF itself is not copied into this repository; the overlay fetches the pinned upstream sources during the vcpkg build.
- Project CMake consumes only `unofficial::libmupdf::libmupdf`.
- There is no `MUPDF_ROOT`, MuPDF DLL-client, or `mupdfcpp64` build path in v2.

On Windows, MuPDF and its third-party dependencies are static libraries built with the dynamic CRT triplet `x64-windows-static-md`. They are linked **privately** into the shared ExtractPDF wrapper:

```text
MuPDF 1.28.2 static libraries
          |
          | PRIVATE
          v
     extractpdf.dll
          |
          v
   C / C++ / .NET callers
```

Only the `extractpdf_*` ABI is exported by the wrapper.

## Build with vcpkg

Set `VCPKG_ROOT` to a vcpkg checkout compatible with the baseline in `vcpkg.json`. CI bootstraps the exact pinned commit declared by the repository workflow.

### Linux x64

```sh
"$VCPKG_ROOT/vcpkg" install \
  --triplet x64-linux \
  --overlay-ports="$PWD/vcpkg-ports"

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF

cmake --build build
ctest --test-dir build --output-on-failure
```

### macOS

Use `arm64-osx` on Apple Silicon or `x64-osx` on Intel, then run the same manifest/overlay flow.

### Windows x64 DLL

```powershell
& "$env:VCPKG_ROOT\vcpkg.exe" install `
  --triplet x64-windows-static-md `
  --overlay-ports="$PWD\vcpkg-ports"

cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DVCPKG_OVERLAY_PORTS="$PWD\vcpkg-ports" `
  -DBUILD_SHARED_LIBS=ON

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The test build stages `extractpdf.dll` beside every Windows test executable. Consumers are responsible for deploying `extractpdf.dll` beside their executable or otherwise making it available through normal Windows DLL search rules.

## Tests

CTest covers the document lifecycle plus Page + Render contracts, including invalid arguments, page geometry, MediaBox/CropBox coordinates, RGB/RGBA output, versioned render options, DPI, rotation, clipping, thumbnails, encrypted PDFs, malformed input, repeated lifecycle stress, interleaved handles, and UTF-8 paths.

Linux additionally runs AddressSanitizer and UndefinedBehaviorSanitizer.

## CI

Normal pull-request updates use Linux as the fast development loop. Windows and macOS are reserved for explicit `full-ci` checkpoints, manual workflow dispatch, and pushes to `master`.

The workflow persists vcpkg binary packages through GitHub Actions cache, keyed by OS/architecture, pinned vcpkg commit, manifest, and overlay content. This avoids rebuilding the MuPDF/HarfBuzz/FreeType dependency graph on each RED/GREEN iteration while leaving vcpkg's package ABI checks authoritative.

A feature is not considered cross-platform complete until Linux, macOS, and Windows pass on the same exact head SHA. Older green runs do not satisfy acceptance for a newer head.

## Legacy implementation

The root `libpdf.c` is the original MuPDF 1.3 experiment from 2015. It remains untouched during the v2 migration for historical reference and is not the supported v2 API.

## License

ExtractPDF is distributed under **AGPL-3.0-or-later**. See `LICENSE`.

MuPDF is an upstream dependency with its own AGPL/commercial licensing options. Downstream users should review the applicable licenses for their distribution model.
