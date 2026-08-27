# ExtractPDF

ExtractPDF is a small C11 library that wraps MuPDF behind a stable C ABI suitable for native callers and .NET P/Invoke.

The current v2 foundation targets **MuPDF 1.28.2**. It replaces the original 2015 MuPDF 1.3 proof of concept with explicit document ownership, error/status handling, CMake/CTest, and cross-platform CI.

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

Phase 1 exports:

- `extractpdf_open`
- `extractpdf_page_count`
- `extractpdf_status_string`
- `extractpdf_close`

Text and image extraction are intentionally deferred until the lifecycle/error foundation is green on every supported platform.

## API contract

- The public header contains no MuPDF types.
- Each `extractpdf_document` owns its own MuPDF context and document.
- ExtractPDF keeps no mutable process-global or thread-local document state.
- Input paths are UTF-8.
- `password == NULL` means no password was supplied.
- Password-required or incorrect-password opens return `EXTRACTPDF_ERROR_PASSWORD`.
- `extractpdf_close(NULL)` is safe.
- MuPDF exceptions are caught inside the library and translated to `extractpdf_status` values.
- The library suppresses MuPDF's default stderr diagnostics at its private context boundary; callers use status values rather than parsing dependency output.
- Phase 1 does not promise concurrent calls on one document handle. Separate handles may coexist, but use them from one thread until an explicit multi-threading contract is added and tested.

## Build

Requirements:

- CMake 3.20 or newer
- a C11 compiler
- MuPDF 1.28.2 for the tested baseline

MuPDF is an external dependency and is not vendored by this repository. Build or install MuPDF, then point `MUPDF_ROOT` at its source/install root.

### Linux / macOS

A MuPDF source tree can provide the required static libraries with:

```sh
git clone --branch 1.28.2 --depth 1 --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git
make -C mupdf -j2 build=release HAVE_LIBCRYPTO=no libs
```

Then build ExtractPDF:

```sh
cmake -S . -B build -DMUPDF_ROOT=/absolute/path/to/mupdf -DBUILD_SHARED_LIBS=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows x64

Build MuPDF's official DLL-client library first:

```powershell
msbuild C:\path\to\mupdf\platform\win32\mupdfcpp.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

Then configure ExtractPDF as a shared library:

```powershell
cmake -S . -B build -A x64 -DMUPDF_ROOT=C:\path\to\mupdf -DBUILD_SHARED_LIBS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

When running the tests or a consumer, make sure MuPDF's `platform\win32\x64\Release` directory is on `PATH` so `mupdfcpp64.dll` can be loaded.

## Tests

CTest covers the Phase 1 behavior contract, including:

- invalid arguments
- missing files
- one- and two-page documents
- encrypted documents with no, wrong, and correct passwords
- malformed/truncated input
- repeated open/count/close cycles
- interleaved independent handles
- `extractpdf_close(NULL)`
- UTF-8 filenames

CI runs the same contract against MuPDF 1.28.2 on Linux, macOS, and Windows. Linux also runs AddressSanitizer and UndefinedBehaviorSanitizer over ExtractPDF-owned code and tests.

## Legacy implementation

The root `libpdf.c` is the original MuPDF 1.3 experiment from 2015. It remains untouched during the v2 migration for historical reference, but it is **not** the supported v2 API and should not be used as the basis for new integrations.

Once the v2 lifecycle, text, and image surfaces are covered, the legacy file can be moved intact under `legacy/` as a separate migration step.

## License

ExtractPDF is distributed under the **GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later)**. See `LICENSE`.

MuPDF is provided by Artifex under AGPL and commercial licensing options. Using a DLL wrapper does not change MuPDF's licensing obligations. Projects that require proprietary/commercial MuPDF terms must obtain the appropriate Artifex license and separately ensure that their use of ExtractPDF is compatible with ExtractPDF's license.
