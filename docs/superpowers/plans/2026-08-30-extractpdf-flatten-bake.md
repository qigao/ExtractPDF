# ExtractPDF Flatten / Bake V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an immutable deterministic whole-document Flatten / Bake V1 transform that bakes already-materialized annotation and Widget normal appearances into ordinary page content, removes only the selected interactive provenance, and preserves unrelated PDF graph state under the approved fail-closed policy.

**Architecture:** Reuse the existing immutable transform substrate: strict source preflight, canonical serialization, fresh private MuPDF context with JavaScript disabled, independent private-plan rebuild, pointer-free semantic-plan equivalence, private runtime reference resolution, first write, then deterministic serialization. Split responsibilities into one read-only appearance helper, one semantic preflight/plan module, one page bake writer, one AcroForm pruning adapter, and one public orchestrator; do not create a new repository-wide kernel hierarchy.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, existing reproducible PDF serializer, Linux static + ASan/UBSan, macOS, Windows MSVC DLL CI.

**Spec:** `docs/superpowers/specs/2026-08-30-extractpdf-flatten-bake-design.md`

**Normative correction:** `docs/superpowers/specs/2026-08-30-extractpdf-flatten-bake-preflight-correction.md`

## Global Constraints

- Baseline is integrated `master@be28add194e98ccfa7b1ab613a8b284782011cf1`, tree `3ca169114f44c5d7c7569e58c573fd621993fd01`, with exactly 24 CTests.
- The feature is tracked by #55, under #48 / #2.
- Public ABI is exactly `extractpdf_flatten_interactive(extractpdf_document *, uint32_t, extractpdf_output **)` plus the two approved flag bits; no MuPDF type enters the public ABI.
- `document == NULL`, `out_output == NULL`, `flags == 0`, or unknown flag bits are `EXTRACTPDF_ERROR_ARGUMENT`; every failure leaves `*out_output == NULL` when an output pointer is provided.
- V1 is whole-document. There are no public page selectors or object identities.
- The source document is immutable. A successful output owns its bytes and remains usable after the source is closed.
- A semantic no-op still performs argument validation, PDF/security validation, and strict discovery of the requested classes, then canonical-serializes the source once without creating a private mutation graph.
- Bake-only restrictions are not applied to unrelated document state when strict requested-class discovery finds zero selected objects.
- Real bake uses canonical source serialization -> fresh private context/document -> `pdf_disable_js()` -> security recheck -> independent complete plan rebuild -> semantic-plan equivalence -> private runtime resolution -> first write -> deterministic serialization.
- No source `pdf_obj *`, `pdf_page *`, `pdf_annot *`, context allocation, or source journal state crosses into the private context.
- Encrypted and already-signed inputs are `EXTRACTPDF_ERROR_UNSUPPORTED`; security is checked before publication even for semantic no-op.
- A real bake with Catalog `/StructTreeRoot` is `EXTRACTPDF_ERROR_UNSUPPORTED`.
- No JavaScript, action, activation, form event, validation, formatting, calculation, appearance generation/regeneration, redaction application, rasterization, GC, object deduplication, recompression, incremental save, decryption, or signature-preserving rewrite is allowed.
- `EXTRACTPDF_FLATTEN_ANNOTATIONS` supports only Text, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight, Underline, Squiggly, StrikeOut, Stamp, Caret, and Ink.
- Redact, FileAttachment, Sound, Movie, RichMedia, Screen, PrinterMark, TrapNet, Watermark, 3D, Projection, and unknown ordinary subtypes are `EXTRACTPDF_ERROR_UNSUPPORTED` when annotation flattening is requested.
- Links are never flattened. A Link remaining on a changed page is acceptable only when `/AP /N` is absent and the normative border rule proves exact effective width `0`.
- Link `/BS`, when present, is authoritative: `/BS` must be a dictionary; absent `/W` means default width `1`; present `/W` must be one finite non-negative number; only exact `0` is neutral; malformed consumed data is `FORMAT`.
- Link `/Border` is inspected only when `/BS` is absent: absent means default width `1`; present must provide at least the first three finite numeric entries; the third is width; only exact `0` is neutral; malformed consumed data is `FORMAT`.
- Any Link `/AP /N`, non-zero/default/implicit Link border width, or otherwise visually non-neutral Link on a changed page is `EXTRACTPDF_ERROR_UNSUPPORTED`.
- Popup `/Parent`, annotation `/Popup`, and `/IRT` relationships are checked in both directions. A selected object participating in a relationship that would leave broken interactive provenance is `EXTRACTPDF_ERROR_UNSUPPORTED`.
- Widget selection uses `extractpdf_pdf_form_build(..., want_provenance=1)` plus `extractpdf_pdf_form_capture_provenance_widgets(...)`; orphan, duplicate, ambiguous, malformed, or unreconciled Widgets are `FORMAT`.
- Real Widget baking rejects `/AcroForm /XFA` and `/NeedAppearances true`; malformed `/NeedAppearances` is `FORMAT`.
- Flatten uses only the already-present current normal appearance `/AP /N`; no appearance synthesis may turn an absent appearance into success.
- Stateful `/AP /N` requires `/AS` as a name selecting an existing indirect Form stream. Missing `/AS` is `UNSUPPORTED`; malformed `/AS` or broken selected state is `FORMAT`; field `/V` never selects the visual state.
- Selected appearance must resolve to an indirect stream with `/Subtype /Form`, an exact four-finite-number positive non-zero `/BBox`, optional exact six-finite-number `/Matrix`, and optional dictionary `/Resources`; selected annotation/appearance `/OC` is `UNSUPPORTED`.
- Selected annotation `/F` is parsed through the existing strict raw `uint32_t` path. Invisible, Hidden, NoZoom, NoRotate, NoView, and ToggleNoView are `UNSUPPORTED`.
- Placement is in raw PDF page user space. The Form's own `/Matrix` stays on the Form; the bake stream adds only the approved placement matrix derived from transformed `/BBox` and normalized annotation `/Rect`.
- Every changed page satisfies visual closure: annotation-only cannot leave a Widget on that page; Widget-only cannot leave an ordinary annotation on that page; both-flags mode moves the selected ordinary annotations and Widgets together in original `/Annots` order; any remaining Link is neutral.
- Each changed page receives at most one appended deterministic bake content stream. Each selected target contributes exactly `q`, placement `cm`, deterministic XObject `Do`, `Q` in original `/Annots` order.
- Distinct appearance identities receive `/EPB0`, `/EPB1`, ... in first-use order; repeated use of one appearance identity reuses one alias; collisions in effective `/Resources /XObject` are skipped deterministically.
- Effective inherited `/Resources` and `/XObject` are never mutated in place. A changed page gets page-local copy-on-write dictionaries preserving existing references.
- Changed-page `/Contents` accepts only absent, one indirect stream, or an array whose every entry resolves to an indirect stream. Anything else is `FORMAT`.
- Existing `/Contents` arrays are never mutated in place. A new page-local array preserves all prior stream references/order and appends exactly one bake stream.
- Bake numeric operands use one private locale-independent canonical finite-number formatter with cross-platform byte-stability tests.
- Page `/Annots` is replaced copy-on-write from the original identity/order, omitting selected targets. If no annotation survives, remove the page-local `/Annots` key. Do not explicitly delete now-unreachable annotation objects from xref.
- Widget field pruning uses the normative affected-node closure: selected Widget owning field/merged node plus every ancestor on its strict locator path to root `/Fields`.
- Nodes outside the affected field-node closure are preserved even if they were already widgetless or empty-looking.
- Affected fields are pruned bottom-up only when a selected merged field+Widget is removed or no `/Kids` survive after selected child/Widget removal.
- Changed `/Kids`, root `/Fields`, and `/CO` arrays are replacement arrays preserving surviving identities/order; indirect/shared source arrays are never edited in place.
- If root `/Fields` has survivors, preserve `/AcroForm` and unrelated keys. If no root field survives, remove Catalog `/AcroForm` entirely; do not leave `/Fields []` solely to retain former form-only keys.
- The semantic plan is pointer-free. Source/private equivalence compares page/ordinal/class, raw Rect/flags, appearance selection/state, BBox/Matrix/placement, first-use alias pattern, Widget locator/provenance, and relationship/closure facts—not pointer values.
- Semantic writes are limited to changed page `/Resources`, `/Contents`, `/Annots`, affected form `/Kids`/`/Fields`/`/CO`, and optional Catalog `/AcroForm` removal. Existing content bytes, appearance streams, page boxes, Rotate, UserUnit, Links, metadata, Outline, Names/Dests, destinations, field values/options, encryption/signature state are not rewritten.
- Same source bytes + same flags must produce byte-identical outputs across repeated calls.
- Add exactly one CTest named `extractpdf.pdf_flatten`; suite target is 24 -> 25 CTests.
- No workflow YAML change is authorized to obtain proof. Use the existing `full-ci` label mechanism for same-SHA macOS and Windows jobs; Linux static + ASan/UBSan already run on PR heads.
- Implementation stops after exact-head multi-platform proof and semantic-write audit. Integration requires a separate explicit authorization such as `go integrate`.

---

## File Structure Locked by This Plan

### Production

- `include/extractpdf/extractpdf.h` — approved public flatten flag enum and immutable transform function only.
- `src/pdf_appearance_common.h` / `src/pdf_appearance_common.c` — strict raw `/AP /N` resolution, Form validation, raw `/Rect` parsing, placement matrix math; read-only and reusable by future page-content transforms.
- `src/pdf_flatten_internal.h` — pointer-free Flatten semantic plan types plus private module interfaces.
- `src/pdf_flatten_preflight.c` — security, whole-document annotation selection, page inventory, subtype/flags/relationship/Link/visual-closure policy, deterministic target/alias plan, plan equality.
- `src/pdf_flatten_form.c` — strict Widget provenance adapter, affected field-node closure, `/Kids`/`/Fields`/`/CO` copy-on-write prune plan/equality/writer.
- `src/pdf_flatten_bake.c` — private runtime target resolution, page-local Resources/XObject isolation, canonical bake stream, strict Contents replacement, Annots replacement.
- `src/pdf_flatten.c` — public ABI validation, semantic no-op, canonical private-copy orchestration, source/private plan-equivalence gate, writer ordering, output publication.
- `CMakeLists.txt` — compile the new private Flatten modules; no unrelated source-list reshuffle.

Do not change `src/pdf_form_common.[ch]`, `src/pdf_form_provenance_widgets.c`, `src/pdf_annotation_common.[ch]`, or `src/pdf_output.c` unless execution discovers a demonstrable reusable bug in those existing components. #55 should consume their current contracts rather than broaden them pre-emptively.

### Tests

- `tests/test_pdf_flatten.c` — public ABI, argument contract, first strict RED, base no-op/happy-path entry.
- `tests/test_pdf_flatten_internal.h` — test-module entry points only.
- `tests/test_pdf_flatten_appearance.c` — direct/state appearance selection, BBox/Matrix/Rect placement, flags, no-synthesis cases.
- `tests/test_pdf_flatten_raw.c` — raw PDF graph assertions for `/Resources`, `/XObject`, `/Contents`, `/Annots`, alias reuse/order, source-object immutability.
- `tests/test_pdf_flatten_form.c` — Widget appearance behavior, strict provenance, merged/separate layouts, affected-node closure, `/Fields`/`Kids`/`CO` replacement identity.
- `tests/test_pdf_flatten_policy.c` — subtype rejection, Link neutrality, Popup/IRT reverse closure, visual closure, tagged/XFA/NeedAppearances policy.
- `tests/test_pdf_flatten_determinism.c` — semantic no-op, repeated bytes, request-flag set semantics, source success/failure immutability, output lifetime, security fail-closed.
- `tests/test_pdf_flatten_main.c` — one process/CTest entry that invokes all Flatten test modules.
- `tests/CMakeLists.txt` — exactly one target `extractpdf_test_pdf_flatten`, fixture defines, MuPDF-private test linkage, Windows DLL copy.

### Fixtures

Commit deterministic static PDF fixtures. Fixture authoring may use one-off local tooling, but CTest must never require Python or generate its semantic fixtures at runtime.

- `tests/fixtures/flatten-basic.pdf` — one changed Square page with an existing indirect Form `/AP /N`, plus one untouched control page; no Link, Widget, Popup, tagged state, actions, XFA, or NeedAppearances.
- `tests/fixtures/flatten-appearance.pdf` — independent pages for FreeText, eligible Text, non-zero BBox origin + non-identity Form Matrix, state dictionary + `/AS`, two annotations sharing one Form identity, and selected appearances with their own Resources.
- `tests/fixtures/flatten-appearance-malformed.pdf` — isolated pages for missing `/AP /N`, malformed `/AP`, malformed/degenerate `/BBox`, malformed `/Matrix`, non-name/broken/missing `/AS`, malformed `/Rect`, `/OC`, and rejected selected `/F` bits.
- `tests/fixtures/flatten-policy.pdf` — isolated pages for each rejected ordinary subtype family plus valid/invalid Link border combinations and mixed ordinary/Widget visual-closure cases.
- `tests/fixtures/flatten-relationships.pdf` — selected annotations exercising `/Popup`, Popup `/Parent`, forward `/IRT`, reverse `/IRT`, and an unrelated Popup control.
- `tests/fixtures/flatten-widgets.pdf` — Text Widget, stale field `/V` vs Widget `/AS`, checkbox, radio, multi-Widget field, merged field+Widget root, separate Widget children, unaffected widgetless subtree, grouping ancestors, and `/CO` with removed + surviving fields.
- `tests/fixtures/flatten-form-cow.pdf` — shared/indirect `/Fields`, `/Kids`, and `/CO` arrays arranged so changed-array object identity can be proved different while surviving child identity/order stays equal.
- `tests/fixtures/flatten-contents.pdf` — pages with absent Contents, one indirect stream, valid indirect-stream array, malformed direct/non-stream array entry, inherited/shared Resources, and pre-existing `/EPB0`/`/EPB1` collisions.
- `tests/fixtures/flatten-tagged.pdf` — Catalog `/StructTreeRoot` plus a real selected annotation.

Reuse `tests/fixtures/annotation-mutation-signed.pdf`, `tests/fixtures/encrypted-one-page.pdf`, `tests/fixtures/composition-non-pdf.txt`, and existing valid form fixtures only where their exact raw shape already proves the approved Flatten condition. Do not weaken a Flatten assertion merely to reuse a near-match fixture.

---

### Task 1: Strict Flatten Compile RED and Draft PR

**Files:**
- Create: `tests/test_pdf_flatten.c`
- Create: `tests/fixtures/flatten-basic.pdf`
- Modify: `tests/CMakeLists.txt`
- Do not modify: `include/extractpdf/extractpdf.h`, `src/`, root `CMakeLists.txt`, workflows.

**Interfaces:**
- Consumes: current integrated public API at `master@be28add194e98ccfa7b1ab613a8b284782011cf1`.
- Produces: exactly one new CTest target whose first build failure is attributable only to the absent approved Flatten ABI.

- [ ] **Step 1: Commit the minimal static happy-path fixture**

`flatten-basic.pdf` must have this raw semantic shape:

```text
page 0
  /MediaBox [0 0 300 300]
  existing content draws a fixed background marker
  /Annots [square-ref]

square-ref
  /Type /Annot
  /Subtype /Square
  /Rect [60 70 180 190]
  /F 0
  /AP << /N appearance-ref >>

appearance-ref
  indirect stream
  /Type /XObject
  /Subtype /Form
  /BBox [0 0 120 120]
  stream draws one deterministic filled/stroked shape

page 1
  untouched control page with a fixed text/vector marker
```

The fixture contains no Link, Popup, Widget, AcroForm, action, optional content, tagged structure, signature, encryption, or inherited resource ambiguity.

- [ ] **Step 2: Add the intentionally uncompilable ABI test**

Create `tests/test_pdf_flatten.c` with the repository's existing `CHECK` style and this call as the first contract:

```c
#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;

    CHECK(extractpdf_open(FLATTEN_BASIC_PDF, NULL, &document) == EXTRACTPDF_OK);
    CHECK(document != NULL);
    CHECK(extractpdf_flatten_interactive(
        document,
        EXTRACTPDF_FLATTEN_ANNOTATIONS,
        &output) == EXTRACTPDF_OK);
    CHECK(output != NULL);

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
```

- [ ] **Step 3: Register only the one new CTest**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_pdf_flatten test_pdf_flatten.c)
target_link_libraries(extractpdf_test_pdf_flatten PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_flatten PRIVATE
  FLATTEN_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/flatten-basic.pdf")
add_test(NAME extractpdf.pdf_flatten COMMAND extractpdf_test_pdf_flatten)
set_tests_properties(extractpdf.pdf_flatten PROPERTIES TIMEOUT 60)

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(TARGET extractpdf_test_pdf_flatten POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:extractpdf>
      $<TARGET_FILE_DIR:extractpdf_test_pdf_flatten>
    VERBATIM)
endif()
```

- [ ] **Step 4: Prove the strict RED is attributable**

Run the same Linux configure/build path as CI:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
```

Expected new-target diagnostics are only the absent approved names:

```text
EXTRACTPDF_FLATTEN_ANNOTATIONS undeclared
extractpdf_flatten_interactive undeclared
```

Existing 24 test executables must still compile. Any unrelated failure is a regression and blocks Task 2.

- [ ] **Step 5: Commit strict RED**

```bash
git add tests/test_pdf_flatten.c tests/fixtures/flatten-basic.pdf tests/CMakeLists.txt
git commit -m "test: add Flatten Bake compile contract"
```

- [ ] **Step 6: Open a draft PR only after the RED commit exists**

Draft PR contract:

```text
base: master
head: feat/flatten-bake
title: feat: add immutable interactive Flatten / Bake transform
tracks: #55, #48, #2
specs: both committed Flatten/Bake spec documents
state: expected strict compile RED; no claim of working production behavior
```

Do not apply `full-ci` yet. Linux RED is sufficient at this gate.

---

### Task 2: Public ABI Shell -> Runtime RED

**Files:**
- Modify: `include/extractpdf/extractpdf.h` near the existing immutable crop/trim/poster transform declarations.
- Create: `src/pdf_flatten.c`
- Modify: root `CMakeLists.txt` source list.
- Modify: `tests/test_pdf_flatten.c`

**Interfaces:**
- Produces exactly:

```c
typedef enum extractpdf_flatten_flag {
    EXTRACTPDF_FLATTEN_ANNOTATIONS = 1u << 0,
    EXTRACTPDF_FLATTEN_WIDGETS = 1u << 1
} extractpdf_flatten_flag;

EXTRACTPDF_API extractpdf_status extractpdf_flatten_interactive(
    extractpdf_document *document,
    uint32_t flags,
    extractpdf_output **out_output);
```

- No private module interface is introduced in this task.

- [ ] **Step 1: Add the approved public declarations verbatim**

Place the enum with other public request enums and the function beside `extractpdf_crop_pages`, `extractpdf_trim_pages`, and `extractpdf_poster_split_pages`. Do not expose selectors, MuPDF objects, options structs, or convenience wrappers.

- [ ] **Step 2: Add only argument validation plus an explicit runtime boundary**

Create `src/pdf_flatten.c`:

```c
#include "pdf_internal.h"

#include <stdint.h>

extractpdf_status extractpdf_flatten_interactive(
    extractpdf_document *document,
    uint32_t flags,
    extractpdf_output **out_output)
{
    const uint32_t known =
        EXTRACTPDF_FLATTEN_ANNOTATIONS | EXTRACTPDF_FLATTEN_WIDGETS;

    if (out_output == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->ctx == NULL || document->doc == NULL ||
        flags == 0 || (flags & ~known) != 0)
        return EXTRACTPDF_ERROR_ARGUMENT;

    return EXTRACTPDF_ERROR_STATE;
}
```

`EXTRACTPDF_ERROR_STATE` is a temporary internal TDD boundary, not a final V1 classification for valid input.

- [ ] **Step 3: Compile the ABI into the library**

Add only `src/pdf_flatten.c` to root `CMakeLists.txt`.

- [ ] **Step 4: Add argument assertions without weakening the valid-call expectation**

Extend `tests/test_pdf_flatten.c` before the valid call:

```c
extractpdf_output *sentinel = (extractpdf_output *)(uintptr_t)1;

CHECK(extractpdf_flatten_interactive(NULL,
    EXTRACTPDF_FLATTEN_ANNOTATIONS, &sentinel) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(sentinel == NULL);

sentinel = (extractpdf_output *)(uintptr_t)1;
CHECK(extractpdf_flatten_interactive(document, 0, &sentinel) ==
    EXTRACTPDF_ERROR_ARGUMENT);
CHECK(sentinel == NULL);

sentinel = (extractpdf_output *)(uintptr_t)1;
CHECK(extractpdf_flatten_interactive(document, 1u << 31, &sentinel) ==
    EXTRACTPDF_ERROR_ARGUMENT);
CHECK(sentinel == NULL);

CHECK(extractpdf_flatten_interactive(document,
    EXTRACTPDF_FLATTEN_ANNOTATIONS, NULL) == EXTRACTPDF_ERROR_ARGUMENT);
```

- [ ] **Step 5: Run the focused target and prove runtime RED**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

Expected: compilation succeeds; the valid Square call fails because the shell returns `EXTRACTPDF_ERROR_STATE`. Argument assertions before it pass.

- [ ] **Step 6: Commit ABI shell**

```bash
git add include/extractpdf/extractpdf.h src/pdf_flatten.c CMakeLists.txt tests/test_pdf_flatten.c
git commit -m "feat: add Flatten Bake ABI shell"
```

---

### Task 3: Strict Appearance Resolver, Pointer-Free Plan, and Private Revalidation Gate

**Files:**
- Create: `src/pdf_appearance_common.h`
- Create: `src/pdf_appearance_common.c`
- Create: `src/pdf_flatten_internal.h`
- Create: `src/pdf_flatten_preflight.c`
- Modify: `src/pdf_flatten.c`
- Modify: root `CMakeLists.txt`
- Create: `tests/fixtures/flatten-appearance.pdf`
- Create: `tests/fixtures/flatten-appearance-malformed.pdf`
- Create: `tests/test_pdf_flatten_internal.h`
- Create: `tests/test_pdf_flatten_appearance.c`
- Create: `tests/test_pdf_flatten_main.c`
- Modify: `tests/test_pdf_flatten.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

`src/pdf_appearance_common.h`:

```c
typedef struct extractpdf_pdf_appearance_view {
    int stateful;
    char *state_name;
    size_t state_name_size;
    fz_rect rect;
    fz_rect bbox;
    fz_matrix matrix;
    fz_matrix placement;
} extractpdf_pdf_appearance_view;

extractpdf_status extractpdf_pdf_appearance_resolve(
    fz_context *ctx,
    pdf_document *document,
    pdf_obj *annotation,
    extractpdf_pdf_appearance_view *out_view,
    pdf_obj **out_form);

void extractpdf_pdf_appearance_drop_view(
    extractpdf_pdf_appearance_view *view);
```

`out_form` is a context-local borrowed resolved Form used only while building/resolving one context's plan; it is never stored in the pointer-free semantic plan.

`src/pdf_flatten_internal.h` locks these semantic types:

```c
typedef enum extractpdf_pdf_flatten_target_kind {
    EXTRACTPDF_PDF_FLATTEN_TARGET_ANNOTATION = 1,
    EXTRACTPDF_PDF_FLATTEN_TARGET_WIDGET = 2
} extractpdf_pdf_flatten_target_kind;

typedef struct extractpdf_pdf_flatten_target_plan {
    int page_index;
    size_t annot_ordinal;
    extractpdf_pdf_flatten_target_kind kind;
    extractpdf_annotation_type annotation_type;
    uint32_t flags;
    fz_rect rect;
    int appearance_stateful;
    char *appearance_state;
    size_t appearance_state_size;
    fz_rect bbox;
    fz_matrix appearance_matrix;
    fz_matrix placement;
    size_t appearance_slot;
} extractpdf_pdf_flatten_target_plan;

typedef struct extractpdf_pdf_flatten_page_plan {
    int page_index;
    size_t first_target;
    size_t target_count;
    size_t appearance_slot_count;
} extractpdf_pdf_flatten_page_plan;

typedef struct extractpdf_pdf_flatten_form_plan
    extractpdf_pdf_flatten_form_plan;

typedef struct extractpdf_pdf_flatten_plan {
    uint32_t flags;
    int source_page_count;
    extractpdf_pdf_flatten_target_plan *targets;
    size_t target_count;
    extractpdf_pdf_flatten_page_plan *pages;
    size_t page_count;
    int any_changed;
    int policy_complete;
    extractpdf_pdf_flatten_form_plan *form;
} extractpdf_pdf_flatten_plan;
```

Private plan functions:

```c
extractpdf_status extractpdf_pdf_flatten_check_security(
    fz_context *ctx,
    pdf_document *document);

extractpdf_status extractpdf_pdf_flatten_build_plan(
    fz_context *ctx,
    pdf_document *document,
    uint32_t flags,
    extractpdf_pdf_flatten_plan **out_plan);

int extractpdf_pdf_flatten_plan_equivalent(
    const extractpdf_pdf_flatten_plan *left,
    const extractpdf_pdf_flatten_plan *right);

void extractpdf_pdf_flatten_drop_plan(
    extractpdf_pdf_flatten_plan *plan);
```

- [ ] **Step 1: Write strict raw appearance REDs before implementing the helper**

`tests/test_pdf_flatten_appearance.c` must assert at least:

```text
Square direct indirect /AP /N Form -> accepted
FreeText direct appearance -> accepted with no regeneration
Text with flags 0 -> accepted
Text with NoZoom or NoRotate -> UNSUPPORTED
non-zero /BBox origin + non-identity /Matrix -> placement matches approved formula
state dictionary + valid /AS -> exact selected state
stale field /V does not influence selected /AS (fixture is exercised fully in Task 5)
missing /AP /N -> UNSUPPORTED
/AP wrong type -> FORMAT
state /AS missing -> UNSUPPORTED
state /AS non-name or missing selected entry -> FORMAT
BBox wrong count/non-number/non-finite -> FORMAT
BBox zero/degenerate geometry -> UNSUPPORTED
Matrix wrong count/non-number/non-finite -> FORMAT
Rect malformed/non-finite -> FORMAT
annotation or selected Form /OC -> UNSUPPORTED
```

Use raw expected matrices rather than render-only assertions. For the matrix fixture, assert the exact approved calculation:

```c
CHECK(fabsf(view.placement.a - expected_sx) < 1e-6f);
CHECK(fabsf(view.placement.d - expected_sy) < 1e-6f);
CHECK(fabsf(view.placement.e - expected_tx) < 1e-6f);
CHECK(fabsf(view.placement.f - expected_ty) < 1e-6f);
CHECK(view.placement.b == 0.0f);
CHECK(view.placement.c == 0.0f);
```

- [ ] **Step 2: Implement the read-only resolver without MuPDF annotation appearance APIs**

`extractpdf_pdf_appearance_resolve()` must read the raw dictionary only. Its control flow is fixed:

```text
strict raw /Rect -> normalize finite rectangle
strict raw /F -> reject viewer-dependent visual flags
reject annotation /OC
read /AP as dictionary
read /AP /N
  indirect stream -> selected Form
  dictionary -> require name /AS -> lookup exact state -> indirect stream
  otherwise -> FORMAT/UNSUPPORTED per spec
validate /Subtype /Form
validate exact finite /BBox[4]
validate optional exact finite /Matrix[6]
validate optional /Resources dictionary
reject appearance /OC
BM = transform_rect(BBox, Matrix)
reject zero/non-finite transformed width/height
compute sx/sy/tx/ty and require every result finite
copy /AS name bytes into owned semantic storage when stateful
```

Do not call `pdf_update_annot`, `pdf_update_page`, widget value setters, form event APIs, or any appearance-generation helper.

- [ ] **Step 3: Build a pointer-free annotation plan in `/Annots` order**

For `EXTRACTPDF_FLATTEN_ANNOTATIONS`, `extractpdf_pdf_flatten_build_plan()` walks every page's raw `/Annots` strictly. The per-page alias algorithm is:

```c
for each selected target in original Annots order:
    resolve appearance Form in this context
    search prior selected targets on this page for the same indirect Form identity
    if found:
        target.appearance_slot = prior.appearance_slot
    else:
        target.appearance_slot = next first-use slot
```

Compare indirect Form identity only within one MuPDF context (`num` + `gen` or equivalent strict same-indirect-object check). Never place that object number, generation, or pointer into semantic equivalence data.

In this task, any call containing `EXTRACTPDF_FLATTEN_WIDGETS` remains fail-closed with `EXTRACTPDF_ERROR_STATE` after source security/form discovery reaches the Widget portion; Task 5 removes that temporary boundary. Annotation-only success must not depend on Widget implementation.

- [ ] **Step 4: Add the strict semantic no-op path**

After source security + strict requested annotation discovery:

```c
if (!source_plan->any_changed)
    return extractpdf_serialize_pdf(document->ctx, source_pdf, out_output);
```

Do not create a private context and do not apply changed-page Link/closure/tagged restrictions when there is no selected requested annotation.

- [ ] **Step 5: Add private canonical reparse and plan-equivalence before any write**

Refactor `src/pdf_flatten.c` around the existing Poster Split pattern:

```text
source security
source plan
semantic no-op -> serialize once
changed:
  canonical serialize source -> seed
  fz_new_context
  open seed in private pdf_document
  pdf_disable_js
  private security check
  independently build private plan
  compare pointer-free plans field-by-field
  mismatch -> FORMAT
  policy not yet complete / writer not yet present -> STATE
```

The equivalence function compares flags, page counts, target/page order, class, Rect/flags, state presence/name, BBox, Matrix, placement, and `appearance_slot` pattern exactly. It never dereferences MuPDF objects.

- [ ] **Step 6: Split the one CTest into focused source modules without adding a second CTest**

Convert the original `main` using the same pattern as form/crop/poster tests:

```cmake
set_source_files_properties(test_pdf_flatten.c PROPERTIES
  COMPILE_DEFINITIONS "main=extractpdf_pdf_flatten_base_main")
target_sources(extractpdf_test_pdf_flatten PRIVATE
  test_pdf_flatten_appearance.c
  test_pdf_flatten_main.c)
target_link_libraries(extractpdf_test_pdf_flatten PRIVATE
  unofficial::libmupdf::libmupdf)
```

`test_pdf_flatten_main.c` calls the base test and appearance module in a deterministic fixed order.

- [ ] **Step 7: Verify appearance/preflight GREEN and intentional pre-write boundary**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

Expected at this task boundary:

```text
appearance resolver unit/raw assertions pass
negative FORMAT/UNSUPPORTED cases pass
annotation semantic no-op passes
changed valid Square reaches plan equivalence and then the intentional STATE boundary
```

The original valid Square end-to-end assertion remains RED until Task 4; keep that RED explicit rather than changing it to accept `STATE`.

- [ ] **Step 8: Commit strict resolver and plan gate**

```bash
git add \
  src/pdf_appearance_common.h src/pdf_appearance_common.c \
  src/pdf_flatten_internal.h src/pdf_flatten_preflight.c src/pdf_flatten.c \
  CMakeLists.txt \
  tests/test_pdf_flatten.c tests/test_pdf_flatten_internal.h \
  tests/test_pdf_flatten_appearance.c tests/test_pdf_flatten_main.c \
  tests/fixtures/flatten-appearance.pdf \
  tests/fixtures/flatten-appearance-malformed.pdf \
  tests/CMakeLists.txt
git commit -m "feat: add strict Flatten Bake preflight"
```

---

### Task 4: Page Bake Writer, Copy-on-Write Resources/Contents/Annots

**Files:**
- Create: `src/pdf_flatten_bake.c`
- Modify: `src/pdf_flatten_internal.h`
- Modify: `src/pdf_flatten_preflight.c`
- Modify: `src/pdf_flatten.c`
- Modify: root `CMakeLists.txt`
- Create: `tests/fixtures/flatten-contents.pdf`
- Create: `tests/test_pdf_flatten_raw.c`
- Modify: `tests/test_pdf_flatten_internal.h`
- Modify: `tests/test_pdf_flatten_main.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

Add private runtime types that exist only in the private context:

```c
typedef struct extractpdf_pdf_flatten_runtime_target {
    pdf_obj *annotation;
    pdf_obj *appearance;
} extractpdf_pdf_flatten_runtime_target;

typedef struct extractpdf_pdf_flatten_runtime_page {
    pdf_obj *page;
    extractpdf_pdf_flatten_runtime_target *targets;
    size_t target_count;
} extractpdf_pdf_flatten_runtime_page;

typedef struct extractpdf_pdf_flatten_runtime {
    extractpdf_pdf_flatten_runtime_page *pages;
    size_t page_count;
} extractpdf_pdf_flatten_runtime;
```

Add:

```c
extractpdf_status extractpdf_pdf_flatten_resolve_runtime(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime **out_runtime);

extractpdf_status extractpdf_pdf_flatten_apply_bake(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime);

void extractpdf_pdf_flatten_drop_runtime(
    fz_context *ctx,
    extractpdf_pdf_flatten_runtime *runtime);
```

- [ ] **Step 1: Add raw writer REDs that inspect object identity, not only pixels**

`tests/test_pdf_flatten_raw.c` must prove:

```text
changed page gets page-local Resources dictionary
inherited/shared source Resources dictionary remains unchanged
page-local XObject dictionary preserves existing entries by reference
/EPB aliases skip pre-existing /EPB0 /EPB1 deterministically
same appearance identity reused by two annotations has one alias
selected aliases follow first-use Annots order
absent Contents -> bake stream
one indirect Contents stream -> new [old,bake] array
valid stream-array Contents -> new array, same old refs/order, exactly one appended bake stream
malformed Contents array entry -> FORMAT before first write
source Contents array identity/content remains unchanged
new page Annots array omits selected exact identities and preserves surviving identity/order
no surviving Annots -> remove page-local /Annots
existing appearance Form stream bytes/BBox/Matrix/Resources are unchanged
```

Use MuPDF only in the test executable for raw graph inspection. Public production ABI remains opaque.

- [ ] **Step 2: Make changed-page structural validation part of preflight**

Before runtime resolution, every changed page strictly validates:

```text
effective Resources absent or dictionary
Resources/XObject absent or dictionary
Contents absent OR one indirect stream OR array of only indirect streams
Annots original array still matches the semantic target ordinals
```

A direct stream, direct dictionary masquerading as a stream, non-stream indirect entry, null array entry, malformed Resources, or malformed XObject is `EXTRACTPDF_ERROR_FORMAT`.

This validation is performed in both source and private preflight so a malformed topology cannot reach the writer.

- [ ] **Step 3: Resolve all private runtime objects before first write**

For each private page plan, resolve the page dictionary and each target by its original `/Annots` ordinal, then rerun the raw appearance resolver and retain exact private annotation/Form references. Verify the resolved semantic fields still equal the already-equivalent private target plan.

No writer may rediscover a target by a mutable array index after mutation starts.

- [ ] **Step 4: Implement deterministic page-local Resources/XObject copies**

The writer sequence for one changed page is fixed:

```text
copy effective Resources key/value references into a new page-local dictionary
copy effective XObject entries into a new XObject dictionary (or create empty)
for each first-use appearance slot in target order:
    find first unused /EPB<number> against the copied XObject dictionary
    add alias -> exact existing private appearance Form reference
put copied XObject into copied Resources
put copied Resources on changed page
```

Store each final alias number in private runtime scratch derived from the already-stable first-use slot. Do not mutate semantic-plan values after equivalence.

- [ ] **Step 5: Implement one canonical bake stream per changed page**

Add a private finite formatter with this contract:

```c
static extractpdf_status flatten_format_number(
    float value,
    char *buffer,
    size_t buffer_size)
{
    int written;

    if (!isfinite(value) || buffer == NULL || buffer_size == 0)
        return EXTRACTPDF_ERROR_FORMAT;
    written = fz_snprintf(buffer, buffer_size, "%.9g", (double)value);
    if (written <= 0 || (size_t)written >= buffer_size ||
        strchr(buffer, ',') != NULL)
        return EXTRACTPDF_ERROR_FORMAT;
    return EXTRACTPDF_OK;
}
```

The test suite pins emitted matrix tokens and repeated output bytes on all CI platforms. If MuPDF 1.28.2's `fz_snprintf` contract fails those locale/byte tests, replace this helper inside this task with a locale-independent conversion routine; do not fall back to ambient `sprintf`/`snprintf`.

Bake bytes for each target are exactly:

```text
q\n
<a> <b> <c> <d> <e> <f> cm\n
/EPB<n> Do\n
Q\n
```

One page buffer contains all targets in original `/Annots` order, then becomes one new indirect content stream.

- [ ] **Step 6: Replace Contents and Annots copy-on-write**

Writer order inside one page:

```text
1. create new Resources/XObject objects
2. create bake stream
3. create replacement Contents shape using only original stream refs + bake
4. create replacement Annots array from original refs minus selected refs
5. install Resources
6. install Contents
7. install/remove Annots
```

All allocations and exact private object references are prepared before the first dictionary put where practical; any MuPDF exception aborts the entire private document and no output is published.

- [ ] **Step 7: Wire writer only after source/private equivalence**

Replace Task 3's changed-path `STATE` boundary in `src/pdf_flatten.c` with:

```text
resolve private runtime
apply page bake
serialize private document to immutable output
```

At this stage only annotation-only pages admitted by the conservative policy are expected GREEN. Widget requests remain fail-closed until Task 5.

- [ ] **Step 8: Prove annotation happy-path GREEN**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

Required GREEN observations now include Square, FreeText, eligible Text, state `/AS`, Matrix/BBox placement, shared appearance alias reuse, strict Contents COW, Annots removal, source raw graph unchanged, and repeated render equality for those annotation-only pages.

- [ ] **Step 9: Commit page bake writer**

```bash
git add \
  src/pdf_flatten_bake.c src/pdf_flatten_internal.h \
  src/pdf_flatten_preflight.c src/pdf_flatten.c CMakeLists.txt \
  tests/test_pdf_flatten_raw.c tests/test_pdf_flatten_internal.h \
  tests/test_pdf_flatten_main.c tests/fixtures/flatten-contents.pdf \
  tests/CMakeLists.txt
git commit -m "feat: bake annotation appearances into page content"
```

---

### Task 5: Strict Widget Provenance and Normative AcroForm Pruning

**Files:**
- Create: `src/pdf_flatten_form.c`
- Modify: `src/pdf_flatten_internal.h`
- Modify: `src/pdf_flatten_preflight.c`
- Modify: `src/pdf_flatten_bake.c`
- Modify: `src/pdf_flatten.c`
- Modify: root `CMakeLists.txt`
- Create: `tests/fixtures/flatten-widgets.pdf`
- Create: `tests/fixtures/flatten-form-cow.pdf`
- Create: `tests/test_pdf_flatten_form.c`
- Modify: `tests/test_pdf_flatten_internal.h`
- Modify: `tests/test_pdf_flatten_main.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

`src/pdf_flatten_form.c` owns the opaque `extractpdf_pdf_flatten_form_plan` and exports:

```c
extractpdf_status extractpdf_pdf_flatten_form_preflight(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_flatten_plan *plan);

int extractpdf_pdf_flatten_form_plan_equivalent(
    const extractpdf_pdf_flatten_form_plan *left,
    const extractpdf_pdf_flatten_form_plan *right);

extractpdf_status extractpdf_pdf_flatten_form_resolve_runtime(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime);

extractpdf_status extractpdf_pdf_flatten_form_apply(
    fz_context *ctx,
    pdf_document *document,
    const extractpdf_pdf_flatten_plan *plan,
    extractpdf_pdf_flatten_runtime *runtime);

void extractpdf_pdf_flatten_form_drop_plan(
    extractpdf_pdf_flatten_form_plan *form);
```

The form plan stores owned copies of strict locator steps and removal/survival decisions only; it stores no `pdf_obj *`.

- [ ] **Step 1: Write Widget/form REDs covering both field layouts**

`tests/test_pdf_flatten_form.c` must cover:

```text
Text Widget existing appearance -> pixels preserved, Widget disappears
stale field /V vs Widget /AS -> baked pixels follow /AS exactly
checkbox current AP state preserved
radio current AP state preserved
multi-Widget field -> all Widgets baked before field branch disappears
separate terminal field + Widget Kids -> exact selected Widget children omitted
merged field+Widget root -> page annotation and root field removed
pre-existing widgetless field outside affected closure survives unchanged
affected grouping ancestor with last child removed -> ancestor pruned
affected ancestor with one unaffected child -> ancestor survives with replacement Kids preserving child identity/order
CO removes only removed field refs and preserves surviving order
Fields/Kids/CO changed arrays have new object identity
zero surviving root Fields -> Catalog AcroForm key removed
surviving root Fields -> AcroForm unrelated keys unchanged
annotation-only call leaves complete AcroForm graph unchanged
```

- [ ] **Step 2: Use the existing strict form builder as the only Widget authority**

When `EXTRACTPDF_FLATTEN_WIDGETS` is present:

```c
status = extractpdf_pdf_form_build(
    ctx, document, 1, &model, &provenance);
```

The existing build path already performs strict form-tree validation and live Widget reconciliation/capture. The Flatten adapter matches every selected page `/Annots` Widget to exactly one provenance field/widget identity and copies the field locator into its own semantic form plan.

Do not derive persistent identity from public field/widget indices alone.

- [ ] **Step 3: Build the affected field-node closure from locator paths**

For every selected Widget field locator `steps[0..n-1]`:

```text
root Fields[steps[0]]
then Kids[steps[1]]
...
owning terminal/group head
```

Mark every field dictionary encountered on that path as affected. Union paths by strict object identity inside this context. Nodes not in this union are never prune candidates.

The plan records locator steps and bottom-up decisions, not pointers.

- [ ] **Step 4: Classify separate Widget child vs merged field+Widget**

For each selected live Widget:

```text
if Widget object identity is the owning field/group node identity:
    merged = true -> owning affected field node is removable after bake
else:
    merged = false -> Widget must be an exact child entry under affected field Kids
```

Any provenance shape that cannot prove one of those cases is `EXTRACTPDF_ERROR_FORMAT`.

- [ ] **Step 5: Compute bottom-up survival exactly as the correction specifies**

For each affected field from deepest locator to root:

```text
start with original Kids order
omit exact selected separate Widget children
omit affected child fields already classified removed
preserve every unaffected child identity/order

remove this affected field from parent/root iff:
  selected merged field+Widget
  OR no Kids survive after the omissions
otherwise:
  survive and require a replacement Kids array if its Kids membership changed
```

A field outside the affected closure is not reevaluated for emptiness.

- [ ] **Step 6: Strictly preflight `/CO`, `/XFA`, and `/NeedAppearances`**

For a real Widget bake:

```text
AcroForm /XFA present -> UNSUPPORTED
NeedAppearances absent/false -> accepted
NeedAppearances true -> UNSUPPORTED
NeedAppearances present non-boolean -> FORMAT
CO absent -> accepted
CO present -> array of strict field references resolvable in form model
unresolved/malformed CO entry -> FORMAT
```

Build the surviving `/CO` order semantically during preflight; never execute calculation.

- [ ] **Step 7: Resolve all affected private fields/arrays before first form write**

After private plan equivalence, walk each stored locator against the unchanged private `/Fields`/`Kids` topology, retain exact private owner/parent references, and verify expected identities/membership. Resolve `/CO` and Catalog `/AcroForm` before page/form writes begin.

- [ ] **Step 8: Apply copy-on-write form arrays bottom-up**

Writer behavior is exact:

```text
for deepest affected surviving owner -> root:
    construct a new Kids array with surviving original refs in original order
    replace owner's Kids with new array
    if no child survives and owner is removed, remove Kids before parent pruning
construct replacement root Fields array if membership changed
construct replacement CO array if membership changed
if no root Fields survive:
    remove Catalog AcroForm
else:
    preserve AcroForm and unrelated keys
```

Do not call `pdf_array_delete()` on source/shared `/Kids`, `/Fields`, or `/CO` arrays.

- [ ] **Step 9: Enable Widget and combined target planning**

Remove Task 3's temporary Widget `STATE` boundary. `pdf_flatten_preflight.c` must merge ordinary annotations and strict-provenance Widgets into one target list in page `/Annots` order when both flags are set.

Page writer removes selected Widgets from `/Annots`; form writer then prunes field provenance using already-resolved exact private identities. Both execute only after complete source/private semantic-plan equivalence.

- [ ] **Step 10: Verify Widget/form GREEN**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

At this gate all correction tests for affected-node closure and `/Fields`/`Kids`/`CO` replacement identity must be GREEN.

- [ ] **Step 11: Commit strict Widget pruning**

```bash
git add \
  src/pdf_flatten_form.c src/pdf_flatten_internal.h \
  src/pdf_flatten_preflight.c src/pdf_flatten_bake.c src/pdf_flatten.c \
  CMakeLists.txt \
  tests/test_pdf_flatten_form.c tests/test_pdf_flatten_internal.h \
  tests/test_pdf_flatten_main.c \
  tests/fixtures/flatten-widgets.pdf tests/fixtures/flatten-form-cow.pdf \
  tests/CMakeLists.txt
git commit -m "feat: prune flattened Widget provenance"
```

---

### Task 6: Link Neutrality, Visual Closure, Relationships, and Fail-Closed Policy Matrix

**Files:**
- Modify: `src/pdf_flatten_preflight.c`
- Modify: `src/pdf_flatten_internal.h`
- Modify: `src/pdf_flatten_form.c`
- Create: `tests/fixtures/flatten-policy.pdf`
- Create: `tests/fixtures/flatten-relationships.pdf`
- Create: `tests/fixtures/flatten-tagged.pdf`
- Create: `tests/test_pdf_flatten_policy.c`
- Modify: `tests/test_pdf_flatten_internal.h`
- Modify: `tests/test_pdf_flatten_main.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- No public ABI change.
- `extractpdf_pdf_flatten_build_plan()` now ends with `policy_complete = 1` only after all changed-page closure, Link, relationship, tagged, and Widget form-policy facts are proven.
- `extractpdf_pdf_flatten_plan_equivalent()` rejects plans unless both sides have `policy_complete == 1` and all policy facts match.

- [ ] **Step 1: Add the normative Link-border RED matrix**

`tests/test_pdf_flatten_policy.c` must assert:

```text
Link /BS << /W 0 >>, no /AP /N -> neutral, survives changed page
Link /Border [0 0 0], no /BS, no /AP /N -> neutral
Link with neither /BS nor /Border -> UNSUPPORTED on changed page (default width 1)
Link /BS missing /W -> UNSUPPORTED (default width 1)
Link /BS /W 2 -> UNSUPPORTED
Link /BS /W 0 + conflicting /Border width 4 -> neutral because BS wins
Link /BS /W 4 + conflicting /Border width 0 -> UNSUPPORTED because BS wins
Link /AP /N present in any form -> UNSUPPORTED even with zero border
Link /BS non-dictionary -> FORMAT
Link /BS /W non-number, negative, NaN/Inf -> FORMAT
Link /Border too short or consumed first-three non-number/non-finite -> FORMAT
```

- [ ] **Step 2: Implement one strict read-only Link neutrality helper in preflight**

The helper follows this exact branch order:

```text
if AP dictionary contains N in any form -> UNSUPPORTED
if BS key is present:
    require dict
    if W absent -> UNSUPPORTED
    require finite non-negative numeric W
    return OK only when W == 0, else UNSUPPORTED
else if Border key is present:
    require array len >= 3
    require first three finite numbers
    return OK only when third == 0, else UNSUPPORTED
else:
    return UNSUPPORTED
```

It never asks MuPDF to synthesize an appearance or infer viewer behavior.

- [ ] **Step 3: Add ordinary subtype and selected-flag policy assertions**

Exercise at least one page for each approved support/rejection class, including explicit Redact, FileAttachment, RichMedia, and unknown subtype `UNSUPPORTED`, plus Invisible/Hidden/NoView/ToggleNoView cases. Keep malformed raw `/F` as `FORMAT`.

- [ ] **Step 4: Add forward and reverse Popup/IRT relationship REDs**

Build a whole-document annotation registry before changed-page approval. For each annotation identity record page/ordinal and these raw relationships:

```text
selected /Popup -> target identity
Popup /Parent -> owner identity
selected /IRT -> replied-to identity
reverse references discovered by registry scan
```

If a selected target participates in either direction of one of those relations, return `UNSUPPORTED`. An unrelated Popup that has no selected relation remains untouched.

- [ ] **Step 5: Enforce page visual closure after target discovery**

For every page with at least one selected target:

```text
ANNOTATIONS only:
    any Widget on page -> UNSUPPORTED
WIDGETS only:
    any ordinary annotation on page -> UNSUPPORTED
ANNOTATIONS | WIDGETS:
    every supported ordinary annotation and every strict-provenance Widget is selected
all modes:
    Links may remain only if strict neutrality helper returns OK
    selected relationship violations remain UNSUPPORTED
```

The combined target sequence is the original `/Annots` sequence; never regroup annotations and Widgets by class.

- [ ] **Step 6: Enforce changed-only tagged and form policy**

A real bake with `/StructTreeRoot` is `UNSUPPORTED`. Semantic no-op skips this bake-only tagged restriction after strict requested-class discovery.

For real Widgets, retain Task 5's exact XFA/NeedAppearances classification.

- [ ] **Step 7: Record policy facts in source/private equivalence**

For each page plan, equivalence must prove at least:

```text
same changed page index
same target count/order/kinds
same surviving neutral-Link count/order
same relationship-clean classification
same closure mode
same deterministic appearance-slot pattern
```

Do not use source/private object numbers to establish equality.

- [ ] **Step 8: Run the complete policy target**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

Required GREEN includes all 12 normative correction cases plus the main-spec subtype, Popup/IRT, visual-closure, tagged, XFA, and NeedAppearances cases.

- [ ] **Step 9: Commit complete fail-closed policy**

```bash
git add \
  src/pdf_flatten_preflight.c src/pdf_flatten_internal.h src/pdf_flatten_form.c \
  tests/test_pdf_flatten_policy.c tests/test_pdf_flatten_internal.h \
  tests/test_pdf_flatten_main.c \
  tests/fixtures/flatten-policy.pdf \
  tests/fixtures/flatten-relationships.pdf \
  tests/fixtures/flatten-tagged.pdf \
  tests/CMakeLists.txt
git commit -m "feat: enforce Flatten Bake closure policy"
```

---

### Task 7: Determinism, Semantic No-Op, Lifetime, Atomicity, and Complete #55 Matrix

**Files:**
- Modify: `src/pdf_flatten.c`
- Modify: `src/pdf_flatten_preflight.c`
- Modify: `src/pdf_flatten_bake.c`
- Modify: `src/pdf_flatten_form.c`
- Create: `tests/test_pdf_flatten_determinism.c`
- Modify: `tests/test_pdf_flatten_raw.c`
- Modify: `tests/test_pdf_flatten_form.c`
- Modify: `tests/test_pdf_flatten_internal.h`
- Modify: `tests/test_pdf_flatten_main.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- No new public API.
- Final public function has only approved final error classifications; the temporary valid-input `EXTRACTPDF_ERROR_STATE` paths from Tasks 2/3 are gone.

- [ ] **Step 1: Add semantic no-op tests before changing orchestration**

Cases:

```text
annotation flatten on a document with only neutral Links -> canonical OK output
Widget flatten on valid AcroForm with zero Widgets -> canonical OK output
malformed Annots consumed by requested annotation discovery -> FORMAT, not no-op
malformed AcroForm consumed by requested Widget discovery -> FORMAT, not no-op
no-op repeated twice from identical source -> byte-identical
no-op source state unchanged
```

- [ ] **Step 2: Add source success/failure immutability snapshots**

Before and after a successful changed flatten and representative FORMAT/UNSUPPORTED failures, snapshot and compare:

```text
page count and bounds
raw selected/unselected Annots identity/order
annotation public snapshots
form field/widget snapshots
raw AcroForm Fields/Kids/CO identity where present
source render checksum for changed pages
```

The source object graph must not change in either success or failure cases.

- [ ] **Step 3: Add output lifetime and repeated-byte tests**

Pattern:

```c
CHECK(extractpdf_flatten_interactive(document, flags, &a) == EXTRACTPDF_OK);
CHECK(extractpdf_flatten_interactive(document, flags, &b) == EXTRACTPDF_OK);
CHECK(output_bytes_equal(a, b));

extractpdf_close(document);
document = NULL;
CHECK(reopen_output_bytes(a, &reopened) == EXTRACTPDF_OK);
CHECK(render_expected_pages(reopened));
```

The helper that reopens output bytes may use a temporary test file if the public API has no memory-open entry; it must not make production depend on filesystem round-trips.

- [ ] **Step 4: Prove flag-set semantics**

There are only three legal non-zero V1 flag sets:

```text
ANNOTATIONS
WIDGETS
ANNOTATIONS | WIDGETS
```

Repeated calls with the same numeric set are byte-identical. There is no ordering API to normalize beyond the bitset itself.

- [ ] **Step 5: Add security fail-closed observations**

Using existing fixtures:

```text
encrypted input -> UNSUPPORTED, output NULL
already-signed input -> UNSUPPORTED, output NULL
tagged real bake -> UNSUPPORTED, output NULL
non-PDF -> existing mapped FORMAT/MuPDF policy, output NULL
```

Security policy is checked before output publication for semantic no-op as required by the spec.

- [ ] **Step 6: Assert final render-equivalence matrix**

For each positive baked appearance fixture, render source and output with identical ExtractPDF render options and compare dimensions/components and pixel bytes (or the existing exact bitmap checksum helper when byte-for-byte bitmap comparison is already established in tests).

Minimum positive set:

```text
Square
FreeText
eligible Text
non-identity Matrix/non-zero BBox origin
state dictionary annotation
shared appearance identity
Text Widget
stale V vs AS Widget
checkbox
radio
combined annotation + Widget page
```

Links that remain are asserted structurally, not flattened into the render oracle.

- [ ] **Step 7: Verify all required structural postconditions after reopen**

The reopened output must prove:

```text
requested selected ordinary annotations gone
requested Widgets gone
neutral Links still present and unchanged
annotation-only leaves AcroForm graph unchanged
Widget-only leaves ordinary annotations unchanged on closure-satisfying pages
combined mode preserves selected relative stacking in one bake stream
unrelated widgetless form subtree survives
removed fields absent from CO
existing page contents references/order preserved before one bake stream
page resources contain deterministic aliases without source resource mutation
```

- [ ] **Step 8: Remove every temporary TDD boundary and search for forbidden execution APIs**

Run repository searches:

```bash
git grep -n 'EXTRACTPDF_ERROR_STATE' -- src/pdf_flatten.c src/pdf_flatten_*.c src/pdf_appearance_common.c
git grep -nE 'pdf_update_annot|pdf_update_page|pdf_set_field_value|pdf_set_choice_field_value|pdf_calculate|pdf_enable_js' -- src/pdf_flatten.c src/pdf_flatten_*.c src/pdf_appearance_common.c
```

Expected:

```text
no valid-input temporary STATE branch remains
no appearance regeneration/form mutation/event/JS execution API is referenced
```

- [ ] **Step 9: Run focused and full Linux suites**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected suite count: exactly 25 CTests, 25 passing.

- [ ] **Step 10: Run a fresh sanitizer configuration**

```bash
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

Expected: 25/25 pass with no ASan/UBSan finding.

- [ ] **Step 11: Commit the completed V1 behavior matrix**

```bash
git add \
  src/pdf_flatten.c src/pdf_flatten_preflight.c \
  src/pdf_flatten_bake.c src/pdf_flatten_form.c \
  tests/test_pdf_flatten_determinism.c tests/test_pdf_flatten_raw.c \
  tests/test_pdf_flatten_form.c tests/test_pdf_flatten_internal.h \
  tests/test_pdf_flatten_main.c tests/CMakeLists.txt
git commit -m "test: complete Flatten Bake invariants"
```

---

### Task 8: Exact-Head Semantic-Write Audit, Same-SHA Full CI, and Integration STOP

**Files:**
- No expected production or test changes after the head is frozen.
- Do not modify workflows.

**Interfaces:**
- Produces one immutable candidate SHA for review/integration.
- Does not merge.

- [ ] **Step 1: Freeze and record exact candidate head**

```bash
HEAD_SHA=$(git rev-parse HEAD)
git status --short
git diff --stat master...HEAD
git diff --name-status master...HEAD
```

Expected: clean worktree. Diff contains only the approved public header addition, Flatten/appearance production modules, one test target/modules, static fixtures, CMake source/target registration, and the committed specs/plan. No workflow, metadata, unrelated transform, or broad form refactor is present.

- [ ] **Step 2: Audit semantic writes against the allowlist**

Review every mutation call in `src/pdf_flatten_bake.c` and `src/pdf_flatten_form.c`. Each write must map to exactly one approved category:

```text
changed page local Resources/XObject materialization
changed page Contents replacement
changed page Annots replacement/removal
affected field Kids replacement/removal
root Fields replacement
CO replacement
Catalog AcroForm removal when no root fields survive
new bake stream / private page-local container objects
```

Any write to page boxes, Rotate/UserUnit, existing stream bytes, appearance Form dictionaries, Link dictionaries, metadata, Outline, Names/Dests, field values/options, action/JS/security state blocks the candidate.

- [ ] **Step 3: Audit copy-on-write identities with raw tests at exact head**

Run:

```bash
ctest --test-dir build -R '^extractpdf\.pdf_flatten$' --output-on-failure
```

Confirm the raw suite explicitly proves replacement-object identity for changed `/Contents`, `/Annots`, `/Fields`, `/Kids`, and `/CO` arrays and preservation of surviving element identities/order.

- [ ] **Step 4: Run exact-head Linux static + sanitizer proof**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Record the command output against `HEAD_SHA`; do not treat an earlier SHA as proof.

- [ ] **Step 5: Push the frozen SHA and wait for ordinary PR Linux CI**

```bash
git push origin feat/flatten-bake
```

Verify the GitHub Actions run's head SHA equals `HEAD_SHA`. Linux static and sanitizer jobs must both pass on that exact SHA.

- [ ] **Step 6: Trigger existing same-SHA macOS/Windows proof without editing workflow YAML**

Apply the existing `full-ci` PR label to the frozen head. Verify the run head SHA still equals `HEAD_SHA`.

Required same-SHA result:

```text
Linux static + 25 CTests            PASS
Linux ASan/UBSan + 25 CTests        PASS
macOS build + 25 CTests             PASS
Windows MSVC DLL build + 25 CTests  PASS
Windows consumer resolves extractpdf_flatten_interactive export PASS
cross-platform Flatten render fixtures agree PASS
```

If a platform correction is required, remove the integration-ready claim, commit the smallest fix, obtain a new `HEAD_SHA`, and rerun all exact-head proof. Do not mix evidence from different SHAs.

- [ ] **Step 7: Review scope and required-observation coverage one final time**

Map every main-spec observation 1-31 and correction observation 1-12 to a named test function in `extractpdf.pdf_flatten`. Confirm there is no untested normative requirement and no fixture expectation that depends on viewer-specific appearance regeneration.

- [ ] **Step 8: STOP at the explicit integration gate**

Report the exact candidate SHA, PR, 25/25 same-SHA platform proof, semantic-write audit result, and any remaining review comments. Do not merge, update master, close #55, or update #48/#2 to integrated state without an explicit `go integrate` authorization.

---

### Task 9: Integrate Only After Explicit `go integrate`

**Precondition:** Task 8 is complete on one exact candidate SHA and the user explicitly authorizes integration.

**Files:**
- No new feature scope.

**Interfaces:**
- Produces integrated `master` proof only.

- [ ] **Step 1: Reconfirm the PR head is still the reviewed exact SHA**

```bash
git rev-parse HEAD
```

Compare to the Task 8 candidate SHA and GitHub PR head. Any mismatch returns to Task 8 proof.

- [ ] **Step 2: Merge through the repository's normal protected flow**

Do not squash/rewrite in a way that loses the reviewed source unless that is the repository's established merge policy and the resulting integrated tree is verified identical.

- [ ] **Step 3: Prove integrated master exact tree/SHA**

After merge, fetch master and verify the integrated tree contains the reviewed feature tree. Run/observe the master CI required by repository policy on the resulting exact master SHA.

- [ ] **Step 4: Only after integrated proof, close tracking state**

Close #55 and update #48/#2 with the integrated master SHA, 25-CTest count, and same-SHA platform evidence. No follow-up #56 GC/idempotence scope is pulled into #55.
