# QuantaPDF Page Image Occurrence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add immutable page-content image-occurrence enumeration with geometry and intrinsic metadata while retaining exact MuPDF image references for a later decode slice.

**Architecture:** A custom external MuPDF device records each `fill_image` callback from `fz_run_page_contents()`. The snapshot copies public metadata/geometry and retains each `fz_image`; callers traverse it through count/info accessors. The snapshot can outlive its source page but not its parent document.

**Tech Stack:** C11, MuPDF 1.28.2 Fitz device/image APIs, CMake/CTest, Linux ASan/UBSan, GitHub Actions Linux-first CI.

**Spec:** `docs/superpowers/specs/2026-08-27-quantapdf-page-images-design.md`

## Global Constraints

- Model one occurrence per normal MuPDF `fill_image` callback, not one PDF/xref image resource.
- V1 excludes `fill_image_mask` / `clip_image_mask` stencil operations.
- Run only `fz_run_page_contents()` so annotation/widget appearances are outside V1.
- Reuse existing public `quantapdf_quad` and Fitz page-space coordinates.
- Do not decode image pixels in this plan.
- Retain exact `fz_image` references for the future decode slice.
- Source page may be dropped; parent document must outlive `quantapdf_image_page`.
- No MuPDF type crosses the public ABI.
- Linux normal CTest + ASan/UBSan is the development gate; Windows/macOS wait for the Phase 3 Content checkpoint.

---

### Task 1: Lock image occurrence enumeration with a real RED

**Files:**
- Create: `tests/fixtures/page-images.pdf`
- Create: `tests/test_images.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `quantapdf_page`, `quantapdf_quad`, page/document lifetime.
- Produces required names for Task 2: `quantapdf_image_page`, `quantapdf_image_info`, `quantapdf_extract_images`, `quantapdf_image_count`, `quantapdf_image_get_info`, `quantapdf_drop_image_page`.

- [ ] **Step 1: Add deterministic PDF fixture**

Create one 200x100 page containing one 2x1 DeviceRGB 8-bpc image XObject painted twice:

```pdf
q
40 0 0 20 10 20 cm
/Im0 Do
Q
q
20 0 0 40 100 50 cm
/Im0 Do
Q
```

The image stream is six raw bytes representing two RGB pixels. Generate exact stream `/Length`, xref offsets and `startxref`; do not rely on repair mode.

- [ ] **Step 2: Add contract test**

`tests/test_images.c` must:

```c
quantapdf_image_page *images = NULL;
quantapdf_image_info info0 = { sizeof(info0) };
quantapdf_image_info info1 = { sizeof(info1) };
size_t count = 0;

CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
quantapdf_drop_page(page);
page = NULL;

CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK);
CHECK(count == 2);
CHECK(quantapdf_image_get_info(images, 0, &info0) == QUANTAPDF_OK);
CHECK(quantapdf_image_get_info(images, 1, &info1) == QUANTAPDF_OK);
CHECK(info0.pixel_width == 2 && info0.pixel_height == 1);
CHECK(info1.pixel_width == 2 && info1.pixel_height == 1);
CHECK(info0.components == 3 && info1.components == 3);
CHECK(info0.bits_per_component == 8 && info1.bits_per_component == 8);
CHECK(info0.has_alpha == 0 && info1.has_alpha == 0);
```

Derive axis-aligned min/max bounds from each quad and assert the two placements are distinct and match the chosen page-space positions with a small float tolerance.

- [ ] **Step 3: Wire CTest and Windows DLL staging**

Add `quantapdf_test_images` to `tests/CMakeLists.txt`, define `PAGE_IMAGES_PDF` and `ONE_PAGE_PDF`, add `quantapdf.images` CTest, and include the test target in Windows post-build DLL copy staging.

- [ ] **Step 4: Verify RED**

Run Linux CI or compile the exact test against the pre-feature header. Expected failure: unknown `quantapdf_image_page` / `quantapdf_image_info` and missing `quantapdf_extract_images` / count/info/drop declarations.

- [ ] **Step 5: Commit RED**

```bash
git add tests/fixtures/page-images.pdf tests/test_images.c tests/CMakeLists.txt
git commit -m "test: define page image occurrence contract"
```

---

### Task 2: Implement occurrence capture and immutable traversal

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Modify: `src/internal.h`
- Create: `src/images.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes MuPDF `fz_device`, `fz_run_page_contents`, `fz_keep_image`, `fz_drop_image`, `fz_image` intrinsic fields.
- Produces the public image snapshot API defined in Task 1.

- [ ] **Step 1: Add public ABI**

Append:

```c
typedef struct quantapdf_image_page quantapdf_image_page;

typedef struct quantapdf_image_info {
    size_t struct_size;
    quantapdf_quad quad;
    int pixel_width;
    int pixel_height;
    int components;
    int bits_per_component;
    int has_alpha;
} quantapdf_image_info;
```

and the four public functions from the spec.

- [ ] **Step 2: Add private snapshot model**

Add an internal occurrence record with retained `fz_image *`, public quad and copied metadata. `quantapdf_image_page` stores a non-owning `quantapdf_document *document`, dynamic occurrence array, count and capacity.

- [ ] **Step 3: Implement custom capture device**

Create a derived device:

```c
typedef struct quantapdf_image_capture_device {
    fz_device super;
    quantapdf_image_page *snapshot;
    int oom;
} quantapdf_image_capture_device;
```

Set only `super.fill_image`. The callback grows the array with overflow-safe `realloc`, calls `fz_keep_image`, copies metadata, and transforms the four unit-square corners through `ctm` into `quantapdf_quad`.

- [ ] **Step 4: Implement extraction lifecycle**

`quantapdf_extract_images` must set output NULL first, allocate a zero-count snapshot, construct the capture device, run `fz_run_page_contents(ctx, page->page, dev, fz_identity, NULL)` inside `fz_try/fz_always/fz_catch`, close/drop the device, unwind partial retained images on MuPDF/OOM failure, and return a valid snapshot on an image-free page.

- [ ] **Step 5: Implement traversal/error semantics**

`quantapdf_image_count` resets count before validation. `quantapdf_image_get_info` validates `struct_size`, zeroes known V1 fields on valid-sized argument/index failure while preserving `struct_size`, and copies one occurrence on success. `quantapdf_drop_image_page(NULL)` is safe and drops every retained image before freeing storage.

- [ ] **Step 6: Wire `src/images.c` into root CMake**

Add it as one focused source beside `search.c`; do not modify Text/Search behavior.

- [ ] **Step 7: Verify GREEN**

Run Linux build + all CTests, then ASan/UBSan build + all CTests. Expected: all existing tests plus `quantapdf.images` pass with no sanitizer findings.

- [ ] **Step 8: Commit GREEN**

```bash
git add include/quantapdf/quantapdf.h src/internal.h src/images.c CMakeLists.txt
git commit -m "feat: enumerate page image occurrences"
```

---

### Task 3: Harden reset/versioning/empty-page behavior

**Files:**
- Modify: `tests/test_images.c`
- Modify only if RED requires it: `src/images.c`

**Interfaces:** same public API as Task 2.

- [ ] **Step 1: Add failure-contract tests**

Test NULL page/output, empty-page zero count, out-of-range info, count reset, minimum accepted `struct_size`, undersized struct rejection, tail-byte preservation for an oversized caller struct, and `drop_image_page(NULL)`.

- [ ] **Step 2: Verify the new test is RED if any contract is missing**

Run `quantapdf.images` through Linux CI/CTest and capture the first failing assertion.

- [ ] **Step 3: Apply only the minimal production fix needed by that RED**

Do not expand decoded-pixel scope.

- [ ] **Step 4: Re-run full Linux normal + ASan/UBSan CTest**

Expected: all tests pass and no sanitizer findings.

- [ ] **Step 5: Update #13 / PR / umbrella #2 with exact-head evidence**

Keep Windows/macOS deferred until the Phase 3 Content checkpoint.
