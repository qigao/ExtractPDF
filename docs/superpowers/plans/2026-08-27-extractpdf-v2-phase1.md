# ExtractPDF v2 Phase 1 implementation plan

Date: 2026-08-27  
Status: implementation complete except final exact-head verification

**Goal:** establish a stable C11 lifecycle/error ABI over MuPDF 1.28.2 with deterministic fixtures, CTest, and exact-head Windows/Linux/macOS CI.

**Canonical architecture:** every supported desktop platform uses the same pinned vcpkg baseline and the repository's MuPDF 1.28.2 overlay port. MuPDF stays private to ExtractPDF. Windows links static MuPDF into `extractpdf.dll`; Linux/macOS build ExtractPDF static in CI.

**Spec:** `docs/superpowers/specs/2026-08-27-extractpdf-v2-design.md`

## Global constraints

- MuPDF baseline is exactly 1.28.2.
- Public headers contain no MuPDF include/type.
- Each handle owns one `fz_context` and `fz_document`.
- No mutable process-global or thread-local document state in ExtractPDF-owned code.
- MuPDF exceptions are caught before crossing the C ABI.
- Public paths are UTF-8.
- Phase 1 is single-threaded.
- CTest is the only test entry point.
- ExtractPDF-owned C code treats warnings as errors.
- Open-source repository license is AGPL-3.0-or-later.
- Root `libpdf.c` remains untouched in Phase 1.

## Task 1 — license and public ABI

- [x] Add AGPL-3.0-or-later root license.
- [x] Add opaque `extractpdf_document` public handle.
- [x] Lock status enum values `0..7` and immutable status strings.
- [x] Export `extractpdf_open`, `extractpdf_page_count`, `extractpdf_status_string`, and `extractpdf_close`.
- [x] Add Windows export/import macro scoped to ExtractPDF only.
- [x] Establish dependency-free RED/GREEN status test.

## Task 2 — MuPDF lifecycle and error boundaries

- [x] Add deterministic 1-page and 2-page fixtures.
- [x] Implement private `{fz_context *, fz_document *}` handle ownership.
- [x] Wrap document open/page count in MuPDF exception boundaries.
- [x] Null output handle on open failure.
- [x] Map allocation/context failure to `EXTRACTPDF_ERROR_NOMEM`.
- [x] Add missing-file, malformed-file, and password behavior.
- [x] Add 100 repeated open/count/close cycles.
- [x] Add two-handle interleaving/isolation test.
- [x] Keep `extractpdf_close(NULL)` safe.

## Task 3 — canonical vcpkg dependency model

- [x] Add root `vcpkg.json` with pinned builtin baseline.
- [x] Add `vcpkg-ports/libmupdf` overlay pinned to MuPDF 1.28.2.
- [x] Fetch the exact MuJS gitlink needed by MuPDF 1.28.2.
- [x] Disable Phase-1-unused Markdown (`FZ_ENABLE_MD=0`) instead of pulling cmark-gfm.
- [x] Disable Phase-1-unused hyphenation (`FZ_ENABLE_HYPHEN=0`) instead of reproducing upstream hyphen resource embedding.
- [x] Disable JavaScript/OCR and other unused output features already outside Phase 1.
- [x] Restore MuPDF's host-side vcpkg dependency on Windows solely for the `bin2coff` build tool.
- [x] Use `x64-windows-static-md` for static MuPDF + `/MD` compatibility with the shared wrapper.
- [x] Make project CMake consume only `unofficial::libmupdf::libmupdf`.
- [x] Remove the legacy `MUPDF_ROOT` / `MuPDF::MuPDF` / `mupdfcpp64` / `FZ_DLL_CLIENT` fallback.

## Task 4 — UTF-8 and Windows shared-library runtime

- [x] Create a configured UTF-8 fixture path (`extractpdf-测试.pdf`).
- [x] Compile the MSVC UTF-8 path test with `/utf-8`.
- [x] Build `extractpdf.dll` on Windows while keeping MuPDF private/static.
- [x] Stage `extractpdf.dll` beside each Windows test executable so loader behavior is deterministic.
- [x] Add diagnostic markers to status/document tests.
- [x] Add bounded CTest timeouts so runtime regressions cannot occupy CI indefinitely.

## Task 5 — exact-version cross-platform CI

- [x] Feature branches run one PR workflow rather than duplicate push + PR workflows.
- [x] `master` retains push verification.
- [x] Linux bootstraps pinned vcpkg, installs overlay MuPDF 1.28.2, builds/tests static ExtractPDF, then runs ASan/UBSan.
- [x] macOS bootstraps the same vcpkg baseline, selects `arm64-osx` or `x64-osx`, and builds/tests static ExtractPDF.
- [x] Windows bootstraps the same baseline, installs `x64-windows-static-md`, builds `extractpdf.dll`, and runs CTest.
- [ ] Record one final exact head where Linux, macOS, and Windows are all green.

## Canonical verification commands

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

### Windows x64

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

macOS follows the Linux flow with `arm64-osx` or `x64-osx`.

## Final Phase 1 acceptance

- [x] Root AGPL license and supported v2 README.
- [x] Public header has no MuPDF types/includes.
- [x] No mutable ExtractPDF-owned global/TLS document state.
- [x] CTest covers argument, IO, page-count, password, malformed-input, lifecycle, isolation, NULL-close, and UTF-8 behavior.
- [x] Linux sanitizer configuration is part of required CI.
- [x] Windows architecture is static MuPDF privately linked into shared ExtractPDF.
- [x] All desktop platforms use the same pinned vcpkg + MuPDF overlay dependency model.
- [x] Legacy `libpdf.c` remains untouched.
- [ ] Linux/macOS/Windows all pass on one exact PR head SHA.

Phase 2 text work starts only after the final exact-head checkbox is green.
