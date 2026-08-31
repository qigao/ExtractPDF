# PDFium Immutable Read Path Implementation Plan

> **For agentic workers:** execute one task at a time, preserve the public C ABI, and run the focused plus full preset verification after every backend cutover.

**Goal:** Move QuantaPDF document/page lifecycle, rendering, text/search, image observation, and links from MuPDF to PDFium while keeping unported mutation/composition modules operational during the migration branch.

**Architecture:** `quantapdf_document` owns immutable source bytes for its lifetime. A private C++ PDFium adapter owns all `FPDF_*` handles and enters the serialized runtime for every call. Transitional MuPDF handles may coexist in the internal struct only for feature modules not yet ported; no public operation has a runtime backend selector. Each migrated public API becomes PDFium-authoritative and its former MuPDF implementation is removed immediately.

**Tech Stack:** C11 public/feature modules, C++20 private PDFium adapters, PDFium public C API `154.0.8021.0`, existing CTest fixtures, Windows Release plus Linux sanitizer and macOS CI.

**Foundation:** `docs/superpowers/specs/2026-08-31-quantapdf-pdfium-qpdf-migration-design.md`

## Global constraints

- Include only PDFium headers from the artifact's public `include/` directory.
- Every PDFium call occurs while `quantapdf_pdfium_enter()` holds the process mutex.
- Immutable source bytes outlive `FPDF_DOCUMENT`.
- C++ exceptions and `FPDF_*` types never enter `include/quantapdf/quantapdf.h`.
- Do not add a PDFium/MuPDF selector, fallback, or parity mode.
- A migrated API has one authority: PDFium. MuPDF may remain only for other unported APIs.
- Initialize output handles/counts according to the existing public tests and publish nothing on failure.
- Preserve UTF-8 Windows paths.
- Every output bitmap/snapshot owns its bytes and can satisfy its documented lifetime independently of the source page handle.

---

### Task 1: Make PDFium authoritative for document and page lifecycle

**Files:**
- Create: `src/input_file.h`
- Create: `src/input_file.c`
- Create: `src/backend/pdfium_document.h`
- Create: `src/backend/pdfium_document.cpp`
- Modify: `src/internal.h`
- Modify: `src/document.c`
- Modify: `src/page.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_document.c`

**Interfaces:**

```c
quantapdf_status quantapdf_pdfium_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_pdfium_document **out_document);
quantapdf_status quantapdf_pdfium_page_count(
    quantapdf_pdfium_document *document,
    int *out_page_count);
quantapdf_status quantapdf_pdfium_load_page(
    quantapdf_pdfium_document *document,
    int page_index,
    quantapdf_pdfium_page **out_page);
quantapdf_status quantapdf_pdfium_page_bounds(
    quantapdf_pdfium_page *page,
    quantapdf_rect *out_bounds);
void quantapdf_pdfium_drop_page(quantapdf_pdfium_page *page);
void quantapdf_pdfium_close(quantapdf_pdfium_document *document);
```

- [x] Read the complete file into QuantaPDF-owned bytes with `_wfopen_s` after strict UTF-8 conversion on Windows and `fopen` elsewhere. Detect seek/size/allocation/read failures without publishing partial data.
- [x] Map `FPDF_GetLastError()` exactly: file to `IO`, format/page to `FORMAT`, password to `PASSWORD`, security to `UNSUPPORTED`, unknown to `BACKEND`.
- [x] Extend the transitional document/page structs with opaque PDFium handles. Source bytes are freed only after the PDFium document closes.
- [x] Open PDFium and the transitional MuPDF document from the same file. Failure of either backend aborts construction; there is no fallback.
- [x] Route public page count and page bounds through PDFium. Route page-index validation through PDFium before loading the transitional MuPDF page required by unported features.
- [x] Add repeated open/page/drop tests and static checks proving `document.c` no longer calls MuPDF page counting and `page.c` no longer uses MuPDF for generic page bounds.
- [x] Run `quantapdf.document`, then the full Windows Release suite.

---

### Task 2: Migrate MediaBox/CropBox geometry

**Files:**
- Modify: `src/backend/pdfium_document.h`
- Modify: `src/backend/pdfium_document.cpp`
- Modify: `src/page.c`
- Modify: `tests/test_document.c`
- Add focused rotated/UserUnit/inherited box fixtures only if existing fixtures do not cover the conversion.

- [ ] Read MediaBox/CropBox through `FPDFPage_GetMediaBox` and `FPDFPage_GetCropBox`.
- [ ] Convert raw PDF coordinates into the existing public CropBox-origin, y-down displayed page space while honoring page rotation and UserUnit.
- [ ] Fail closed on non-finite, inverted, or inconsistent boxes instead of adopting tolerant repair behavior.
- [ ] Remove `fz_bound_page_box` from the generic Page API.

---

### Task 3: Migrate page and thumbnail rendering

**Files:**
- Create: `src/backend/pdfium_render.h`
- Create: `src/backend/pdfium_render.cpp`
- Modify: `src/internal.h`
- Modify: `src/render.c`
- Modify: `tests/test_render.c`

- [ ] Render through `FPDFBitmap_CreateEx` and `FPDF_RenderPageBitmapWithMatrix` using checked integer bounds.
- [ ] Normalize PDFium BGR/BGRA output into QuantaPDF-owned interleaved RGB/RGBA bytes.
- [ ] Preserve premultiplied-alpha, stride, arbitrary rotation, clipping, white opaque background, and transparent-alpha contracts.
- [ ] Keep bitmap observation/drop backend-neutral so later image decoding can reuse the same owned-byte representation.
- [ ] Remove every `fz_pixmap` and draw-device dependency from generic rendering.

---

### Task 4: Migrate plain and structured text plus search

**Files:**
- Create: `src/backend/pdfium_text.h`
- Create: `src/backend/pdfium_text.cpp`
- Modify: `src/internal.h`
- Modify: `src/text.c`
- Modify: `src/structured_text.c`
- Modify: `src/search.c`
- Modify: text/search tests

- [ ] Load `FPDF_TEXTPAGE` under the runtime lock and copy UTF-16 results into validated UTF-8 owned storage.
- [ ] Project character boxes, angles, font size/weight/style/color, and line grouping into existing snapshots without exposing PDFium handles.
- [ ] Define deterministic block/line/span grouping from PDFium characters and preserve existing fixtures or explicitly document a narrowed semantic boundary.
- [ ] Use PDFium search APIs or the owned normalized character sequence, but keep one shared geometry source for text and search.
- [ ] Remove all Fitz structured-text types from internal snapshot storage.

---

### Task 5: Migrate image occurrences and bitmap decode

**Files:**
- Create: `src/backend/pdfium_images.h`
- Create: `src/backend/pdfium_images.cpp`
- Modify: `src/internal.h`
- Modify: `src/images.c`
- Modify: `src/image_bitmap.c`
- Modify: image tests

- [ ] Traverse public PDFium page objects recursively and record image occurrences with transformed quads.
- [ ] Query intrinsic image metadata through public `FPDFImageObj_*` APIs.
- [ ] Decode into the same owned RGB/RGBA bitmap representation used by page rendering.
- [ ] Preserve occurrence order and alpha semantics; reject unsupported image-mask cases explicitly.
- [ ] Remove all retained `fz_image` ownership.

---

### Task 6: Migrate links and close the immutable-read gate

**Files:**
- Create: `src/backend/pdfium_links.h`
- Create: `src/backend/pdfium_links.cpp`
- Modify: `src/links.c`
- Modify: link tests
- Modify: `README.md`

- [ ] Enumerate annotations/links with public PDFium APIs, copy URI bytes, and resolve internal destinations to page index and public coordinates.
- [ ] Preserve deterministic ordering and fail closed for malformed destinations.
- [ ] Remove all `fz_link` use and transitional longjmp handling from `links.c`.
- [ ] Run static residue checks for the migrated files, full Windows tests, Linux Release/ASan/UBSan, and macOS on one SHA.
- [ ] Update README only for APIs whose PDFium cutover is proven.

## Phase acceptance

The phase is complete only when document/page/render/text/search/images/links contain no MuPDF types or calls, all existing public tests pass, the same immutable bytes back PDFium and lazy qpdf, and three-platform CI is green on the exact phase SHA.
