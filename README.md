# ExtractPDF

ExtractPDF is a small C11 library that keeps MuPDF behind a stable C ABI suitable for native callers and .NET P/Invoke.

The v2 foundation targets **MuPDF 1.28.2**. It replaces the original 2015 MuPDF 1.3 proof of concept with explicit document ownership, stable status handling, deterministic tests, CMake/CTest, and exact-head Windows/Linux/macOS CI.

## Phase 1 API

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

Phase 1 exports only:

- `extractpdf_open`
- `extractpdf_page_count`
- `extractpdf_status_string`
- `extractpdf_close`

Text extraction is Phase 2. Page-observed image extraction is Phase 3.

## API contract

- The public header contains no MuPDF types.
- Each `extractpdf_document` owns one MuPDF context and document.
- ExtractPDF keeps no mutable process-global or thread-local document state.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Missing or incorrect passwords return `EXTRACTPDF_ERROR_PASSWORD`.
- `extractpdf_open` leaves the output handle NULL on failure.
- `extractpdf_close(NULL)` is safe.
- MuPDF exceptions are caught inside the library and translated to `extractpdf_status`.
- Phase 1 is deliberately single-threaded. Separate handles may coexist and be used sequentially/interleaved on one thread; concurrent MuPDF calls are not yet part of the contract.

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

Set `VCPKG_ROOT` to a vcpkg checkout compatible with the baseline in `vcpkg.json`. CI bootstraps the exact baseline commit declared by the repository workflow.

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

Use `arm64-osx` on Apple Silicon or `x64-osx` on Intel, then run the same manifest/overlay flow. For example:

```sh
triplet=arm64-osx
"$VCPKG_ROOT/vcpkg" install \
  --triplet "$triplet" \
  --overlay-ports="$PWD/vcpkg-ports"

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="$triplet" \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF

cmake --build build
ctest --test-dir build --output-on-failure
```

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

The test build stages `extractpdf.dll` beside the Windows test executables. Consumers are responsible for deploying `extractpdf.dll` beside their executable or otherwise making it available through normal Windows DLL search rules.

## Tests

CTest covers the Phase 1 contract, including invalid arguments, missing files, one/two-page counts, encrypted PDFs with missing/wrong/correct passwords, malformed input, 100 repeated open/count/close cycles, interleaved independent handles, `close(NULL)`, and a UTF-8 filename.

Linux additionally runs AddressSanitizer and UndefinedBehaviorSanitizer. Windows tests have explicit runtime bounds so a loader or lifecycle regression cannot occupy CI indefinitely.

## CI

Feature branches are validated by the pull-request workflow. `master` is validated again on push after merge. All three desktop jobs bootstrap the same pinned vcpkg baseline and consume the same MuPDF 1.28.2 overlay port.

The supported proof is always the CI result on the **exact PR head SHA**; an older green run is not considered evidence for a newer head.

## Legacy implementation

The root `libpdf.c` is the original MuPDF 1.3 experiment from 2015. It remains untouched during the v2 migration for historical reference and is not the supported v2 API.

After the v2 lifecycle, text, and image surfaces are covered, it can be moved intact under `legacy/` as a separate migration step.

## License

ExtractPDF is distributed under **AGPL-3.0-or-later**. See `LICENSE`.

MuPDF is an upstream dependency with its own AGPL/commercial licensing options. Downstream users should review the applicable licenses for their distribution model.
