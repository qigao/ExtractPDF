# QuantaPDF v2 API Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the QuantaPDF v2 C ABI a verifiable release-candidate boundary.

**Architecture:** Keep the public header as the sole API definition, add an
explicit v2 export baseline checked against the real Windows DLL, and compile
fault injection locally into test executables. Add public version constants
that agree with CMake target versioning and document external serialization as
the v2 threading contract.

**Tech Stack:** C11, CMake 3.20+, CTest, MSVC `dumpbin`, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-v2-api-stability-design.md`

## Global Constraints

- PDFium and qpdf remain private implementation dependencies.
- `include/quantapdf/quantapdf.h` remains the sole installed public header.
- The existing public function signatures and enum numeric values do not
  change.
- The v2 public export set contains exactly 83 `quantapdf_*` functions.
- Windows operations use `win-release-user` from `VsDevCmd.bat`.
- Every behavior change follows RED, GREEN, REFACTOR.

---

### Task 1: Public version contract

**Files:**
- Create: `tests/test_version.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/quantapdf/quantapdf.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: CMake project version `2.0.0`.
- Produces: `QUANTAPDF_VERSION_MAJOR`, `QUANTAPDF_VERSION_MINOR`,
  `QUANTAPDF_VERSION_PATCH`, and `QUANTAPDF_ABI_VERSION`.

- [ ] **Step 1: Write the failing version test**

```c
#include <quantapdf/quantapdf.h>

#if QUANTAPDF_VERSION_MAJOR != 2
#error unexpected major version
#endif
#if QUANTAPDF_VERSION_MINOR != 0
#error unexpected minor version
#endif
#if QUANTAPDF_VERSION_PATCH != 0
#error unexpected patch version
#endif
#if QUANTAPDF_ABI_VERSION != 2
#error unexpected ABI version
#endif

int main(void)
{
    return 0;
}
```

Register it as `quantapdf.version` and link it to `QuantaPDF::QuantaPDF`.

- [ ] **Step 2: Run the focused build and verify RED**

Run from `VsDevCmd.bat`:

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_version
```

Expected: compilation fails because the four version macros are undefined.

- [ ] **Step 3: Add the public constants and target versions**

Add the four literal macros immediately after the API visibility macro in the
public header. Set the `quantapdf` target properties to:

```cmake
VERSION "${PROJECT_VERSION}"
SOVERSION "${PROJECT_VERSION_MAJOR}"
```

- [ ] **Step 4: Verify GREEN**

```bat
cmake --build --preset win-release-user --target quantapdf_test_version
ctest --preset win-release-user -R "^quantapdf.version$" --output-on-failure
```

Expected: one test passes.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/quantapdf/quantapdf.h tests/CMakeLists.txt tests/test_version.c
git commit -m "feat: publish QuantaPDF v2 version contract"
```

### Task 2: Exact shared-library export gate

**Files:**
- Create: `abi/quantapdf-v2.exports`
- Create: `cmake/CheckQuantaPDFExports.cmake`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `dumpbin`, `$<TARGET_FILE:quantapdf>`, and the committed baseline.
- Produces: CTest `quantapdf.abi_exports`.

- [ ] **Step 1: Commit the 83-symbol v2 baseline**

Create `abi/quantapdf-v2.exports` with one sorted public function name per
line using this exact content:

```text
quantapdf_annotation_contents
quantapdf_annotation_count
quantapdf_annotation_get_info
quantapdf_bitmap_data
quantapdf_bitmap_dimensions
quantapdf_close
quantapdf_crop_pages
quantapdf_document_form
quantapdf_document_metadata
quantapdf_document_outline
quantapdf_drop_annotation_page
quantapdf_drop_bitmap
quantapdf_drop_form
quantapdf_drop_image_page
quantapdf_drop_link_page
quantapdf_drop_outline
quantapdf_drop_output
quantapdf_drop_page
quantapdf_drop_pdf_edit
quantapdf_drop_text_page
quantapdf_export_page_range
quantapdf_export_pages
quantapdf_extract_annotations
quantapdf_extract_images
quantapdf_extract_links
quantapdf_extract_structured_text
quantapdf_extract_text
quantapdf_form_field_count
quantapdf_form_field_get_info
quantapdf_form_field_label
quantapdf_form_field_name
quantapdf_form_field_option_display
quantapdf_form_field_option_export
quantapdf_form_field_option_get_info
quantapdf_form_field_value_get_info
quantapdf_form_field_value_utf8
quantapdf_form_widget_count
quantapdf_form_widget_get_info
quantapdf_free
quantapdf_image_count
quantapdf_image_get_info
quantapdf_image_render
quantapdf_link_count
quantapdf_link_get_info
quantapdf_link_uri
quantapdf_load_page
quantapdf_merge_outputs
quantapdf_open
quantapdf_outline_count
quantapdf_outline_get_info
quantapdf_outline_title
quantapdf_outline_uri
quantapdf_output_data
quantapdf_output_save_file
quantapdf_page_bounds
quantapdf_page_box_bounds
quantapdf_page_count
quantapdf_pdf_edit_annotation_contents
quantapdf_pdf_edit_annotation_count
quantapdf_pdf_edit_annotation_create
quantapdf_pdf_edit_annotation_delete
quantapdf_pdf_edit_annotation_get_info
quantapdf_pdf_edit_annotation_ref_at
quantapdf_pdf_edit_annotation_update
quantapdf_pdf_edit_begin
quantapdf_pdf_edit_form_field_ref_at
quantapdf_pdf_edit_form_set_values
quantapdf_pdf_edit_form_snapshot
quantapdf_pdf_edit_snapshot
quantapdf_poster_split_pages
quantapdf_render_page
quantapdf_render_page_with_options
quantapdf_render_thumbnail
quantapdf_status_string
quantapdf_text_block_count
quantapdf_text_get_block_info
quantapdf_text_get_line_info
quantapdf_text_get_span_info
quantapdf_text_line_count
quantapdf_text_search
quantapdf_text_span_count
quantapdf_text_span_text
quantapdf_trim_pages
```

- [ ] **Step 2: Write the failing real-DLL export test**

`cmake/CheckQuantaPDFExports.cmake` must:

```cmake
execute_process(
  COMMAND "${QUANTAPDF_DUMPBIN}" /nologo /exports "${QUANTAPDF_LIBRARY}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dumpbin failed: ${error}")
endif()
string(REGEX MATCHALL "quantapdf_[A-Za-z0-9_]+" actual "${output}")
list(REMOVE_DUPLICATES actual)
list(SORT actual)
file(STRINGS "${QUANTAPDF_EXPORT_BASELINE}" expected
  REGEX "^quantapdf_[A-Za-z0-9_]+$")
list(REMOVE_DUPLICATES expected)
list(SORT expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "QuantaPDF ABI export mismatch\nExpected: ${expected}\nActual: ${actual}")
endif()
```

On Windows shared builds, locate `dumpbin` and register the script as
`quantapdf.abi_exports`.

- [ ] **Step 3: Run and verify RED**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf
ctest --preset win-release-user -R "^quantapdf.abi_exports$" --output-on-failure
```

Expected: failure reports the two unexpected `quantapdf_test_*` exports.

- [ ] **Step 4: Commit the RED test**

```bash
git add abi/quantapdf-v2.exports cmake/CheckQuantaPDFExports.cmake tests/CMakeLists.txt
git commit -m "test: enforce the QuantaPDF v2 export baseline"
```

### Task 3: Keep fault injection out of the DLL

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/pdf_edit_test_api.h`
- Modify: `tests/pdf_poster_test_api.h`

**Interfaces:**
- Consumes: the existing fault hook C files and test-only private structure
  fields enabled by `QUANTAPDF_TESTING`.
- Produces: locally linked test hooks with ordinary C linkage.

- [ ] **Step 1: Remove hooks from the production target**

Keep `QUANTAPDF_TESTING=1` on `quantapdf`, but remove both hook C files from
`target_sources(quantapdf ...)`.

- [ ] **Step 2: Compile hooks into their consumers**

Add `pdf_edit_fault_hook.c` to annotation-mutation and form-mutation test
targets. Add `pdf_poster_fault_hook.c` to the poster-split test target. Define
`QUANTAPDF_TESTING=1` privately on those three executable targets so the hook
translation units see the same private structure layouts as the DLL.

- [ ] **Step 3: Remove DLL decoration from test declarations**

Change both test declarations from `QUANTAPDF_API void` to plain `void`. The
public QuantaPDF calls remain imported through the linked target; only the
test-local functions lose DLL import decoration.

- [ ] **Step 4: Verify GREEN and rollback behavior**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user -R "quantapdf.(abi_exports|pdf_annotation_mutation|pdf_form_mutation|pdf_poster_split)" --output-on-failure
```

Expected: export baseline and all three fault-injection behavior groups pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/pdf_edit_test_api.h tests/pdf_poster_test_api.h
git commit -m "fix: isolate fault injection from the public DLL"
```

### Task 4: Freeze and verify the v2 contract

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: version macros and exact export gate.
- Produces: documented v2 compatibility and threading policy.

- [ ] **Step 1: Replace the temporary threading statement**

Document that all v2 API calls require external serialization, including calls
using different documents. State that additive functions and enum values are
allowed, existing fields and numeric values are retained, and incompatible
changes require ABI version 3.

- [ ] **Step 2: Run complete local verification**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user
```

Expected: configure/build/install succeed and all 28 CTests pass.

- [ ] **Step 3: Inspect the installed DLL export table**

```bat
dumpbin /exports "%PKG_ROOT%\quantapdf\release\bin\quantapdf.dll"
```

Expected: exactly the 83 baseline symbols and no `quantapdf_test_*` names.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: define the QuantaPDF v2 compatibility policy"
```

- [ ] **Step 5: Push and run cross-platform CI**

Push `feat/v2-api-stability`, create a pull request, and require Linux Release
plus sanitizer, macOS, and Windows checks on the same head SHA.
