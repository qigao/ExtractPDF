# QuantaPDF PDF Outline / Bookmarks V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PDF-only immutable outline/bookmarks snapshot with flat preorder hierarchy indices, stable navigation semantics, strict read-only structural validation, document-independent lifetime, and a bounded MuPDF 1.28.2 recursion envelope.

**Architecture:** Reuse the existing opaque `quantapdf_document`. Before creating MuPDF's PDF outline iterator, perform an QuantaPDF-owned iterative read-only preflight over `/Root/Outlines` that rejects repairable structural inconsistencies and cycles and records maximum depth; only valid trees at depth <= 256 may enter MuPDF's recursive iterator validation. Flatten decoded items iteratively into one QuantaPDF-owned snapshot containing a node array plus UTF-8 string arena; no MuPDF pointer survives publication.

**Tech Stack:** C11, MuPDF 1.28.2 pinned through the existing vcpkg overlay, PDF object/Fitz outline APIs, CMake/CTest, Linux ASan/UBSan, macOS CI, Windows DLL CI, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-28-quantapdf-document-outline-design.md`

## Global Constraints

- Base is integrated `master` exact SHA `cb0f50b0734bcae00fedd529e4f13701c0852866`.
- Work on `feat/document-outline`; child issue is #33; umbrella is #2.
- V1 is PDF-only. Non-PDF documents return `QUANTAPDF_ERROR_UNSUPPORTED`.
- Reuse `quantapdf_document`; do not introduce a second public PDF document handle.
- Public hierarchy is one immutable `quantapdf_outline` snapshot in global preorder with snapshot-local `parent_index`, `first_child_index`, and `next_sibling_index` relations.
- `SIZE_MAX` means no parent, no first child, or no next sibling. Do not add an `QUANTAPDF_INDEX_NONE` macro.
- Destination kinds are NONE / INTERNAL / URI. INTERNAL exposes zero-based flat PDF page index + Fitz page-space x/y. URI exposes snapshot-owned borrowed UTF-8.
- Titles are snapshot-owned borrowed UTF-8 and distinguish absent (`NULL/0`) from present-empty (non-NULL empty string, size 0).
- V1 exposes only `is_open`; bold/italic/RGB presentation fields remain deferred.
- Empty PDF outline is success with a non-NULL snapshot whose count is zero.
- The completed snapshot remains valid after source document close and retains no MuPDF/document pointer.
- QuantaPDF-owned preflight and flattening are iterative and heap-backed.
- Preflight rejects any structure MuPDF would otherwise repair: bad Parent/Prev/parent-Last relationships, non-indirect outline items, repeated nodes, or cycles -> `QUANTAPDF_ERROR_FORMAT`.
- Preflight validates the entire tree before choosing the depth result: malformed always wins `FORMAT`; only a structurally valid tree whose maximum depth exceeds 256 returns `QUANTAPDF_ERROR_UNSUPPORTED`.
- Do not invoke MuPDF's outline iterator for a tree that failed preflight or whose valid maximum depth is > 256.
- A non-null internal destination that cannot resolve to a valid PDF page makes the whole extraction `QUANTAPDF_ERROR_FORMAT`.
- No partial snapshot may be published on any failure.
- Snapshot indices are never persistent bookmark IDs, PDF object numbers, cross-snapshot identities, or future mutation handles.
- Do not modify `src/document.c`, `src/internal.h`, `src/links.c`, `src/pdf_metadata.c`, Phase 4 composition/output implementation, or existing Page/Render/Text/Search/Image/Links behavior.
- MuPDF types never cross the public C ABI.
- Preserve a real RED commit before adding any public outline declaration or production implementation.
- Final acceptance requires the exact GREEN head to pass Linux strict static/all CTests, Linux ASan/UBSan/all CTests, macOS configure/build/test, and Windows DLL configure/build/test.
- Do not merge this slice during plan execution; leave the implementation PR draft/open for an explicit integration decision.

---

## File Structure

**Create during RED**
- `tests/fixtures/outline-tree.pdf`
- `tests/fixtures/outline-repairable-bad.pdf`
- `tests/fixtures/outline-cycle.pdf`
- `tests/fixtures/outline-depth-257.pdf`
- `tests/test_pdf_outline.c`

**Modify during RED**
- `tests/CMakeLists.txt`

**Create during GREEN**
- `src/pdf_outline.c`

**Modify during GREEN**
- `include/quantapdf/quantapdf.h`
- `CMakeLists.txt`

**Reuse unchanged**
- `src/pdf_internal.h`
- `src/document.c`
- `tests/fixtures/composition-three-page.pdf`
- `tests/fixtures/composition-non-pdf.txt`
- `.github/workflows/ci.yml`

---

### Task 1: Strict RED for the immutable PDF outline contract

**Files:**
- Create: `tests/fixtures/outline-tree.pdf`
- Create: `tests/fixtures/outline-repairable-bad.pdf`
- Create: `tests/fixtures/outline-cycle.pdf`
- Create: `tests/fixtures/outline-depth-257.pdf`
- Create: `tests/test_pdf_outline.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `quantapdf_open`, `quantapdf_close`, `quantapdf_point`, `quantapdf_status`, and existing fixture paths.
- Produces: failing compile references to `quantapdf_outline`, `quantapdf_outline_destination_kind`, `quantapdf_outline_info`, `quantapdf_document_outline`, `quantapdf_outline_count`, `quantapdf_outline_get_info`, `quantapdf_outline_title`, `quantapdf_outline_uri`, and `quantapdf_drop_outline`. No production outline declaration or implementation exists in this task.

- [ ] **Step 1: Generate and check in all four deterministic PDF fixtures**

Run this once from the repository root; commit only the resulting PDFs, not a generator file:

```python
from pathlib import Path

FIXTURES = Path("tests/fixtures")
FIXTURES.mkdir(parents=True, exist_ok=True)


def text_hex(value: str) -> str:
    return "<FEFF" + value.encode("utf-16-be").hex().upper() + ">"


def write_pdf(name: str, objects: list[str]) -> None:
    data = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0] * (len(objects) + 1)

    for number, body in enumerate(objects, 1):
        offsets[number] = len(data)
        data.extend(f"{number} 0 obj\n".encode("ascii"))
        data.extend(body.encode("ascii"))
        data.extend(b"\nendobj\n")

    xref = len(data)
    data.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    data.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        data.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    data.extend(
        (
            f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
            f"startxref\n{xref}\n%%EOF\n"
        ).encode("ascii")
    )

    path = FIXTURES / name
    path.write_bytes(data)
    assert path.read_bytes() == data


write_pdf(
    "outline-tree.pdf",
    [
        "<< /Type /Catalog /Pages 2 0 R /Outlines 6 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
        "<< /Type /Outlines /First 7 0 R /Last 11 0 R >>",
        (
            f"<< /Title {text_hex('Chapter 1 Café')} /Parent 6 0 R "
            "/Next 11 0 R /First 8 0 R /Last 10 0 R /Count 3 "
            "/Dest [3 0 R /XYZ 30 150 1] >>"
        ),
        (
            "<< /Title (Section 1.1) /Parent 7 0 R /Next 9 0 R "
            "/Dest [4 0 R /XYZ 10 180 1] >>"
        ),
        (
            "<< /Title (Website) /Parent 7 0 R /Prev 8 0 R /Next 10 0 R "
            "/A << /S /URI /URI (https://example.com/quantapdf-outline) >> >>"
        ),
        "<< /Parent 7 0 R /Prev 9 0 R >>",
        (
            "<< /Title (Chapter 2) /Parent 6 0 R /Prev 7 0 R "
            "/First 12 0 R /Last 12 0 R /Count -1 "
            "/Dest [5 0 R /XYZ 40 160 1] >>"
        ),
        "<< /Title () /Parent 11 0 R >>",
    ],
)

write_pdf(
    "outline-repairable-bad.pdf",
    [
        "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
        "<< /Type /Outlines /First 5 0 R /Last 6 0 R >>",
        "<< /Title (First) /Parent 4 0 R /Next 6 0 R >>",
        "<< /Title (Second) /Parent 4 0 R >>",
    ],
)

write_pdf(
    "outline-cycle.pdf",
    [
        "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
        "<< /Type /Outlines /First 5 0 R /Last 5 0 R >>",
        "<< /Title (Cycle) /Parent 4 0 R /Next 5 0 R >>",
    ],
)

depth_objects = [
    "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>",
    "<< /Type /Outlines /First 5 0 R /Last 5 0 R >>",
]
for level in range(257):
    number = 5 + level
    parent = 4 if level == 0 else number - 1
    parts = [f"<< /Title (Depth {level + 1}) /Parent {parent} 0 R"]
    if level < 256:
        child = number + 1
        parts.append(f"/First {child} 0 R /Last {child} 0 R /Count 1")
    parts.append(">>")
    depth_objects.append(" ".join(parts))
write_pdf("outline-depth-257.pdf", depth_objects)

assert len(depth_objects) == 261
```

Locked semantic shape of `outline-tree.pdf`:

```text
0 Chapter 1 Café    INTERNAL -> page 0, Fitz (30,50), open
1   Section 1.1     INTERNAL -> page 1, Fitz (10,20)
2   Website         URI      -> https://example.com/quantapdf-outline
3   [title absent]  NONE
4 Chapter 2         INTERNAL -> page 2, Fitz (40,40), closed
5   ""              NONE, present-empty title
```

`outline-repairable-bad.pdf` deliberately omits `/Prev 5 0 R` on its second top-level item. MuPDF's normal PDF outline constructor would repair it; QuantaPDF must return `FORMAT` without invoking that repair path. `outline-cycle.pdf` has `/Next` self-reference. `outline-depth-257.pdf` is structurally valid and must return `UNSUPPORTED`, not `FORMAT`.

- [ ] **Step 2: Add the failing public contract test**

Create `tests/test_pdf_outline.c`:

```c
#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void expect_info(
    const quantapdf_outline *outline,
    size_t index,
    size_t parent_index,
    size_t first_child_index,
    size_t next_sibling_index,
    quantapdf_outline_destination_kind kind,
    int target_page,
    float x,
    float y,
    int is_open)
{
    quantapdf_outline_info info = { 0 };
    info.struct_size = sizeof(info);

    CHECK(quantapdf_outline_get_info(outline, index, &info) == QUANTAPDF_OK);
    CHECK(info.struct_size == sizeof(info));
    CHECK(info.parent_index == parent_index);
    CHECK(info.first_child_index == first_child_index);
    CHECK(info.next_sibling_index == next_sibling_index);
    CHECK(info.destination_kind == kind);
    CHECK(info.target_page == target_page);
    CHECK(close_float(info.target.x, x));
    CHECK(close_float(info.target.y, y));
    CHECK(info.is_open == is_open);
}

static void expect_title(
    const quantapdf_outline *outline,
    size_t index,
    const char *expected)
{
    const char *text = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_title(outline, index, &text, &size) == QUANTAPDF_OK);
    if (expected == NULL) {
        CHECK(text == NULL);
        CHECK(size == 0);
        return;
    }
    CHECK(text != NULL);
    CHECK(size == strlen(expected));
    CHECK(memcmp(text, expected, size) == 0);
    CHECK(text[size] == '\0');
}

static void expect_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char *expected)
{
    const char *uri = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_uri(outline, index, &uri, &size) == QUANTAPDF_OK);
    CHECK(uri != NULL);
    CHECK(size == strlen(expected));
    CHECK(memcmp(uri, expected, size) == 0);
    CHECK(uri[size] == '\0');
}

static void expect_uri_unavailable(
    const quantapdf_outline *outline,
    size_t index)
{
    const char *uri = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_uri(outline, index, &uri, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(uri == NULL);
    CHECK(size == 0);
}

static void test_valid_outline_and_independent_lifetime(void)
{
    static const char unicode_title[] = "Chapter 1 Caf\xC3\xA9";
    static const char uri[] = "https://example.com/quantapdf-outline";
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = NULL;
    size_t count = 0;

    CHECK(quantapdf_open(OUTLINE_TREE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    quantapdf_close(document);

    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 6);

    expect_info(outline, 0, SIZE_MAX, 1, 4,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                0, 30.0f, 50.0f, 1);
    expect_info(outline, 1, 0, SIZE_MAX, 2,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                1, 10.0f, 20.0f, 0);
    expect_info(outline, 2, 0, SIZE_MAX, 3,
                QUANTAPDF_OUTLINE_DESTINATION_URI,
                -1, 0.0f, 0.0f, 0);
    expect_info(outline, 3, 0, SIZE_MAX, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_NONE,
                -1, 0.0f, 0.0f, 0);
    expect_info(outline, 4, SIZE_MAX, 5, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                2, 40.0f, 40.0f, 0);
    expect_info(outline, 5, 4, SIZE_MAX, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_NONE,
                -1, 0.0f, 0.0f, 0);

    expect_title(outline, 0, unicode_title);
    expect_title(outline, 1, "Section 1.1");
    expect_title(outline, 2, "Website");
    expect_title(outline, 3, NULL);
    expect_title(outline, 4, "Chapter 2");
    expect_title(outline, 5, "");

    expect_uri(outline, 2, uri);
    expect_uri_unavailable(outline, 0);
    expect_uri_unavailable(outline, 3);

    quantapdf_drop_outline(outline);
}

static void test_empty_outline(void)
{
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = NULL;
    size_t count = 99;

    CHECK(quantapdf_open(EMPTY_OUTLINE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    quantapdf_close(document);

    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 0);
    quantapdf_drop_outline(outline);
}

static void test_repairable_outline_is_rejected_without_repair(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;

    CHECK(quantapdf_open(OUTLINE_REPAIRABLE_BAD_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);

    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);
    quantapdf_close(document);
}

static void test_cycle_depth_and_pdf_only_boundaries(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;

    CHECK(quantapdf_open(OUTLINE_CYCLE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);
    quantapdf_close(document);

    document = NULL;
    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_open(OUTLINE_DEPTH_257_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(outline == NULL);
    quantapdf_close(document);

    document = NULL;
    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_open(COMPOSITION_NON_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(outline == NULL);
    quantapdf_close(document);
}

static void test_argument_and_reset_contract(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;
    quantapdf_outline_info info = { 0 };
    quantapdf_outline_info small = { 0 };
    const char *text = (const char *)&sentinel;
    size_t size = 99;
    size_t count = 99;

    CHECK(quantapdf_document_outline(NULL, &outline) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(outline == NULL);
    CHECK(quantapdf_document_outline(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(OUTLINE_TREE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);

    CHECK(quantapdf_outline_count(NULL, &count) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(quantapdf_outline_count(outline, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    small.struct_size = offsetof(quantapdf_outline_info, is_open);
    CHECK(quantapdf_outline_get_info(outline, 0, &small) ==
          QUANTAPDF_ERROR_ARGUMENT);

    info.struct_size = sizeof(info);
    info.parent_index = 0;
    info.first_child_index = 0;
    info.next_sibling_index = 0;
    info.destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
    info.target_page = 99;
    info.target.x = 99.0f;
    info.target.y = 99.0f;
    info.is_open = 99;
    CHECK(quantapdf_outline_get_info(outline, 99, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.parent_index == SIZE_MAX);
    CHECK(info.first_child_index == SIZE_MAX);
    CHECK(info.next_sibling_index == SIZE_MAX);
    CHECK(info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_NONE);
    CHECK(info.target_page == -1);
    CHECK(close_float(info.target.x, 0.0f));
    CHECK(close_float(info.target.y, 0.0f));
    CHECK(info.is_open == 0);

    info.parent_index = 0;
    CHECK(quantapdf_outline_get_info(NULL, 0, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.parent_index == SIZE_MAX);

    CHECK(quantapdf_outline_get_info(NULL, 0, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_outline_title(NULL, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_title(outline, 99, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_uri(outline, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_uri(NULL, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    CHECK(quantapdf_outline_uri(outline, 2, NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_close(document);
    quantapdf_drop_outline(outline);
    quantapdf_drop_outline(NULL);
}

int main(void)
{
    test_valid_outline_and_independent_lifetime();
    test_empty_outline();
    test_repairable_outline_is_rejected_without_repair();
    test_cycle_depth_and_pdf_only_boundaries();
    test_argument_and_reset_contract();
    return EXIT_SUCCESS;
}
```

No public outline declaration and no `src/pdf_outline.c` may exist in this RED commit.

- [ ] **Step 3: Register only the new failing target and Windows DLL-copy wiring**

Add after the metadata test in `tests/CMakeLists.txt`:

```cmake
add_executable(quantapdf_test_pdf_outline test_pdf_outline.c)
target_link_libraries(quantapdf_test_pdf_outline PRIVATE QuantaPDF::QuantaPDF)
target_compile_definitions(quantapdf_test_pdf_outline PRIVATE
  OUTLINE_TREE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-tree.pdf"
  OUTLINE_REPAIRABLE_BAD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-repairable-bad.pdf"
  OUTLINE_CYCLE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-cycle.pdf"
  OUTLINE_DEPTH_257_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/outline-depth-257.pdf"
  EMPTY_OUTLINE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt")
add_test(NAME quantapdf.pdf_outline COMMAND quantapdf_test_pdf_outline)
set_tests_properties(quantapdf.pdf_outline PROPERTIES TIMEOUT 30)
```

Add `quantapdf_test_pdf_outline` immediately after `quantapdf_test_pdf_metadata` in the existing Windows shared-library `foreach(test_target IN ITEMS ...)` list.

- [ ] **Step 4: Prove every pre-existing target/test remains green on the RED tree**

Configure Linux/static exactly like CI, then explicitly build every pre-existing target and exclude the new test from CTest:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF

cmake --build build --parallel 2 --target \
  quantapdf_test_status \
  quantapdf_test_document \
  quantapdf_test_render \
  quantapdf_test_text \
  quantapdf_test_structured_text \
  quantapdf_test_text_search \
  quantapdf_test_images \
  quantapdf_test_image_bitmap \
  quantapdf_test_links \
  quantapdf_test_pdf_export \
  quantapdf_test_pdf_range \
  quantapdf_test_pdf_order \
  quantapdf_test_pdf_delete \
  quantapdf_test_pdf_merge \
  quantapdf_test_output_file \
  quantapdf_test_pdf_metadata

ctest --test-dir build --output-on-failure -E '^quantapdf\.pdf_outline$'
```

Required: every existing target builds/links and every existing CTest passes.

- [ ] **Step 5: Prove only the new target is RED for the intended API boundary**

Run:

```bash
cmake --build build --parallel 2 --target quantapdf_test_pdf_outline
```

Accept only compile failures caused by the absent outline type/enum/functions. Fixture parse errors, unrelated production failures, crashes, or timeouts are invalid RED.

- [ ] **Step 6: Commit RED and capture exact SHA**

```bash
git add \
  tests/fixtures/outline-tree.pdf \
  tests/fixtures/outline-repairable-bad.pdf \
  tests/fixtures/outline-cycle.pdf \
  tests/fixtures/outline-depth-257.pdf \
  tests/test_pdf_outline.c \
  tests/CMakeLists.txt

git commit -m "test: define PDF outline snapshot contract"
RED_SHA=$(git rev-parse HEAD)
printf '%s\n' "$RED_SHA"
```

The RED commit contains only those six paths relative to the plan head.

- [ ] **Step 7: Push RED and open one draft PR**

Push `feat/document-outline` and create one draft PR against `master`, referencing #33 and #2. Save the actual numeric PR number returned by GitHub/forge tooling as `PR_NUMBER`; never predict it.

State in the PR body that the exact `RED_SHA` has no public outline declaration and no production outline implementation.

- [ ] **Step 8: Capture exact-head RED CI evidence**

Find the pull-request workflow run whose head SHA equals `RED_SHA`; save its actual run ID as `RED_RUN_ID`. Require Linux to fail only at the absent outline API boundary after the library/existing targets remain buildable. Record the compiler diagnostics in the PR and #33.

---

### Task 2: Minimal read-only PDF outline snapshot GREEN

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_outline.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_outline.c` unchanged from accepted RED

**Interfaces:**
- Consumes: `quantapdf_document`, `quantapdf_status_from_mupdf`, `quantapdf_point`, `pdf_specifics`, `pdf_trailer`, PDF dictionary APIs, `pdf_mark_bits`, `fz_new_outline_iterator`, `fz_outline_iterator_item/next/up/down`, `fz_is_external_link`, `fz_resolve_link_dest`, `fz_page_number_from_location`, and `fz_count_pages`.
- Produces the exact ABI below.

- [ ] **Step 1: Add the public ABI exactly as specified**

In `include/quantapdf/quantapdf.h`, add:

```c
typedef struct quantapdf_outline quantapdf_outline;

typedef enum quantapdf_outline_destination_kind {
    QUANTAPDF_OUTLINE_DESTINATION_NONE = 0,
    QUANTAPDF_OUTLINE_DESTINATION_INTERNAL = 1,
    QUANTAPDF_OUTLINE_DESTINATION_URI = 2
} quantapdf_outline_destination_kind;

typedef struct quantapdf_outline_info {
    size_t struct_size;
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    int is_open;
} quantapdf_outline_info;
```

Add declarations:

```c
QUANTAPDF_API quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline);
QUANTAPDF_API quantapdf_status quantapdf_outline_count(
    const quantapdf_outline *outline,
    size_t *out_count);
QUANTAPDF_API quantapdf_status quantapdf_outline_get_info(
    const quantapdf_outline *outline,
    size_t index,
    quantapdf_outline_info *out_info);
QUANTAPDF_API quantapdf_status quantapdf_outline_title(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);
QUANTAPDF_API quantapdf_status quantapdf_outline_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char **out_utf8,
    size_t *out_size);
QUANTAPDF_API void quantapdf_drop_outline(
    quantapdf_outline *outline);
```

Do not add a public sentinel macro.

- [ ] **Step 2: Create private snapshot storage and string-arena helpers in `src/pdf_outline.c`**

Start the file with:

```c
#include "pdf_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUANTAPDF_OUTLINE_MAX_DEPTH 256u

typedef struct quantapdf_outline_node_internal {
    size_t parent_index;
    size_t first_child_index;
    size_t next_sibling_index;
    quantapdf_outline_destination_kind destination_kind;
    int target_page;
    quantapdf_point target;
    size_t title_offset;
    size_t title_size;
    size_t uri_offset;
    size_t uri_size;
    int has_title;
    int is_open;
} quantapdf_outline_node_internal;

struct quantapdf_outline {
    quantapdf_outline_node_internal *nodes;
    char *strings;
    size_t count;
    size_t string_size;
    size_t string_capacity;
};

typedef struct quantapdf_outline_preflight_frame {
    pdf_obj *parent;
    pdf_obj *node;
    pdf_obj *expected_prev;
    size_t depth;
} quantapdf_outline_preflight_frame;
```

Implement these exact ownership rules:

```c
static void quantapdf_dispose_outline(quantapdf_outline *outline)
{
    if (outline == NULL)
        return;
    free(outline->nodes);
    free(outline->strings);
    free(outline);
}

static void quantapdf_init_outline_node(quantapdf_outline_node_internal *node)
{
    memset(node, 0, sizeof(*node));
    node->parent_index = SIZE_MAX;
    node->first_child_index = SIZE_MAX;
    node->next_sibling_index = SIZE_MAX;
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
    node->target_page = -1;
}
```

Allocate the handle plus exact node array with overflow checks. Grow one UTF-8 string arena with `realloc`, checking `string_size + strlen(text) + 1` and capacity multiplication for `SIZE_MAX` overflow before every growth. Each appended title/URI is copied including its NUL terminator; store offset and byte size excluding NUL. `has_title` is required so absent title remains distinguishable from an appended empty string.

- [ ] **Step 3: Implement iterative read-only structural preflight**

Use a heap-backed stack of `quantapdf_outline_preflight_frame` and `pdf_new_mark_bits()`/`pdf_mark_bits_set()` for temporary visited-state. The stack growth helper must reject multiplication/doubling overflow and return `QUANTAPDF_ERROR_NOMEM` through the caller instead of mutating PDF objects.

For each popped frame, perform exactly this validation order:

```c
if (!pdf_is_dict(ctx, frame.node) || !pdf_is_indirect(ctx, frame.node))
    return FORMAT;
if (pdf_mark_bits_set(ctx, marks, frame.node))
    return FORMAT;
if (pdf_objcmp(ctx,
               pdf_dict_get(ctx, frame.node, PDF_NAME(Parent)),
               frame.parent) != 0)
    return FORMAT;
if (pdf_objcmp(ctx,
               pdf_dict_get(ctx, frame.node, PDF_NAME(Prev)),
               frame.expected_prev) != 0)
    return FORMAT;
```

Then increment node count with overflow protection, record `too_deep = 1` when `frame.depth > 256`, fetch `Next` and `First`, and for a node with no `Next` require:

```c
pdf_objcmp(ctx,
           pdf_dict_get(ctx, frame.parent, PDF_NAME(Last)),
           frame.node) == 0
```

Push the next sibling with `expected_prev = frame.node`, and push the first child with `parent = frame.node`, `expected_prev = NULL`, `depth = frame.depth + 1`. Push sibling first and child second so the LIFO worklist remains preorder-compatible.

Important: do **not** return `UNSUPPORTED` as soon as depth 257 is observed. Continue validating the full reachable tree. After traversal:

```text
any structural failure -> FORMAT
allocation/overflow    -> NOMEM
otherwise too_deep     -> UNSUPPORTED
otherwise              -> OK + exact node_count
```

If `/Root/Outlines` is absent, or exists as a dictionary but has no `/First`, return `OK` with node count 0. If `/Root` or present `/Outlines` is not a dictionary, return `FORMAT`.

Wrap mark-bit lifetime in `fz_try/fz_always/fz_catch` so MuPDF exceptions cannot leak temporary state. Do not call `fz_new_outline_iterator()` inside preflight.

- [ ] **Step 4: Implement bounded iterative projection through MuPDF's outline iterator**

Only call this helper after preflight returned `OK` and exact `node_count > 0`.

Allocate a heap-backed `size_t` parent-context stack with 256 entries. Create `fz_new_outline_iterator(ctx, document)` inside `fz_try`, and always drop it with `fz_drop_outline_iterator()`.

Process each `fz_outline_item` before moving the iterator:

```c
node->parent_index = parent_index;
node->is_open = item->is_open != 0;

if (parent_index != SIZE_MAX &&
    outline->nodes[parent_index].first_child_index == SIZE_MAX)
    outline->nodes[parent_index].first_child_index = current_index;
if (last_sibling_index != SIZE_MAX)
    outline->nodes[last_sibling_index].next_sibling_index = current_index;
```

If `item->title != NULL`, append it to the snapshot arena and set `has_title = 1` even when `strlen(item->title) == 0`.

Destination projection is exact:

```c
if (item->uri == NULL) {
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
} else if (fz_is_external_link(ctx, item->uri)) {
    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
    /* copy item->uri into the snapshot arena */
} else {
    fz_link_dest destination = fz_resolve_link_dest(ctx, document, item->uri);
    int page;

    if (destination.loc.chapter < 0 || destination.loc.page < 0)
        return QUANTAPDF_ERROR_FORMAT;
    page = fz_page_number_from_location(ctx, document, destination.loc);
    if (page < 0 || page >= page_count)
        return QUANTAPDF_ERROR_FORMAT;

    node->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_INTERNAL;
    node->target_page = page;
    node->target.x = destination.x;
    node->target.y = destination.y;
}
```

Traverse without recursion using `fz_outline_iterator_down/next/up` and the heap parent stack:

```text
down == 0 -> push old parent context, descend, reset last_sibling
 down == 1 -> up once to restore current PDF item, then try next
next == 0 -> process sibling at same parent
next == 1 -> climb until a parent has a next sibling or root is exhausted
any negative movement result in a structurally preflighted PDF -> FORMAT
```

When climbing from a child level, set `last_sibling_index` to the parent node just returned to, then restore the parent's parent index from the stack. At completion require the number of decoded iterator items to equal the exact preflight node count; mismatch -> `FORMAT`.

Any standard allocation failure from the snapshot arena -> `NOMEM`. Any MuPDF exception -> existing `quantapdf_status_from_mupdf()` mapping. Do not retain the iterator/document in the snapshot.

- [ ] **Step 5: Implement the extraction entry point with strict output atomicity**

Implement:

```c
quantapdf_status quantapdf_document_outline(
    quantapdf_document *document,
    quantapdf_outline **out_outline)
{
    fz_context *ctx;
    pdf_document *pdf = NULL;
    quantapdf_outline *outline;
    quantapdf_status preflight_status = QUANTAPDF_OK;
    quantapdf_status flatten_status;
    size_t count = 0;
    int caught_code = FZ_ERROR_NONE;

    if (out_outline == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_outline = NULL;

    if (document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    ctx = document->ctx;
    fz_var(pdf);
    fz_var(preflight_status);
    fz_var(count);
    fz_var(caught_code);

    fz_try(ctx)
    {
        pdf = pdf_specifics(ctx, document->doc);
        if (pdf != NULL)
            preflight_status = quantapdf_preflight_pdf_outline(
                ctx, pdf, &count);
    }
    fz_catch(ctx)
    {
        caught_code = fz_caught(ctx);
        fz_report_error(ctx);
    }

    if (caught_code != FZ_ERROR_NONE)
        return quantapdf_status_from_mupdf(caught_code);
    if (pdf == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (preflight_status != QUANTAPDF_OK)
        return preflight_status;

    outline = quantapdf_allocate_outline(count);
    if (outline == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    if (count != 0) {
        flatten_status = quantapdf_flatten_pdf_outline(
            ctx, document->doc, outline);
        if (flatten_status != QUANTAPDF_OK) {
            quantapdf_dispose_outline(outline);
            return flatten_status;
        }
    }

    *out_outline = outline;
    return QUANTAPDF_OK;
}
```

This ordering is mandatory: non-PDF gate -> complete preflight/depth decision -> allocation -> iterator projection -> publish.

- [ ] **Step 6: Implement accessors and cleanup with deterministic resets**

`quantapdf_outline_count()` resets `*out_count = 0` before validating the handle.

`quantapdf_outline_get_info()` computes:

```c
minimum_size = offsetof(quantapdf_outline_info, is_open) +
    sizeof(out_info->is_open);
```

Reject smaller structs before writing any other field. For an accepted struct size, reset all known fields before handle/index validation:

```c
out_info->parent_index = SIZE_MAX;
out_info->first_child_index = SIZE_MAX;
out_info->next_sibling_index = SIZE_MAX;
out_info->destination_kind = QUANTAPDF_OUTLINE_DESTINATION_NONE;
out_info->target_page = -1;
out_info->target.x = 0.0f;
out_info->target.y = 0.0f;
out_info->is_open = 0;
```

On success copy the corresponding internal record; never modify `out_info->struct_size`.

`quantapdf_outline_title()` and `quantapdf_outline_uri()` reset supplied outputs to `NULL/0` before validation. Title returns `OK + NULL/0` for `has_title == 0`; otherwise return `outline->strings + title_offset` and byte size. URI succeeds only for `DESTINATION_URI`; NONE/INTERNAL -> `ARGUMENT + NULL/0`. `quantapdf_drop_outline(NULL)` is a no-op.

- [ ] **Step 7: Register the new production file only in root CMake**

Change:

```cmake
  src/output_file.c
  src/pdf_metadata.c)
```

to:

```cmake
  src/output_file.c
  src/pdf_metadata.c
  src/pdf_outline.c)
```

Do not modify `src/pdf_internal.h`, `src/internal.h`, or `src/document.c`.

- [ ] **Step 8: Prove the RED contract turns GREEN without weakening tests**

Run:

```bash
cmake --build build --parallel 2 --target quantapdf_test_pdf_outline
ctest --test-dir build --output-on-failure -R '^quantapdf\.pdf_outline$'
```

Required: target builds/links and `quantapdf.pdf_outline` passes with the RED fixtures/assertions unchanged.

- [ ] **Step 9: Prove Linux strict static + all tests and sanitizer + all tests**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Required: all normal and sanitizer CTests pass.

- [ ] **Step 10: Commit minimal GREEN and capture exact SHA**

Verify the production delta from RED is exactly:

```text
include/quantapdf/quantapdf.h
src/pdf_outline.c
CMakeLists.txt
```

Then:

```bash
git add include/quantapdf/quantapdf.h src/pdf_outline.c CMakeLists.txt
git commit -m "feat: expose immutable PDF outline snapshot"
GREEN_SHA=$(git rev-parse HEAD)
printf '%s\n' "$GREEN_SHA"
```

Do not edit RED tests/fixtures between accepted RED and GREEN.

- [ ] **Step 11: Push GREEN and capture normal exact-head Linux CI**

Push `GREEN_SHA` to the existing draft PR. Find the PR workflow whose head SHA equals `GREEN_SHA`; save its actual run ID as `GREEN_RUN_ID`.

Require:

```text
Linux strict static build + all CTests ✅
Linux ASan/UBSan build + all CTests   ✅
```

Do not change code after this proof before `full-ci`.

---

### Task 3: Same-head platform proof, review gate, and bookkeeping

**Files:**
- No code changes.
- Update only GitHub PR/issue/roadmap metadata after CI evidence is known.

**Interfaces:**
- Consumes: `RED_SHA`, `RED_RUN_ID`, `GREEN_SHA`, `GREEN_RUN_ID`, `PR_NUMBER`.
- Produces: same-head Linux/macOS/Windows evidence and a draft/open/unmerged PR ready for explicit integration.

- [ ] **Step 1: Verify exact final scope before platform CI**

Compare base `cb0f50b0734bcae00fedd529e4f13701c0852866` to `GREEN_SHA`.

The complete feature diff must contain exactly **11 changed paths**:

```text
docs/superpowers/specs/2026-08-28-quantapdf-document-outline-design.md
docs/superpowers/plans/2026-08-28-quantapdf-document-outline.md
include/quantapdf/quantapdf.h
src/pdf_outline.c
CMakeLists.txt
tests/CMakeLists.txt
tests/test_pdf_outline.c
tests/fixtures/outline-tree.pdf
tests/fixtures/outline-repairable-bad.pdf
tests/fixtures/outline-cycle.pdf
tests/fixtures/outline-depth-257.pdf
```

Any other changed path is a scope blocker. Explicitly reject changes to `src/document.c`, `src/internal.h`, `src/links.c`, `src/pdf_metadata.c`, `src/pdf_export.c`, `src/pdf_merge.c`, `src/pdf_output.c`, or `src/output_file.c`.

- [ ] **Step 2: Trigger same-head `full-ci` without a code change**

Add the existing `full-ci` label to the draft PR. Re-read the PR head and require it still equals `GREEN_SHA`. Save the label-triggered workflow run ID as `FULL_RUN_ID`, and require that run's head SHA also equals `GREEN_SHA`.

- [ ] **Step 3: Require all platform jobs on that exact head**

Require `FULL_RUN_ID` to complete successfully with:

```text
Linux static configure/build/all CTests       ✅
Linux ASan/UBSan configure/build/all CTests   ✅
macOS configure/build/test                    ✅
Windows DLL configure/build/test              ✅
```

On Windows, verify `quantapdf_test_pdf_outline` is in the DLL-copy list and `quantapdf.pdf_outline` passes through the shared-library build.

- [ ] **Step 4: Perform a fresh exact-diff review against the spec**

Review `cb0f50b...GREEN_SHA` and explicitly verify:

```text
public ABI exactly matches the spec
PDF-only gate precedes outline extraction
preflight is read-only, iterative, and cycle/repeated-node safe
preflight validates the full tree before choosing depth UNSUPPORTED
bad Parent/Prev/Last -> FORMAT without silent repair
valid 257-level tree -> UNSUPPORTED before iterator creation
empty outline -> non-NULL count-0 snapshot
preorder parent/child/sibling indices are exact
internal destination -> flat page + Fitz x/y
URI/title strings are snapshot-owned only
absent title != present-empty title
snapshot survives document close
no MuPDF/document pointer survives publication
all failures publish NULL / reset accessor outputs
indices are snapshot-local only, never mutation identities
no unrelated Phase 3/4 implementation changed
```

Any Critical/Important finding requires a new GREEN SHA and fresh Linux + same-head full-ci evidence.

- [ ] **Step 5: Update the draft PR with actual evidence**

Use the captured values `RED_SHA`, `RED_RUN_ID`, `GREEN_SHA`, `GREEN_RUN_ID`, and `FULL_RUN_ID`. Do not invent run or PR numbers. Record:

```text
RED: existing targets/tests green; only outline API target absent
GREEN: Linux static + all CTests ✅; ASan/UBSan + all CTests ✅
full-ci same head: Linux ✅; macOS ✅; Windows DLL ✅
```

Keep the PR draft/open/unmerged.

- [ ] **Step 6: Update #33 but keep it open**

Write the actual captured PR number, RED/GREEN/full-ci evidence, final scope, and locked safety semantics into #33. State that implementation/evidence closure is complete but the issue remains open for integration bookkeeping.

- [ ] **Step 7: Update umbrella #2 conservatively**

Mark only the Phase 5 Outline/bookmarks implementation line complete, link #33 and the captured PR number, and record `GREEN_SHA` + `FULL_RUN_ID` while stating the PR is still draft/open/unmerged. Do not mark annotation enumeration, annotation mutation, forms/widgets, or the interactive-mutation architecture decision complete.

- [ ] **Step 8: Stop at the integration decision gate**

Report the draft/open/unmerged PR state, mergeability, `GREEN_SHA`, RED run, Linux GREEN run, same-head full-ci run, and remaining Phase 5 items. Do not mark ready and do not merge without explicit later authorization.
