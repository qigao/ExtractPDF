# ExtractPDF AcroForm Value Mutation V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, session-local, journal-atomic AcroForm value mutation to the existing `extractpdf_pdf_edit` layer without making `extractpdf_form` mutable or executing PDF form runtime behavior.

**Architecture:** Reuse the strict AcroForm parser as the single semantic authority, but refactor Widget reconciliation onto raw page objects so observation never enters MuPDF page/annotation runtime. Add an optional live provenance sidecar for editor-only field identity, a separate form-ref registry, a typed value-assignment engine, and target-Widget update helpers. Checkbox/Radio mutate `/V` + `/AS` while preserving `/AP`; Text/Combo/List use one outer journal operation and target-only MuPDF Widget resynthesis after a mandatory zero-side-effect page-load safety gate.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, GitHub Actions Linux/macOS/Windows.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md`

## Global Constraints

- Integrated base is exactly `fdcb2f6cd489de34802d09989ab61a1af8cd1861`; implementation branch is `feat/acroform-value-mutation`.
- Pinned PDF engine is MuPDF **1.28.2**; do not change vcpkg pins, overlay ports, or CI workflow to make the feature pass.
- Existing suite starts at **20 CTests**; this slice adds exactly one new public test target, `extractpdf.pdf_form_mutation`, for **21 total**.
- First implementation checkpoint is a strict compile RED: existing targets #1-#20 still build, and only the new #21 target fails because the approved mutation ABI is absent.
- `extractpdf_form` remains immutable, deep-owned, document-independent, and free of MuPDF pointers.
- Mutation exists only on `extractpdf_pdf_edit`; do not add a second mutable form/document handle.
- Public mutation identity is `extractpdf_form_field_ref`; snapshot indices, field names, Widget indices, PDF object numbers, and option strings are never public mutation identity.
- Form refs use a token domain cryptographically/seman­tically distinct from annotation refs and are valid only for one editor session.
- Every editor form observation path must remain raw and side-effect-free. `extractpdf_pdf_edit_form_snapshot()`, `extractpdf_pdf_edit_form_field_ref_at()`, and strict form parsing/reconciliation must not call `pdf_load_page()`, `pdf_update_page()`, `pdf_update_open_pages()`, or any form event/runtime API.
- Raw page reconciliation must use `pdf_lookup_page_obj()` plus `pdf_page_obj_transform()` and raw `/Annots` dictionaries.
- Value mutation V1 supports Text, Checkbox, Radio, Combo, and List. PushButton, Signature, UNKNOWN, Text RichText, and Text FileSelect return `EXTRACTPDF_ERROR_UNSUPPORTED` after caller-command validation.
- Mutation-only preflight: `/XFA` present -> `UNSUPPORTED`; `/NeedAppearances true` -> `UNSUPPORTED`; non-Boolean `/NeedAppearances` -> `FORMAT`. Observation/ref discovery remains allowed.
- Do not execute Keystroke, Validate, Format, Calculate, Widget activation, SubmitForm, ResetForm, JavaScript, `/CO`, or page-wide recalculation.
- Do not call `pdf_set_field_value`, `pdf_set_annot_field_value`, `pdf_set_text_field_value`, `pdf_set_choice_field_value`, `pdf_choice_widget_set_value`, `pdf_toggle_widget`, `pdf_calculate_form`, or `pdf_reset_form`.
- Semantic no-op happens only after parse, mutation-capability preflight, caller validation, unsupported-mode checks, and ReadOnly checks. A no-op returns `OK` without a journal operation or any byte change.
- Every successful non-noop setter is exactly one outer `pdf_begin_operation()` / `pdf_end_operation()` pair; every failure after begin must `pdf_abandon_operation()`.
- Checkbox/Radio preserve existing `/AP` objects/streams and update only canonical `/V` plus Widget `/AS`.
- Text/Combo/List appearance refresh may use `pdf_load_page()` only after the mandatory Task 4 safety gate passes for pinned MuPDF 1.28.2; if that gate fails, **STOP implementation and return to design** rather than weakening no-execution/no-side-effect semantics.
- Text/Combo/List must set each target Widget editing state to true while requesting/updating its appearance, restore the previous editing state in exception-safe cleanup, and never use page-wide update as a shortcut.
- Multi-list caller order is preserved. Duplicate option indices are `ARGUMENT`. Single choice OPTION writes `/I [index]`; multi choice writes `/I` matching selected indices exactly.
- Missing and present-empty are distinct. Checkbox/Radio `PRESENT + 0` is explicit Off; `MISSING + 0` removes group-local `/V` and visually sets Widgets Off.
- If effective `/V` is inherited from outside the target logical group, assigning MISSING is `UNSUPPORTED`; PRESENT may safely override it at the group head.
- V1 does not mutate `/DV`, `/Ff`, `/Opt`, `/T`, `/TU`, `/MaxLen`, field structure, Widget structure/geometry, signatures, XFA, or NeedAppearances.
- Final frozen feature SHA must pass Linux static 21/21, Linux ASan/UBSan 21/21, macOS 21/21, and Windows DLL 21/21 on the same SHA before integration is even considered.
- Keep the PR draft/open through the feature proof/review gate. Merge only after explicit user authorization and then require integrated-master push proof before closing #46.

## File Structure

**Create**

- `src/pdf_edit_forms.c` — public editor form snapshot/ref APIs, form-ref registry, current-field resolution, mutation preflight, and the outer setter transaction coordinator.
- `src/pdf_edit_form_values.c` — public update validation, normalized assignment copy, semantic equality/no-op comparison, and canonical `/V`/`/I` writes.
- `src/pdf_edit_form_widgets.c` — target Widget wrapper preparation, page-load safety boundary, Checkbox/Radio `/AS`, Text/Combo/List targeted appearance refresh, and cleanup.
- `tests/test_pdf_form_mutation.c` — all public mutation/identity/round-trip/atomicity/no-execution tests.
- `tests/fixtures/acroform-mutation-basic.pdf` — deterministic Text + Checkbox + Radio fields, including zero-Widget and multi-Widget cases.
- `tests/fixtures/acroform-mutation-choice.pdf` — editable/non-editable Combo and single/multi List, including duplicate exports.
- `tests/fixtures/acroform-mutation-groups.pdf` — same-group local `/V`/`/I` overrides plus external inherited `/V` and sibling field.
- `tests/fixtures/acroform-mutation-modes.pdf` — ReadOnly/Required/NoExport/RichText/FileSelect/PushButton/Signature/UNKNOWN fields.
- `tests/fixtures/acroform-mutation-events.pdf` — Keystroke/Validate/Format/Calculate/activation/`/CO` sentinels plus target and unrelated Widgets.
- `tests/fixtures/acroform-mutation-need-appearances.pdf` — valid AcroForm with `/NeedAppearances true` and stable pre-existing Widget appearances.
- `tests/fixtures/acroform-mutation-xfa.pdf` — valid AcroForm plus present `/XFA`.
- `tests/fixtures/acroform-mutation-bad-need-appearances.pdf` — `/NeedAppearances` present with a non-Boolean value.
- `tests/fixtures/acroform-mutation-direct-field.pdf` — a direct logical field dictionary used to prove direct-group ref stability inside one editor session.

**Modify**

- `include/extractpdf/extractpdf.h` — add only the approved form-field ref/value-update ABI and three editor form functions.
- `CMakeLists.txt` — register the three focused production files.
- `src/pdf_form_common.h` — private provenance/build interfaces and common identity helpers.
- `src/pdf_form_common.c` — optional live provenance creation from the validated field graph.
- `src/pdf_form_widgets.c` — raw page-object reconciliation and optional provenance Widget capture.
- `src/pdf_form.c` — factor one internal reusable `extractpdf_form` snapshot builder used by document and editor paths.
- `src/pdf_edit_internal.h` — form registry state, private mutation context types, and test-only fault IDs.
- `src/pdf_edit.c` — dispose form registry entries and any retained PDF objects.
- `tests/CMakeLists.txt` — register fixture paths and `extractpdf.pdf_form_mutation`; add NeedAppearances fixture to the read-only form target.
- `tests/test_pdf_form.c` — raw-observation byte-preservation regression only; keep mutation assertions out of this target.
- `tests/pdf_edit_test_api.h` — add form-mutation fault IDs only.
- `tests/pdf_edit_fault_hook.c` — route the new fault IDs through the existing test hook.

---

### Task 1: Strict compile RED and draft PR

**Files:**
- Create: `tests/test_pdf_form_mutation.c`
- Create: all `tests/fixtures/acroform-mutation-*.pdf` files listed above
- Modify: `tests/CMakeLists.txt`
- Do **not** modify: `include/`, `src/`, root `CMakeLists.txt`, or `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: existing `extractpdf_pdf_edit`, immutable `extractpdf_form`, and current 20-test suite.
- Produces: one new CTest target that references the final approved ABI before that ABI exists.

- [ ] **Step 1: Add the compile-surface RED**

Create the new test with the final ABI spelled exactly as approved:

```c
#include <extractpdf/extractpdf.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static void compile_surface(void)
{
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "x";
    value.utf8_size = 1;

    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    if (0) {
        (void)extractpdf_pdf_edit_form_snapshot(edit, &form);
        (void)extractpdf_pdf_edit_form_field_ref_at(edit, 0, &ref);
        (void)extractpdf_pdf_edit_form_set_values(edit, &ref, &update);
    }
}

int main(void)
{
    compile_surface();
    return 0;
}
```

- [ ] **Step 2: Register exactly one new test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_pdf_form_mutation
  test_pdf_form_mutation.c)
target_link_libraries(extractpdf_test_pdf_form_mutation PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_form_mutation PRIVATE
  FORM_MUTATION_BASIC_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-basic.pdf"
  FORM_MUTATION_CHOICE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-choice.pdf"
  FORM_MUTATION_GROUPS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-groups.pdf"
  FORM_MUTATION_MODES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-modes.pdf"
  FORM_MUTATION_EVENTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-events.pdf"
  FORM_MUTATION_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-need-appearances.pdf"
  FORM_MUTATION_XFA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-xfa.pdf"
  FORM_MUTATION_BAD_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-bad-need-appearances.pdf"
  FORM_MUTATION_DIRECT_FIELD_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-direct-field.pdf")
add_test(NAME extractpdf.pdf_form_mutation
  COMMAND extractpdf_test_pdf_form_mutation)
set_tests_properties(extractpdf.pdf_form_mutation PROPERTIES TIMEOUT 60)
```

- [ ] **Step 3: Create deterministic fixtures with locked object intent**

Keep each PDF small, ASCII, classic-xref, and deterministic. The fixtures must encode these exact semantic cases; object numbers may differ, but the relationships may not:

```text
acroform-mutation-basic.pdf
  field 0: Text, no Widget, /V missing
  field 1: Text, one Widget, /V (old-text), existing uncompressed AP marker "TEXT-OLD-AP"
  field 2: Checkbox, two Widgets on different pages, one on-state /Yes, /V /Off
  field 3: Radio, three Widgets, states /One and /Two with /One repeated, /V /One

acroform-mutation-choice.pdf
  field 0: non-edit Combo, options [A,B], selected B
  field 1: editable Combo, options [Tokyo,Osaka], custom /V (Kyoto)
  field 2: single List, options [S,M,L], selected M
  field 3: multi List, options [Red,Green,Blue], selected [Red,Blue]
  field 4: Combo with duplicate export values [[dup First],[dup Second]] and /I [1]

acroform-mutation-groups.pdf
  parent logical field "shared" with unnamed same-group descendants
  group-head /V plus one descendant local /V override that is semantically equal
  choice group with same-group descendant /I override
  separate ancestor carrying /V (external) above target logical child and sibling logical child

acroform-mutation-modes.pdf
  ordinary Text with ReadOnly
  ordinary Text with Required
  ordinary Text with NoExport
  Text RichText
  Text FileSelect
  PushButton
  unsigned Signature
  valid unrecognized /FT -> UNKNOWN

acroform-mutation-events.pdf
  target Text Widget with /AA /K, /V, /F and activation action sentinel scripts
  unrelated Text Widget with fixed value "UNCHANGED"
  AcroForm /CO references a third calculated field with fixed value "CALC-SENTINEL"
  target and unrelated Widgets contain stable existing AP markers

acroform-mutation-need-appearances.pdf
  valid ordinary Text + Widget
  /NeedAppearances true
  existing AP marker "NEED-AP-KEEP"

acroform-mutation-xfa.pdf
  valid ordinary Text field plus present /XFA value

acroform-mutation-bad-need-appearances.pdf
  valid ordinary Text field plus /NeedAppearances (yes) as a PDF string

acroform-mutation-direct-field.pdf
  /Fields contains a direct Text field dictionary with no Widget
```

- [ ] **Step 4: Prove the strict RED**

Use the normal Linux configuration. The authoritative CI command sequence is:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
```

Expected result:

```text
ExtractPDF library builds
extractpdf_test_* existing targets #1-#20 build
extractpdf_test_pdf_form_mutation fails compilation
failure names the absent approved types/functions from compile_surface()
```

A failure in an old target is not the intended RED; fix the test/fixture registration before proceeding.

- [ ] **Step 5: Commit and open the draft PR**

```bash
git add tests/test_pdf_form_mutation.c tests/CMakeLists.txt tests/fixtures/acroform-mutation-*.pdf
git commit -m "test: lock AcroForm value mutation ABI RED"
git push -u origin feat/acroform-value-mutation
```

Create a **draft** PR targeting `master`, title `feat: add atomic AcroForm value mutation`, linking #46. Record the exact RED SHA/workflow in PR #47-or-current and issue #46. Keep it draft until Task 11.

---

### Task 2: Remove `pdf_load_page()` from strict form observation

**Files:**
- Modify: `tests/test_pdf_form.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/pdf_form_widgets.c`

**Interfaces:**
- Consumes: current immutable `extractpdf_document_form()` behavior.
- Produces: raw page-object Widget reconciliation using `pdf_lookup_page_obj()` and `pdf_page_obj_transform()`, with no page/annotation runtime entry.

- [ ] **Step 1: Add a read-only byte-preservation regression**

Add the NeedAppearances fixture path to the existing form test target and add helpers that compare serialized editor outputs:

```c
static void output_bytes(const extractpdf_output *o,
    const unsigned char **data, size_t *size)
{
    *data = NULL;
    *size = 0;
    CHECK(extractpdf_output_data(o, data, size) == EXTRACTPDF_OK);
    CHECK(*data != NULL);
    CHECK(*size != 0);
}

static extractpdf_output *snapshot_fresh_document(const char *path, int observe_form)
{
    extractpdf_document *d = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_output *out = NULL;
    extractpdf_form *form = NULL;

    CHECK(extractpdf_open(path, NULL, &d) == EXTRACTPDF_OK);
    if (observe_form) {
        CHECK(extractpdf_document_form(d, &form) == EXTRACTPDF_OK);
        extractpdf_drop_form(form);
    }
    CHECK(extractpdf_pdf_edit_begin(d, &edit) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_snapshot(edit, &out) == EXTRACTPDF_OK);
    extractpdf_drop_pdf_edit(edit);
    extractpdf_close(d);
    return out;
}

static void test_need_appearances_observation_is_byte_preserving(void)
{
    extractpdf_output *before = snapshot_fresh_document(
        ACROFORM_NEED_APPEARANCES_PDF, 0);
    extractpdf_output *after = snapshot_fresh_document(
        ACROFORM_NEED_APPEARANCES_PDF, 1);
    const unsigned char *a = NULL, *b = NULL;
    size_t na = 0, nb = 0;

    output_bytes(before, &a, &na);
    output_bytes(after, &b, &nb);
    CHECK(na == nb);
    CHECK(memcmp(a, b, na) == 0);
    extractpdf_drop_output(before);
    extractpdf_drop_output(after);
}
```

- [ ] **Step 2: Run only the existing form target and verify the regression fails on the old implementation**

Because Task 1 intentionally keeps the new mutation target in compile RED, build/run only the old target:

```bash
cmake --build build --target extractpdf_test_pdf_form --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form$' --output-on-failure
```

Expected before the refactor: byte-preservation fails or otherwise demonstrates that the old `pdf_load_page()` observation path is not an acceptable invariant for NeedAppearances.

If the old implementation happens to produce byte-identical output for this exact fixture, keep the test anyway; the implementation refactor is still required by the approved spec because `pdf_load_page()` enters form runtime by contract.

- [ ] **Step 3: Replace page loading with raw page-object lookup**

In `extractpdf_pdf_form_reconcile_widgets()`, replace the page lifecycle with this shape:

```c
for (page_index = 0;
     page_index < page_count && status == EXTRACTPDF_OK;
     ++page_index) {
    pdf_obj *page_obj = pdf_lookup_page_obj(ctx, document, page_index);
    pdf_obj *annots = NULL;
    fz_matrix page_ctm;
    int ai;
    int acount = 0;

    if (!pdf_is_dict(ctx, page_obj)) {
        status = EXTRACTPDF_ERROR_FORMAT;
        break;
    }

    pdf_page_obj_transform(ctx, page_obj, NULL, &page_ctm);

    if (extractpdf_pdf_dict_find(ctx, page_obj, PDF_NAME(Annots), &annots) &&
        pdf_is_array(ctx, annots))
        acount = pdf_array_len(ctx, annots);

    for (ai = 0; ai < acount && status == EXTRACTPDF_OK; ++ai) {
        pdf_obj *obj = pdf_array_get(ctx, annots, ai);
        /* Keep the existing strict Widget identity/P/Rect/F logic. */
        /* Compare Widget /P to page_obj, not page->obj. */
        /* Transform Rect with page_ctm. */
    }
}
```

Delete all `pdf_load_page()`, `page->obj`, `pdf_page_transform()`, and `fz_drop_page()` usage from `src/pdf_form_widgets.c`.

- [ ] **Step 4: Run all pre-existing tests that can build while #21 remains compile-RED**

```bash
cmake --build build --target \
  extractpdf_test_pdf_annotations \
  extractpdf_test_pdf_annotation_mutation \
  extractpdf_test_pdf_form --parallel 2
ctest --test-dir build -R 'extractpdf\.(pdf_annotations|pdf_annotation_mutation|pdf_form)$' --output-on-failure
```

Expected: all selected existing tests pass; the NeedAppearances observation check is byte-identical.

- [ ] **Step 5: Commit**

```bash
git add src/pdf_form_widgets.c tests/test_pdf_form.c tests/CMakeLists.txt
git commit -m "refactor: make form observation raw-page only"
```

---

### Task 3: Public ABI, reusable editor snapshots, live provenance, and form refs

**Files:**
- Create: `src/pdf_edit_forms.c`
- Modify: `include/extractpdf/extractpdf.h`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_form_common.h`
- Modify: `src/pdf_form_common.c`
- Modify: `src/pdf_form_widgets.c`
- Modify: `src/pdf_form.c`
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces public:
  - `extractpdf_form_field_ref`
  - `extractpdf_form_value_input`
  - `extractpdf_form_value_update`
  - `extractpdf_pdf_edit_form_snapshot()`
  - `extractpdf_pdf_edit_form_field_ref_at()`
  - `extractpdf_pdf_edit_form_set_values()` (minimal unsupported mutation shell only in this task)
- Produces private:
  - `extractpdf_pdf_form_provenance`
  - current-form build/drop helpers
  - form-ref registry resolver used by later tasks.

- [ ] **Step 1: Add failing identity/discovery tests before production changes**

Extend `tests/test_pdf_form_mutation.c` with:

```c
static void test_editor_snapshot_and_refs(void)
{
    extractpdf_document *d = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form *a = NULL;
    extractpdf_form *b = NULL;
    extractpdf_form_field_ref r0 = {{9, 9}};
    extractpdf_form_field_ref r0b = {{8, 8}};
    extractpdf_form_field_ref r1 = {{7, 7}};
    size_t count = 0;

    CHECK(extractpdf_open(FORM_MUTATION_BASIC_PDF, NULL, &d) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(d, &edit) == EXTRACTPDF_OK);
    extractpdf_close(d);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &a) == EXTRACTPDF_OK);
    CHECK(a != NULL);
    CHECK(extractpdf_form_field_count(a, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);

    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &r0) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 0, &r0b) == EXTRACTPDF_OK);
    CHECK(memcmp(&r0, &r0b, sizeof(r0)) == 0);
    CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, 1, &r1) == EXTRACTPDF_OK);
    CHECK(memcmp(&r0, &r1, sizeof(r0)) != 0);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &b) == EXTRACTPDF_OK);
    extractpdf_drop_pdf_edit(edit);
    CHECK(extractpdf_form_field_count(a, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);
    CHECK(extractpdf_form_field_count(b, &count) == EXTRACTPDF_OK);
    CHECK(count == 4);
    extractpdf_drop_form(a);
    extractpdf_drop_form(b);
}
```

Also test zero-reset on bad output/ref arguments and direct-field repeated discovery using `FORM_MUTATION_DIRECT_FIELD_PDF`.

Run the new target; expected failure is still absent ABI/linkage.

- [ ] **Step 2: Add the exact public ABI**

In `include/extractpdf/extractpdf.h`, add exactly:

```c
typedef struct extractpdf_form_field_ref {
    uint64_t opaque[2];
} extractpdf_form_field_ref;

typedef struct extractpdf_form_value_input {
    size_t struct_size;
    extractpdf_form_value_kind kind;
    size_t option_index;
    const char *utf8;
    size_t utf8_size;
} extractpdf_form_value_input;

typedef struct extractpdf_form_value_update {
    size_t struct_size;
    extractpdf_form_value_presence presence;
    const extractpdf_form_value_input *values;
    size_t value_count;
} extractpdf_form_value_update;

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_form **out_form);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_field_ref_at(
    extractpdf_pdf_edit *edit,
    size_t field_index,
    extractpdf_form_field_ref *out_ref);

EXTRACTPDF_API extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update);
```

Do not add live editor getters, Widget refs, option refs, clear/reset APIs, or mutation flags.

- [ ] **Step 3: Define optional private provenance**

In `src/pdf_form_common.h`, add focused live-only structures:

```c
typedef struct extractpdf_pdf_form_live_widget {
    pdf_obj *object;      /* kept by provenance */
    int page_index;
} extractpdf_pdf_form_live_widget;

typedef struct extractpdf_pdf_form_live_field {
    pdf_obj *group_head;  /* kept by provenance */
    pdf_obj **group_nodes;/* each kept */
    size_t group_node_count;
    pdf_obj *effective_v_owner; /* kept when present */
    int effective_v_present;
    extractpdf_pdf_form_live_widget *widgets;
    size_t widget_count;
} extractpdf_pdf_form_live_field;

typedef struct extractpdf_pdf_form_provenance {
    extractpdf_pdf_form_live_field *fields;
    size_t field_count;
} extractpdf_pdf_form_provenance;

extractpdf_status extractpdf_pdf_form_build(
    fz_context *ctx,
    pdf_document *document,
    int want_provenance,
    extractpdf_pdf_form_model **out_model,
    extractpdf_pdf_form_provenance **out_provenance);

void extractpdf_pdf_form_drop_provenance(
    fz_context *ctx,
    extractpdf_pdf_form_provenance *provenance);

int extractpdf_pdf_form_same_identity(
    fz_context *ctx,
    pdf_obj *left,
    pdf_obj *right);
```

Make the existing identity helper non-static rather than duplicating indirect/direct comparison logic.

- [ ] **Step 4: Build provenance from the validated graph, not from a second permissive traversal**

In `src/pdf_form_common.c`, while validated transient `nodes[]` and `groups[]` still exist:

```c
/* For each public terminal field: */
live->group_head = pdf_keep_obj(ctx, state->nodes[group->head_node].object);

/* Append every validated node whose group_index == group_index. */
live->group_nodes[live->group_node_count++] =
    pdf_keep_obj(ctx, state->nodes[node_index].object);

/* Resolve effective /V through the validated parent graph only. */
effective = extractpdf_pdf_form_effective_value(
    state, group->head_node, PDF_NAME(V));
if (effective.present) {
    live->effective_v_present = 1;
    live->effective_v_owner =
        pdf_keep_obj(ctx, state->nodes[effective.owner_node].object);
}
```

Do not use `pdf_dict_get_inheritable()` as provenance authority.

- [ ] **Step 5: Capture raw Widget provenance during existing strict reconciliation**

When a Widget has passed every current reconciliation check and `widget.field_index` is known:

```c
if (provenance != NULL) {
    extractpdf_pdf_form_live_field *live =
        &provenance->fields[widget.field_index];
    /* grow live->widgets */
    live->widgets[live->widget_count].object = pdf_keep_obj(ctx, obj);
    live->widgets[live->widget_count].page_index = page_index;
    ++live->widget_count;
}
```

Provenance Widget order must match the public global Widget order restricted to that field.

- [ ] **Step 6: Factor one reusable internal snapshot builder**

In `src/pdf_form.c`, define an internal helper declared in `pdf_form_common.h`:

```c
extractpdf_status extractpdf_pdf_form_snapshot_from_pdf(
    fz_context *ctx,
    pdf_document *pdf,
    extractpdf_form **out_form);
```

Its body runs `extractpdf_pdf_form_build(..., 0, ...)`, wraps the resulting model in `extractpdf_form`, and applies the same atomic publication rules as current `extractpdf_document_form()`.

Then make public `extractpdf_document_form()` only validate/obtain the PDF document and delegate to this helper. This guarantees editor and document snapshots cannot drift semantically.

- [ ] **Step 7: Add a separate form-ref registry to the editor**

In `src/pdf_edit_internal.h`:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;

struct extractpdf_pdf_edit {
    /* existing fields unchanged */
    extractpdf_pdf_edit_form_entry *form_entries;
    size_t form_entry_count;
    size_t form_entry_capacity;
};
```

Use a form-domain constant different from annotation-token construction, e.g.:

```c
#define EXTRACTPDF_FORM_REF_DOMAIN UINT64_C(0x464f524d5f524546)
```

Form token rules:

```c
out_ref->opaque[0] = edit->session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN;
out_ref->opaque[1] = ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
```

Derive `tag` from `session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN ^ (slot + 1)` through the same mix function family, never the annotation domain.

- [ ] **Step 8: Implement snapshot/ref public functions and a minimal setter shell**

`src/pdf_edit_forms.c` must:

```c
extractpdf_status extractpdf_pdf_edit_form_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_form **out_form)
{
    if (out_form == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    *out_form = NULL;
    if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return extractpdf_pdf_form_snapshot_from_pdf(
        edit->ctx, edit->document, out_form);
}
```

For `field_ref_at()`: zero output first, build current form with provenance, bounds-check `field_index`, register/reuse `provenance->fields[field_index].group_head`, drop model/provenance, and return the stable token.

For this task only, `extractpdf_pdf_edit_form_set_values()` may return `EXTRACTPDF_ERROR_UNSUPPORTED` after null argument checks; Tasks 4-8 replace that shell incrementally. Do not add semantic mutation yet.

- [ ] **Step 9: Dispose retained form identities**

In `extractpdf_dispose_pdf_edit()`:

```c
for (index = 0; index < edit->form_entry_count; ++index)
    pdf_drop_obj(edit->ctx, edit->form_entries[index].group_head);
free(edit->form_entries);
```

Keep annotation registry disposal unchanged.

- [ ] **Step 10: Build and run identity/discovery tests**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Expected: compile succeeds; snapshot/ref tests pass; no successful setter is asserted yet.

- [ ] **Step 11: Commit**

```bash
git add include/extractpdf/extractpdf.h CMakeLists.txt \
  src/pdf_form_common.h src/pdf_form_common.c src/pdf_form_widgets.c src/pdf_form.c \
  src/pdf_edit_internal.h src/pdf_edit.c src/pdf_edit_forms.c \
  tests/test_pdf_form_mutation.c
git commit -m "feat: add editor AcroForm snapshots and field refs"
```

---

### Task 4: MuPDF Widget-wrapper page-load safety gate — mandatory STOP checkpoint

**Files:**
- Create: `src/pdf_edit_form_widgets.c`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_edit_internal.h`
- Modify: `tests/pdf_edit_test_api.h`
- Modify: `tests/pdf_edit_fault_hook.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces private `extractpdf_pdf_edit_form_widget_handles` preparation/drop helpers.
- Produces test-only fault `EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE`.
- Does **not** yet perform a successful semantic mutation.

- [ ] **Step 1: Add the byte-preserving safety test**

Add an output-copy helper and this test shape:

```c
static void copy_editor_output(
    extractpdf_pdf_edit *edit,
    unsigned char **out_data,
    size_t *out_size)
{
    extractpdf_output *out = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    *out_data = NULL;
    *out_size = 0;
    CHECK(extractpdf_pdf_edit_snapshot(edit, &out) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(out, &data, &size) == EXTRACTPDF_OK);
    *out_data = malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
    extractpdf_drop_output(out);
}

static void test_widget_prepare_is_byte_preserving(void)
{
    /* Open FORM_MUTATION_BASIC_PDF, discover the Widget-backed Text ref. */
    /* Copy editor bytes to before. */
    /* Set FORM_AFTER_WIDGET_PREPARE fault. */
    /* Call set_values() with a valid changed Text value. */
    /* Expect EXTRACTPDF_ERROR_MUPDF. */
    /* Copy editor bytes to after. */
    /* Assert same size + memcmp == 0. */
}
```

The fixture must include a stable pre-existing Text AP marker and at least one unrelated Widget on the same page so a page-wide resynthesis would be observable.

- [ ] **Step 2: Add the test-only fault ID**

In `tests/pdf_edit_test_api.h` and matching internal test enum:

```c
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE = 4
```

Keep numeric values 1-3 unchanged for Annotation Mutation tests.

- [ ] **Step 3: Implement exact Widget-wrapper preparation by raw provenance identity**

Private structures:

```c
typedef struct extractpdf_pdf_edit_form_widget_handle {
    pdf_page *page;   /* owned by handle set */
    pdf_annot *widget;/* borrowed from page */
    int previous_editing;
} extractpdf_pdf_edit_form_widget_handle;

typedef struct extractpdf_pdf_edit_form_widget_handles {
    extractpdf_pdf_edit_form_widget_handle *items;
    size_t count;
} extractpdf_pdf_edit_form_widget_handles;
```

Preparation algorithm:

```text
for each unique page_index in live_field.widgets:
    pdf_load_page(edit->ctx, edit->document, page_index) exactly once
    scan pdf_first_widget()/pdf_next_widget()
    match raw Widget object by extractpdf_pdf_form_same_identity()
    retain page until the whole public setter returns
require every provenance Widget to resolve exactly once
```

Do not call `pdf_update_page()` explicitly. Do not begin a journal operation in preparation.

- [ ] **Step 4: Wire only the injected pre-operation path into the setter shell**

For a syntactically valid Text update on a field with Widgets:

```c
status = extractpdf_pdf_edit_form_prepare_widget_handles(
    edit, live_field, &handles);
if (status != EXTRACTPDF_OK)
    return status;

#if defined(EXTRACTPDF_TESTING)
if (edit->test_fault ==
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE) {
    edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
    extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
    return EXTRACTPDF_ERROR_MUPDF;
}
#endif

extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
return EXTRACTPDF_ERROR_UNSUPPORTED; /* successful mutation comes later */
```

The normal non-fault path still being unsupported is intentional for this gate only.

- [ ] **Step 5: Run the safety test and STOP on any byte change**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required result: the injected failure returns with byte-identical editor output.

**STOP condition:** if preparing Widget wrappers changes PDF bytes, an unrelated field, an AP marker, or any sentinel on pinned MuPDF 1.28.2, stop implementation here and return to the design. Do not attempt to hide the mutation with extra rollback, do not call private MuPDF internals, and do not weaken the no-execution contract.

- [ ] **Step 6: Commit only if the gate passes**

```bash
git add CMakeLists.txt src/pdf_edit_form_widgets.c src/pdf_edit_internal.h \
  tests/pdf_edit_test_api.h tests/pdf_edit_fault_hook.c tests/test_pdf_form_mutation.c
git commit -m "test: prove safe form Widget preparation"
```

---

### Task 5: Typed validation, mutation preflight, Text semantics, group ownership, and no-op

**Files:**
- Create: `src/pdf_edit_form_values.c`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces private `extractpdf_pdf_edit_form_assignment`.
- Produces validation/no-op/canonical-write helpers used by Button and Choice tasks.
- Makes `set_values()` fully support zero-Widget Text fields and all mutation document/error-preflight rules.

- [ ] **Step 1: Add failing Text/preflight/error-precedence tests**

Add helpers that construct UTF8 and MISSING updates:

```c
static extractpdf_form_value_update text_update(
    extractpdf_form_value_input *value,
    const char *text,
    size_t size)
{
    extractpdf_form_value_update update = {0};
    memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value->option_index = SIZE_MAX;
    value->utf8 = text;
    value->utf8_size = size;
    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = value;
    update.value_count = 1;
    return update;
}

static extractpdf_form_value_update missing_update(void)
{
    extractpdf_form_value_update update = {0};
    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_MISSING;
    return update;
}
```

Lock these assertions before implementation:

```text
zero-Widget Text missing -> "alpha" -> MISSING
present-empty distinct from MISSING
valid UTF-8 round-trips
invalid UTF-8 -> ARGUMENT
embedded NUL -> ARGUMENT
too-small update/value structs -> ARGUMENT
NOT_APPLICABLE input -> ARGUMENT
XFA -> UNSUPPORTED
NeedAppearances true -> UNSUPPORTED
malformed NeedAppearances -> FORMAT
RichText/FileSelect -> UNSUPPORTED
PushButton/Signature/UNKNOWN -> UNSUPPORTED
ReadOnly + valid Text command -> STATE
ReadOnly + structurally invalid Text command -> ARGUMENT
Required and NoExport do not block mutation
same-group descendant /V overrides removed after success
external inherited /V: PRESENT override works; MISSING -> UNSUPPORTED
sibling value remains unchanged
same Text value is byte-identical no-op
```

Use `extractpdf_pdf_edit_form_snapshot()` and reopen through `extractpdf_pdf_edit_snapshot()` + `extractpdf_document_form()` as the primary oracle.

- [ ] **Step 2: Define the normalized private assignment**

In `src/pdf_edit_internal.h`:

```c
typedef struct extractpdf_pdf_edit_form_assignment {
    extractpdf_form_value_presence presence;
    extractpdf_form_value_kind kind;
    char *utf8;
    size_t utf8_size;
    size_t *option_indices;
    size_t value_count;
} extractpdf_pdf_edit_form_assignment;
```

Provide drop/reset helpers that free only ExtractPDF-owned copies.

- [ ] **Step 3: Implement generic public-input validation exactly once**

`src/pdf_edit_form_values.c` must validate in this order:

```text
update pointer and minimum struct_size
presence is MISSING or PRESENT
value_count/pointer relationship
each nested minimum struct_size
known value kind
UTF8 pointer/non-NUL/strict UTF-8/option_index == SIZE_MAX
OPTION has utf8 == NULL, utf8_size == 0
field-specific option-index bounds/cardinality/type rules
unsupported field mode
ReadOnly
external inherited missing boundary
```

Copy UTF-8 bytes and OPTION index arrays before journal mutation.

- [ ] **Step 4: Implement strict mutation-document preflight before no-op detection**

Read raw AcroForm dictionary keys without invoking form runtime:

```c
/* XFA */
if (extractpdf_pdf_dict_find(ctx, acroform, PDF_NAME(XFA), &obj))
    return EXTRACTPDF_ERROR_UNSUPPORTED;

/* NeedAppearances */
if (extractpdf_pdf_dict_find(
        ctx, acroform, PDF_NAME(NeedAppearances), &obj)) {
    if (!pdf_is_bool(ctx, obj))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_to_bool(ctx, obj))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

Do not silently coerce integer/string values to Boolean.

- [ ] **Step 5: Resolve a ref against the current strict provenance**

Never mutate directly through the registry's retained pointer without validating current structure. Each setter must:

```text
validate token -> registry entry
build current model + provenance strictly
find exactly one current public field whose group_head identity matches registry entry
use that current model/provenance for all validation and writes
```

If the current field cannot be found despite a valid token, return `EXTRACTPDF_ERROR_STATE` and publish no mutation.

- [ ] **Step 6: Implement semantic equality for no-op**

For Text:

```c
if (requested.presence == EXTRACTPDF_FORM_VALUE_MISSING)
    equal = field->value_presence == EXTRACTPDF_FORM_VALUE_MISSING;
else
    equal = field->value_presence == EXTRACTPDF_FORM_VALUE_PRESENT &&
        field->value_count == 1 &&
        model->values[field->first_value].kind == EXTRACTPDF_FORM_VALUE_UTF8 &&
        model->values[field->first_value].utf8.size == assignment->utf8_size &&
        memcmp(model_string, assignment->utf8, assignment->utf8_size) == 0;
```

Do not inspect/repair `/AS` or `/AP` during no-op comparison.

- [ ] **Step 7: Implement group-local ownership helpers**

Private helpers:

```c
int extractpdf_pdf_edit_form_live_contains_node(
    fz_context *ctx,
    const extractpdf_pdf_form_live_field *live,
    pdf_obj *object);

void extractpdf_pdf_edit_form_delete_group_key(
    fz_context *ctx,
    const extractpdf_pdf_form_live_field *live,
    pdf_obj *key,
    int keep_group_head);
```

For successful Text PRESENT:

```c
pdf_dict_put_text_string(ctx, live->group_head, PDF_NAME(V), assignment->utf8);
for each same-group descendant other than group_head:
    pdf_dict_del(ctx, node, PDF_NAME(V));
```

For MISSING, first reject external `effective_v_owner`; then delete `/V` from every same-group node including head.

- [ ] **Step 8: Make zero-Widget Text mutation one journal operation**

`extractpdf_pdf_edit_form_set_values()` now coordinates:

```c
pdf_begin_operation(edit->ctx, edit->document,
    "ExtractPDF set form value");
operation_open = 1;

extractpdf_pdf_edit_form_write_semantic(...);

pdf_end_operation(edit->ctx, edit->document);
operation_open = 0;
```

Catch path:

```c
if (operation_open)
    pdf_abandon_operation(edit->ctx, edit->document);
```

Do not dirty unrelated fields and do not set `document->recalculate`.

- [ ] **Step 9: Run Text/group/preflight tests plus old suites**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R 'extractpdf\.(pdf_annotation_mutation|pdf_form|pdf_form_mutation)$' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt src/pdf_edit_internal.h src/pdf_edit_forms.c \
  src/pdf_edit_form_values.c tests/test_pdf_form_mutation.c
git commit -m "feat: add atomic Text form value mutation"
```

---

### Task 6: Checkbox and Radio value mutation with `/AP` preservation

**Files:**
- Modify: `src/pdf_edit_form_values.c`
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Consumes: normalized assignment + current provenance from Task 5.
- Produces: button OPTION/Off/Missing semantics and target raw Widget `/AS` updates inside the same outer journal operation.

- [ ] **Step 1: Add failing button tests**

Lock:

```text
Checkbox /Off -> OPTION(0)
Checkbox OPTION(0) -> explicit Off
Checkbox Off -> Missing, and Missing != Off after snapshot/reopen
Radio /One -> /Two
repeated /One Widgets all select together when OPTION(One) selected
NoToggleToOff still permits explicit Off and Missing
RadiosInUnison does not alter the API's option-index semantics
/AP marker bytes "CHECK-AP-KEEP" and "RADIO-AP-KEEP" survive unchanged
```

Use public snapshot/reopen for semantic assertions and deterministic marker search in `extractpdf_output_data()` only for the supplementary `/AP` preservation proof.

- [ ] **Step 2: Extend assignment validation for button fields**

Rules:

```text
MISSING -> value_count must be 0
PRESENT/0 -> explicit Off
PRESENT/1 -> kind must be OPTION and index < option_count
PRESENT/>1 -> ARGUMENT
UTF8 -> ARGUMENT
```

Resolve the selected private button state from:

```c
model->options[field->first_option + option_index].button_state
```

Create the PDF Name inside the journal operation with `pdf_new_name()`; never expose or accept the raw Name publicly.

- [ ] **Step 3: Write canonical button `/V`**

```text
MISSING   -> delete same-group /V keys
Off       -> group-head /V /Off, delete descendant local /V
OPTION(i) -> group-head /V /<private-state>, delete descendant local /V
```

External inherited `/V` MISSING restriction remains identical to Text.

- [ ] **Step 4: Update every raw Widget `/AS` without loading pages**

For each `live->widgets[]`:

```c
pdf_obj *widget = live->widgets[i].object;
const char *widget_state = /* strict private state established by model */;

if (selected_state != NULL &&
    widget_state != NULL &&
    strcmp(selected_state, widget_state) == 0)
    pdf_dict_put_name(ctx, widget, PDF_NAME(AS), selected_state);
else
    pdf_dict_put(ctx, widget, PDF_NAME(AS), PDF_NAME(Off));
```

Use the same normalized private button-state mapping produced during strict Widget reconciliation; do not call `pdf_button_field_on_state()` because it invents `/Yes` when no on-state exists.

Do not touch `/AP` and do not request resynthesis.

- [ ] **Step 5: Run button tests**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Expected: semantic round-trip passes and AP markers are unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_edit_form_values.c src/pdf_edit_form_widgets.c \
  src/pdf_edit_forms.c tests/test_pdf_form_mutation.c
git commit -m "feat: add checkbox and radio value mutation"
```

---

### Task 7: Combo and List semantic mutation with exact `/I`

**Files:**
- Modify: `src/pdf_edit_form_values.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces Choice semantic writes for zero-Widget and Widget-backed fields; target appearance refresh is still completed in Task 8.

- [ ] **Step 1: Add failing Choice tests**

Lock exact cases:

```text
non-edit Combo OPTION(0/1)
editable Combo OPTION
editable Combo UTF8 custom and UTF8 empty
non-edit Combo UTF8 -> ARGUMENT
duplicate export Combo OPTION(1) remains OPTION(1) after reopen via /I [1]
single List OPTION
single List MISSING
single List PRESENT/0 -> ARGUMENT
multi List PRESENT/0 -> explicit /V [] + /I []
multi List one/many options
multi duplicate indices -> ARGUMENT
multi caller order [2,0] round-trips as [2,0]
```

- [ ] **Step 2: Extend normalized assignment validation for Choice**

```text
Combo non-edit: PRESENT exactly one OPTION
Combo edit: PRESENT exactly one OPTION or one UTF8
single List: PRESENT exactly one OPTION
multi List: PRESENT zero or more OPTION values, each unique
MISSING: zero values for all Choice fields
```

Validate every option index against the current model before unsupported/ReadOnly checks.

- [ ] **Step 3: Implement exact Choice representation writes**

Single OPTION:

```c
pdf_dict_put_text_string(ctx, live->group_head, PDF_NAME(V), export_text);
pdf_obj *indices = pdf_new_array(ctx, edit->document, 1);
pdf_array_push_int(ctx, indices, (int64_t)option_index);
pdf_dict_put_drop(ctx, live->group_head, PDF_NAME(I), indices);
```

Editable custom UTF8:

```c
pdf_dict_put_text_string(ctx, live->group_head, PDF_NAME(V), assignment->utf8);
pdf_dict_del(ctx, live->group_head, PDF_NAME(I));
```

Multi List:

```c
pdf_obj *values = pdf_new_array(ctx, edit->document, assignment->value_count);
pdf_obj *indices = pdf_new_array(ctx, edit->document, assignment->value_count);
for each caller-ordered option index:
    pdf_array_push_text_string(ctx, values, export_text);
    pdf_array_push_int(ctx, indices, (int64_t)option_index);
pdf_dict_put_drop(ctx, live->group_head, PDF_NAME(V), values);
pdf_dict_put_drop(ctx, live->group_head, PDF_NAME(I), indices);
```

For explicit empty multi-selection, create and store two empty arrays. For MISSING, delete same-group `/V` and `/I` after external-provider check.

Always delete descendant same-group local `/V` and `/I` overrides after a successful canonical write.

- [ ] **Step 4: Extend semantic no-op comparison to Choice**

OPTION selections compare normalized option indices, not export strings. Editable custom UTF8 compares bytes. Multi-list compares exact count **and exact caller order**.

- [ ] **Step 5: Run Choice tests**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/pdf_edit_form_values.c src/pdf_edit_forms.c tests/test_pdf_form_mutation.c
git commit -m "feat: add Combo and List value mutation"
```

---

### Task 8: Text/Combo/List target Widget appearance refresh with no form-runtime execution

**Files:**
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Consumes: Task 4 proven-safe wrapper preparation and Tasks 5/7 semantic writes.
- Produces: targeted Widget AP refresh inside the same public setter journal operation.

- [ ] **Step 1: Add failing Widget/AP/no-execution tests**

For Text and Combo Widget-backed fields:

```text
snapshot/render target region before mutation
snapshot unrelated Widget region before mutation
mutate target
snapshot/render target region after reopen
assert target bitmap bytes differ
assert unrelated field value unchanged
assert unrelated Widget region bytes unchanged
assert event sentinel fields remain unchanged
assert historical output object captured before mutation remains byte-identical
```

For List:

```text
setter succeeds
/V + /I round-trip exactly
Widget resynthesis completes and reopened PDF remains valid
no pixel-level selected-row highlight assertion
```

For `FORM_MUTATION_EVENTS_PDF`, mutate the target Text and assert Keystroke/Validate/Format/Calculate/activation/`/CO` sentinel values remain exactly their fixture values.

- [ ] **Step 2: Prepare all target Widget wrappers before opening the journal operation**

Use Task 4 helper. If any expected raw Widget cannot be resolved exactly once, return `STATE` before mutation.

No successful mutation may begin until all wrappers for the field are ready.

- [ ] **Step 3: Add exception-safe editing-state handling**

For each target Widget:

```c
item->previous_editing = pdf_get_widget_editing_state(edit->ctx, item->widget);
pdf_set_widget_editing_state(edit->ctx, item->widget, 1);
```

Restore in cleanup regardless of success/failure:

```c
pdf_set_widget_editing_state(
    edit->ctx, item->widget, item->previous_editing);
```

The editor already has JavaScript globally disabled; editing state is the additional target-local guarantee that appearance synthesis uses raw value semantics and does not run Format behavior.

- [ ] **Step 4: Refresh only target Widgets after semantic write**

Inside the same outer journal operation created by the public setter:

```c
pdf_annot_request_resynthesis(edit->ctx, item->widget);
(void)pdf_update_widget(edit->ctx, item->widget);
```

Do not call `pdf_update_page()` or `pdf_update_open_pages()`.

After every target Widget is refreshed successfully, end the outer operation. On any exception, abandon the outer operation before dropping prepared pages.

- [ ] **Step 5: Prove no hidden second-pass dependency**

After a successful setter:

```text
call extractpdf_pdf_edit_snapshot twice
both snapshots must parse via extractpdf_document_form after reopen
second snapshot must not require page loading/update to become semantically correct
unrelated field/AP sentinel remains unchanged
```

This catches residual `doc->recalculate` or deferred page-wide update dependencies.

- [ ] **Step 6: Run mutation + rendering/no-execution tests**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R 'extractpdf\.(render|pdf_form|pdf_form_mutation)$' --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/pdf_edit_form_widgets.c src/pdf_edit_forms.c tests/test_pdf_form_mutation.c
git commit -m "feat: refresh mutated form Widget appearances"
```

---

### Task 9: Fault-injected journal rollback, ref stability, and editor reuse

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `tests/pdf_edit_test_api.h`
- Modify: `tests/pdf_edit_fault_hook.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Adds three final form-specific fault points without changing public production ABI.

- [ ] **Step 1: Add exact fault IDs without renumbering existing IDs**

Use:

```c
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE = 4,
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE = 5,
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE = 6,
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH = 7
```

Mirror these in the internal `EXTRACTPDF_PDF_EDIT_TEST_FAULT_*` enum.

- [ ] **Step 2: Add a reusable rollback assertion helper**

Test shape:

```c
static void expect_failed_mutation_atomic(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update,
    extractpdf_test_pdf_edit_fault fault)
{
    unsigned char *before = NULL, *after = NULL;
    size_t before_size = 0, after_size = 0;
    extractpdf_form *form = NULL;

    copy_editor_output(edit, &before, &before_size);
    extractpdf_test_pdf_edit_set_fault(edit, fault);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, ref, update) ==
        EXTRACTPDF_ERROR_MUPDF);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size);
    CHECK(memcmp(before, after, before_size) == 0);

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    extractpdf_drop_form(form);
    free(before);
    free(after);
}
```

After every injected failure, immediately reuse the **same ref** for one valid mutation and assert success.

- [ ] **Step 3: Inject after semantic write**

Immediately after canonical `/V`/`/I` and descendant-override deletion, before Widget work:

```c
#if defined(EXTRACTPDF_TESTING)
if (edit->test_fault ==
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_SEMANTIC_WRITE) {
    edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
    fz_throw(edit->ctx, FZ_ERROR_GENERIC,
        "injected form failure after semantic write");
}
#endif
```

Required: catch executes `pdf_abandon_operation()` and output bytes equal pre-call.

- [ ] **Step 4: Inject after first button `/AS` update**

After the first raw Widget state write, throw inside the same operation. Required: field `/V` and every `/AS` revert.

- [ ] **Step 5: Inject after first Text/Choice AP refresh**

After one successful `pdf_update_widget()` and before operation end, throw. Required: generated AP/xref changes and semantic `/V`/`/I` all revert.

- [ ] **Step 6: Prove direct-field ref stability across rollback**

Use `FORM_MUTATION_DIRECT_FIELD_PDF`: discover the ref, inject `FORM_AFTER_SEMANTIC_WRITE`, assert rollback bytes equal, then use the same ref for a valid mutation and assert the reopened semantic value changed. This is the mandatory proof that retained direct group-head identity remains usable inside one editor session.

If this direct-field stability test fails because MuPDF journal rollback replaces direct-object identity, stop and revise the private ref locator design before proceeding; do not special-case the test away.

- [ ] **Step 7: Run atomicity tests under static and sanitizer builds**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure

cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel 2
ctest --test-dir build-asan -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add src/pdf_edit_internal.h src/pdf_edit_forms.c src/pdf_edit_form_widgets.c \
  tests/pdf_edit_test_api.h tests/pdf_edit_fault_hook.c tests/test_pdf_form_mutation.c
git commit -m "test: lock atomic form mutation rollback"
```

---

### Task 10: Complete public contract matrix and Linux 21/21 feature GREEN

**Files:**
- Modify: `tests/test_pdf_form_mutation.c`
- Modify only if a failing contract requires it: `src/pdf_edit_forms.c`, `src/pdf_edit_form_values.c`, `src/pdf_edit_form_widgets.c`, `src/pdf_form_*`

**Interfaces:**
- Produces: complete V1 functional test surface and one frozen Linux feature candidate.

- [ ] **Step 1: Add final API/reset/error-domain assertions**

Lock all of these explicitly:

```text
form_snapshot(NULL, &form) -> ARGUMENT and form == NULL
form_snapshot(edit, NULL) -> ARGUMENT
field_ref_at bad index -> ARGUMENT and zero ref
field_ref_at NULL output -> ARGUMENT
forged form ref -> ARGUMENT
form ref from another editor -> ARGUMENT
bitwise annotation ref used as form ref -> ARGUMENT
old form ref used after editor drop/reopen -> ARGUMENT
unknown value kind -> ARGUMENT
OPTION with non-NULL utf8 or nonzero utf8_size -> ARGUMENT
UTF8 with option_index != SIZE_MAX -> ARGUMENT
out-of-range option -> ARGUMENT
multi duplicate option -> ARGUMENT
larger struct_size accepted; trailing bytes untouched/ignored
```

- [ ] **Step 2: Add final no-op coverage for every supported value family**

Required byte-identical no-op tests:

```text
same Text value
same Checkbox/Radio state
same Combo OPTION
same editable Combo custom UTF8
same single List OPTION
same multi-list exact ordered sequence
```

For each: copy editor output before and after, assert equal size and `memcmp == 0`.

- [ ] **Step 3: Add ref-survival integration assertions**

One field ref must remain usable across:

```text
multiple editor form snapshots
multiple editor PDF output snapshots
annotation create/update/delete on another page/object
another field's form value mutation
```

Do not compare refs across editor sessions.

- [ ] **Step 4: Run the complete strict static suite**

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Required: **100% / 21 tests passed**.

- [ ] **Step 5: Run the complete sanitizer suite**

```bash
rm -rf build-asan
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

Required: **100% / 21 tests passed** under ASan/UBSan.

- [ ] **Step 6: Commit only contract-driven fixes and freeze a candidate SHA**

```bash
git add include/extractpdf/extractpdf.h CMakeLists.txt src tests
git commit -m "feat: complete AcroForm value mutation V1"
git push origin feat/acroform-value-mutation
```

Do not add opportunistic refactors after the first 21/21 static+sanitizer candidate unless a review or platform proof exposes a real defect.

---

### Task 11: Exact-head same-SHA full CI, scope review, evidence, and STOP

**Files:**
- No code changes expected.
- Metadata only: draft PR, issue #46, roadmap #2.

**Interfaces:**
- Consumes: frozen feature SHA from Task 10.
- Produces: same-SHA Linux/macOS/Windows evidence and merge-readiness decision; does **not** merge.

- [ ] **Step 1: Verify the draft PR head is exactly the frozen feature SHA**

Fresh-read PR metadata and branch ref. If the branch advanced unexpectedly, do not force or merge; investigate the new commits first.

- [ ] **Step 2: Require normal PR Linux proof on the frozen SHA**

The PR workflow must show:

```text
Linux strict static: 21/21
Linux ASan/UBSan:    21/21
```

Do not accept a workflow on a merge-base-only or stale source SHA.

- [ ] **Step 3: Apply `full-ci` to the same SHA**

The workflow is already configured so labeling a PR `full-ci` enables macOS and Windows without modifying `.github/workflows/ci.yml`.

Required same-SHA result:

```text
Linux static + sanitizer 21/21
macOS 21/21
Windows DLL 21/21
```

Windows logs must explicitly show both `extractpdf.dll` and `extractpdf_test_pdf_form_mutation.exe` built and `extractpdf.pdf_form_mutation` executed.

- [ ] **Step 4: Perform the final base-to-head scope review**

Compare exactly:

```text
base: fdcb2f6cd489de34802d09989ab61a1af8cd1861
head: <frozen feature SHA>
```

Allowed paths are limited to:

```text
docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md
docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation.md
include/extractpdf/extractpdf.h
CMakeLists.txt
src/pdf_form_common.[ch]
src/pdf_form_widgets.c
src/pdf_form.c
src/pdf_edit_internal.h
src/pdf_edit.c
src/pdf_edit_forms.c
src/pdf_edit_form_values.c
src/pdf_edit_form_widgets.c
tests/CMakeLists.txt
tests/test_pdf_form.c
tests/test_pdf_form_mutation.c
tests/pdf_edit_test_api.h
tests/pdf_edit_fault_hook.c
tests/fixtures/acroform-mutation-*.pdf
```

Reject unrelated workflow, render, text, image, link, outline, metadata, composition, or public mutation-surface changes.

- [ ] **Step 5: Fresh Critical/Important review against the spec**

Review specifically:

```text
ref domain separation and wrong-session handling
raw observation never loading pages
provenance ownership/drop correctness
direct-field ref stability proof
error precedence
external inherited /V Missing boundary
same-group override removal
button AP preservation
choice /I exactness and duplicate export handling
page-load safety gate evidence
editing-state restoration on exceptions
no pdf_update_page/recalculate path
one outer operation + abandon on every failure
byte-identical no-op
previous outputs/snapshots remain immutable
```

No Critical or Important blocker may remain.

- [ ] **Step 6: Record evidence and STOP**

Comment on draft PR, #46, and roadmap #2 with:

```text
strict compile RED SHA/workflow
raw-observation regression proof
page-load safety gate proof
final feature SHA
Linux static 21/21
Linux ASan/UBSan 21/21
same-SHA full-ci Linux/macOS/Windows 21/21
final changed-path count/scope review
review result
```

Keep #46 open and PR draft/open. **STOP here.** Do not ready, merge, close #46, or start a follow-up Forms slice without explicit user integration authorization.

---

### Task 12: Integration only after explicit authorization

**Files:**
- No planned source changes.
- GitHub state/evidence only.

**Interfaces:**
- Consumes: frozen proven feature SHA and explicit user authorization.
- Produces: integrated master proof and completed #46.

- [ ] **Step 1: Fresh-read the integration gate**

Confirm immediately before merge:

```text
PR still targets master
source head == frozen proven feature SHA
all required exact-head workflows successful
no new review blocker
master has not moved incompatibly
```

If master moved, rebase/merge only through a new proven SHA; never claim the old proof covers changed content.

- [ ] **Step 2: Mark ready and merge with expected-head protection**

Use the canonical PR when possible. If the known connector draft->ready GraphQL `fullDatabaseId` bug recurs, create an exact-SHA non-draft carrier PR whose head points to the already-proven feature commit and contains **no new commits/content**, then merge that carrier with an expected-head guard. Record the workaround in the canonical PR and #46.

- [ ] **Step 3: Require integrated-master push proof on the exact merge commit**

The push workflow must run on the new `master` merge SHA and pass:

```text
Linux strict static 21/21
Linux ASan/UBSan 21/21
macOS 21/21
Windows DLL 21/21
```

Do not close #46 on feature-branch proof alone.

- [ ] **Step 4: Close the slice only after master proof**

After integrated-master success:

```text
close #46 as completed
update roadmap #2: Form Value Mutation V1 integrated
record feature SHA, merge SHA, feature full-ci, and master-push workflow
leave all deferred form-structure/signature/XFA/flattening work unchecked
```

Do not create or implement another Forms mutation slice unless separately requested.
