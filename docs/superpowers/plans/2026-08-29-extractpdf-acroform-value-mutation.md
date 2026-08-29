# ExtractPDF AcroForm Value Mutation V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, session-local, journal-atomic AcroForm value mutation to the existing `extractpdf_pdf_edit` layer without making `extractpdf_form` mutable or executing PDF form runtime behavior.

**Architecture:** Reuse the strict AcroForm parser as the single semantic authority, refactor Widget reconciliation onto raw page objects so observation never enters MuPDF page/annotation runtime, and add an optional live provenance sidecar for editor-only identity. A separate form-ref registry resolves one logical terminal field group; a typed assignment engine owns `/V`/`/I`; a focused Widget layer owns button `/AS` and target-only Text/Choice appearance refresh. Checkbox/Radio preserve `/AP`. Text/Combo/List may load MuPDF Widget wrappers only after a mandatory zero-byte-change safety gate on pinned MuPDF 1.28.2.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, GitHub Actions Linux/macOS/Windows.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md`

## Global Constraints

- Integrated base is exactly `fdcb2f6cd489de34802d09989ab61a1af8cd1861`; implementation branch is `feat/acroform-value-mutation`.
- Pinned PDF engine is MuPDF **1.28.2**. Do not change vcpkg pins, overlay ports, or `.github/workflows/ci.yml` to make the feature pass.
- Existing suite starts at **20 CTests**. This slice adds exactly one new public test target, `extractpdf.pdf_form_mutation`, for **21 total**.
- First implementation checkpoint is strict compile RED: the library and existing targets #1-#20 build; only new target #21 fails because the approved mutation ABI does not exist yet.
- `extractpdf_form` remains immutable, deep-owned, document-independent, and free of MuPDF pointers.
- Mutation exists only on `extractpdf_pdf_edit`; do not introduce a second mutable form/document handle.
- Public mutation identity is `extractpdf_form_field_ref`. Snapshot indices, field names, Widget indices, PDF object numbers, and option strings are never public mutation identity.
- Form refs use a token domain distinct from annotation refs and are valid only for one editor session.
- `extractpdf_document_form()`, `extractpdf_pdf_edit_form_snapshot()`, `extractpdf_pdf_edit_form_field_ref_at()`, and strict form parsing/reconciliation must not call `pdf_load_page()`, `fz_load_page()`, `pdf_update_page()`, `pdf_update_open_pages()`, or form event/runtime APIs.
- Raw page reconciliation uses `pdf_lookup_page_obj()`, raw `/Annots`, and `pdf_page_obj_transform()` exactly as locked by the spec.
- Value mutation V1 supports Text, Checkbox, Radio, Combo, and List. PushButton, Signature, UNKNOWN, Text RichText, and Text FileSelect are `EXTRACTPDF_ERROR_UNSUPPORTED` after command validation.
- Mutation-only preflight: `/XFA` present -> `UNSUPPORTED`; `/NeedAppearances true` -> `UNSUPPORTED`; non-Boolean `/NeedAppearances` -> `FORMAT`. Observation and ref discovery remain allowed.
- Never execute Keystroke, Validate, Format, Calculate, Widget activation, SubmitForm, ResetForm, JavaScript, `/CO`, or page-wide recalculation.
- Never call `pdf_set_field_value`, `pdf_set_annot_field_value`, `pdf_set_text_field_value`, `pdf_set_choice_field_value`, `pdf_choice_widget_set_value`, `pdf_toggle_widget`, `pdf_calculate_form`, or `pdf_reset_form`.
- Semantic no-op occurs only after ref validation, strict parse, mutation preflight, assignment validation, unsupported-mode checks, ReadOnly check, and external-inheritance check. No-op returns `OK` without a journal operation or byte change.
- Every successful non-noop setter is exactly one outer `pdf_begin_operation()` / `pdf_end_operation()` pair. Any failure after begin must execute `pdf_abandon_operation()`.
- Checkbox/Radio update canonical `/V` plus every target Widget `/AS` and preserve existing `/AP` objects/streams.
- Text/Combo/List appearance refresh may use `pdf_load_page()` only in the mutation Widget-wrapper layer and only after Task 4 proves preparatory page loading is byte-preserving on the locked deterministic fixture. If Task 4 fails, **STOP and return to design**.
- Target Text/Combo/List Widgets must run resynthesis with Widget editing state enabled, then restore the previous state in exception-safe cleanup. Do not use page-wide update as a shortcut.
- Multi-list caller order is preserved. Duplicate option indices are `ARGUMENT`. Single choice OPTION writes `/I [index]`; multi choice writes `/I` matching selected indices exactly.
- Missing and present-empty remain distinct. Checkbox/Radio `PRESENT + 0` is explicit Off; `MISSING + 0` removes group-local `/V` and sets target Widgets visually Off.
- If effective `/V` is inherited from outside the target logical group, MISSING is `UNSUPPORTED`; PRESENT may override it at the group head without touching siblings.
- V1 does not mutate `/DV`, `/Ff`, `/Opt`, `/T`, `/TU`, `/MaxLen`, field structure, Widget structure/geometry, signatures, XFA, or NeedAppearances.
- Final frozen feature SHA must pass Linux static 21/21, Linux ASan/UBSan 21/21, macOS 21/21, and Windows DLL 21/21 on the same SHA.
- Keep the PR draft/open through Task 11. Merge only after explicit user authorization, then require integrated-master push proof before closing #46.

## File Structure

**Create**

- `src/pdf_edit_forms.c` — public editor form snapshot/ref APIs, form-ref registry, current-field resolution, mutation preflight, outer setter transaction coordinator.
- `src/pdf_edit_form_values.c` — update validation, owned normalized assignment, semantic equality/no-op, canonical `/V`/`/I` writes.
- `src/pdf_edit_form_widgets.c` — Widget-wrapper preparation/drop, Checkbox/Radio `/AS`, Text/Combo/List target-only appearance refresh.
- `tests/test_pdf_form_mutation.c` — all mutation/identity/round-trip/atomicity/no-execution tests.
- `tests/fixtures/acroform-mutation-basic.pdf` — Text + Checkbox + Radio fields, zero-Widget and multi-Widget cases.
- `tests/fixtures/acroform-mutation-choice.pdf` — editable/non-editable Combo and single/multi List, duplicate export case.
- `tests/fixtures/acroform-mutation-groups.pdf` — same-group `/V`/`/I` overrides, external inherited `/V`, sibling field.
- `tests/fixtures/acroform-mutation-modes.pdf` — ReadOnly/Required/NoExport/RichText/FileSelect/PushButton/Signature/UNKNOWN.
- `tests/fixtures/acroform-mutation-events.pdf` — Keystroke/Validate/Format/Calculate/activation/`/CO` sentinels and unrelated Widget.
- `tests/fixtures/acroform-mutation-need-appearances.pdf` — valid form, `/NeedAppearances true`, stable AP marker.
- `tests/fixtures/acroform-mutation-xfa.pdf` — valid form plus present `/XFA`.
- `tests/fixtures/acroform-mutation-bad-need-appearances.pdf` — non-Boolean `/NeedAppearances`.
- `tests/fixtures/acroform-mutation-direct-field.pdf` — direct logical field dictionary for ref/rollback stability.

**Modify**

- `include/extractpdf/extractpdf.h` — approved ref/value-update ABI and exactly three editor form APIs.
- `CMakeLists.txt` — add the three focused production files.
- `src/pdf_form_common.h` — provenance/build interfaces and shared identity helper.
- `src/pdf_form_common.c` — optional live provenance generated from the validated field graph.
- `src/pdf_form_widgets.c` — raw page-object reconciliation plus optional Widget provenance capture.
- `src/pdf_form.c` — reusable private PDF-to-`extractpdf_form` snapshot builder.
- `src/pdf_edit_internal.h` — form registry, private assignment/Widget interfaces, test fault IDs.
- `src/pdf_edit.c` — dispose retained form registry objects.
- `tests/CMakeLists.txt` — register #21 and fixture paths; add NeedAppearances fixture to read-only form target.
- `tests/test_pdf_form.c` — raw-observation regression only.
- `tests/pdf_edit_test_api.h` — form mutation fault IDs.
- `tests/pdf_edit_fault_hook.c` — existing hook routes new IDs.

---

### Task 1: Strict compile RED and draft PR

**Files:**
- Create: `tests/test_pdf_form_mutation.c`
- Create: all `tests/fixtures/acroform-mutation-*.pdf` files listed above
- Modify: `tests/CMakeLists.txt`
- Do not modify: `include/`, `src/`, root `CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: existing `extractpdf_pdf_edit`, immutable `extractpdf_form`, current 20-test suite.
- Produces: one new target referencing the final approved ABI before that ABI exists.

- [ ] **Step 1: Write the compile-surface RED**

Create `tests/test_pdf_form_mutation.c` with:

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

- [ ] **Step 2: Register exactly one new CTest**

Append:

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

- [ ] **Step 3: Create deterministic fixtures with these exact semantic records**

```text
basic
  field[0] Text: /T zero, no Widget, no /V
  field[1] Text: /T textWidget, /V (old-text), one Widget, AP stream contains literal TEXT-OLD-AP
  field[2] Checkbox: /T check, two Widgets on different pages, both AP/N << /Off ... /Yes ... >>, /V /Off
  field[3] Radio: /T radio, three Widgets, on-state order /One, /Two, /One, /V /One

choice
  field[0] non-edit Combo: Opt A,B; /V B; /I [1]
  field[1] editable Combo: Opt Tokyo,Osaka; /V Kyoto; no /I
  field[2] single List: Opt S,M,L; /V M; /I [1]
  field[3] multi List: Opt Red,Green,Blue; /V [Red Blue]; /I [0 2]
  field[4] non-edit Combo: Opt [[dup First] [dup Second]]; /V dup; /I [1]

groups
  logical group g with unnamed descendants whose local /V values equal group-head /V
  logical Choice group c with descendant local /I equal group-head /I
  unnamed ancestor carries /V shared; named child target and named child sibling both inherit it

modes
  fields: ReadOnly Text, Required Text, NoExport Text, RichText Text, FileSelect Text,
          PushButton, unsigned Signature, valid unrecognized /FT UNKNOWN

events
  target Text Widget: /AA/K, /AA/V, /AA/F and activation JS/action sentinels
  unrelated Text field /V UNCHANGED with stable AP marker UNRELATED-AP-KEEP
  calculated field /V CALC-SENTINEL included in AcroForm /CO
  target Widget has stable existing AP marker TARGET-AP-BEFORE

need-appearances
  ordinary Text + Widget, /NeedAppearances true, AP marker NEED-AP-KEEP

xfa
  ordinary Text + Widget, present /XFA

bad-need-appearances
  ordinary Text + Widget, /NeedAppearances (yes) as PDF string

direct-field
  /Fields [ << /FT /Tx /T (direct) /V (before) >> ]
```

Use classic deterministic xref/trailer formatting consistent with existing hand-authored fixtures. Do not generate dates, random IDs, object streams, or compression.

- [ ] **Step 4: Run the strict RED**

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
```

Required evidence:

```text
library builds
existing executable targets #1-#20 build
new extractpdf_test_pdf_form_mutation fails compilation
compiler errors name extractpdf_form_field_ref / value input/update / three absent functions
```

Any old-target failure is not the intended RED.

- [ ] **Step 5: Commit, push, and create the draft PR**

```bash
git add tests/test_pdf_form_mutation.c tests/CMakeLists.txt tests/fixtures/acroform-mutation-*.pdf
git commit -m "test: lock AcroForm value mutation ABI RED"
git push -u origin feat/acroform-value-mutation
```

Create a draft PR targeting `master`, title `feat: add atomic AcroForm value mutation`, body linking #46 and both design/plan paths. Record the returned PR number, exact RED source SHA, and workflow run in that draft PR and issue #46. Keep it draft through Task 11.

---

### Task 2: Make strict form observation raw-page only

**Files:**
- Modify: `tests/test_pdf_form.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/pdf_form_widgets.c`

**Interfaces:**
- Consumes: current immutable `extractpdf_document_form()`.
- Produces: Widget reconciliation via raw page dictionaries and page-object transform only.

- [ ] **Step 1: Add the NeedAppearances read-only regression**

Add this fixture define to the existing `extractpdf_test_pdf_form` target:

```cmake
ACROFORM_NEED_APPEARANCES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mutation-need-appearances.pdf"
```

Add:

```c
static extractpdf_output *snapshot_fresh_document(const char *path, int observe_form)
{
    extractpdf_document *d = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_output *out = NULL;
    extractpdf_form *form = NULL;

    CHECK(extractpdf_open(path, NULL, &d) == EXTRACTPDF_OK);
    if (observe_form) {
        CHECK(extractpdf_document_form(d, &form) == EXTRACTPDF_OK);
        CHECK(form != NULL);
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
    const unsigned char *a = NULL;
    const unsigned char *b = NULL;
    size_t na = 0;
    size_t nb = 0;

    CHECK(extractpdf_output_data(before, &a, &na) == EXTRACTPDF_OK);
    CHECK(extractpdf_output_data(after, &b, &nb) == EXTRACTPDF_OK);
    CHECK(na == nb);
    CHECK(memcmp(a, b, na) == 0);
    extractpdf_drop_output(before);
    extractpdf_drop_output(after);
}
```

- [ ] **Step 2: Record the forbidden-call RED in source**

Before the refactor:

```bash
grep -n 'pdf_load_page' src/pdf_form_widgets.c
```

Required pre-change result: at least one match inside `extractpdf_pdf_form_reconcile_widgets()`.

Build only the old form target while #21 is intentionally compile-RED:

```bash
cmake --build build --target extractpdf_test_pdf_form --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form$' --output-on-failure
```

The byte test is retained whether the old implementation visibly changes this fixture or not; the source-level forbidden-call check is the deterministic boundary required by the spec.

- [ ] **Step 3: Replace loaded-page reconciliation with raw page objects**

The page loop must have this complete control shape:

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
        extractpdf_expected_widget *match;
        extractpdf_pdf_form_widget_internal widget;
        pdf_obj *p = NULL;
        char *state = NULL;

        if (!pdf_is_dict(ctx, obj) || !is_widget(ctx, obj))
            continue;
        if (!pdf_is_indirect(ctx, obj)) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        match = find_expected(
            expected, expected_count, pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
        if (match == NULL || match->seen != 0) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }
        if (extractpdf_pdf_dict_find(ctx, obj, PDF_NAME(P), &p) &&
            !pdf_is_null(ctx, p) &&
            pdf_objcmp_resolve(ctx, p, page_obj) != 0) {
            status = EXTRACTPDF_ERROR_FORMAT;
            break;
        }

        memset(&widget, 0, sizeof(widget));
        widget.field_index = match->field_index;
        widget.page_index = page_index;
        widget.button_option_index = SIZE_MAX;

        status = extractpdf_pdf_read_rect_strict(
            ctx, obj, PDF_NAME(Rect), page_ctm, &widget.bounds);
        if (status == EXTRACTPDF_OK)
            status = extractpdf_pdf_read_u32_default(
                ctx, obj, PDF_NAME(F), 0, &widget.flags);
        if (status == EXTRACTPDF_OK)
            status = read_button_state(
                ctx, obj, model->fields[match->field_index].type, &state);
        if (status == EXTRACTPDF_OK)
            status = append_widget(model, &widget, state, &states, &state_capacity);
        free(state);
        if (status == EXTRACTPDF_OK)
            ++match->seen;
    }
}
```

Preserve the existing post-loop `expected[i].seen == 1` validation and button-option materialization.

- [ ] **Step 4: Prove the forbidden page-load path is gone and old semantics stay green**

```bash
! grep -n 'pdf_load_page' src/pdf_form_widgets.c
cmake --build build --target \
  extractpdf_test_pdf_annotations \
  extractpdf_test_pdf_annotation_mutation \
  extractpdf_test_pdf_form --parallel 2
ctest --test-dir build -R 'extractpdf\.(pdf_annotations|pdf_annotation_mutation|pdf_form)$' --output-on-failure
```

Required: grep has no match and all selected tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/pdf_form_widgets.c tests/test_pdf_form.c tests/CMakeLists.txt
git commit -m "refactor: make form observation raw-page only"
```

---

### Task 3: Add public ABI, reusable editor snapshots, live provenance, and form refs

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
- Public output: approved types and three editor form functions.
- Private output: `extractpdf_pdf_form_provenance`, reusable PDF-to-form builder, form-ref register/resolve functions.

- [ ] **Step 1: Add failing editor snapshot/ref tests first**

Add:

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

Also assert:

```c
CHECK(extractpdf_pdf_edit_form_snapshot(NULL, &a) == EXTRACTPDF_ERROR_ARGUMENT);
CHECK(a == NULL);
memset(&r0, 0xA5, sizeof(r0));
CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, SIZE_MAX, &r0) ==
    EXTRACTPDF_ERROR_ARGUMENT);
CHECK(r0.opaque[0] == 0 && r0.opaque[1] == 0);
```

Run the new target and record its current absent-ABI/link failure before production edits.

- [ ] **Step 2: Add the exact public ABI**

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

Do not add editor-specific live getters, Widget refs, option refs, reset, or clear APIs.

- [ ] **Step 3: Define optional live provenance**

In `src/pdf_form_common.h`:

```c
typedef struct extractpdf_pdf_form_live_widget {
    pdf_obj *object;
    int page_index;
} extractpdf_pdf_form_live_widget;

typedef struct extractpdf_pdf_form_live_field {
    pdf_obj *group_head;
    pdf_obj **group_nodes;
    size_t group_node_count;
    pdf_obj *effective_v_owner;
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

extractpdf_status extractpdf_pdf_form_snapshot_from_pdf(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_form **out_form);
```

Make the existing indirect-num/gen-or-direct-pointer identity helper non-static; do not introduce a second identity relation.

- [ ] **Step 4: Populate provenance from the validated graph before transient state is freed**

For each public terminal group:

```c
live->group_head = pdf_keep_obj(
    state->ctx, state->nodes[group->head_node].object);

for (node_index = 0; node_index < state->node_count; ++node_index) {
    if (state->nodes[node_index].group_index != group_index)
        continue;
    live->group_nodes[live->group_node_count++] = pdf_keep_obj(
        state->ctx, state->nodes[node_index].object);
}

effective = extractpdf_pdf_form_effective_value(
    state, group->head_node, PDF_NAME(V));
if (effective.present) {
    live->effective_v_present = 1;
    live->effective_v_owner = pdf_keep_obj(
        state->ctx, state->nodes[effective.owner_node].object);
}
```

Allocate `group_nodes` to the exact validated node count for that group. On any allocation/keep failure, drop the whole unpublished provenance and return the mapped error.

- [ ] **Step 5: Capture Widget provenance only after each Widget passes strict reconciliation**

When the public `widget.field_index` is known and the Widget has passed indirect identity, page uniqueness, `/P`, Rect, and F validation:

```c
extractpdf_pdf_form_live_field *live =
    &provenance->fields[widget.field_index];
extractpdf_pdf_form_live_widget *grown = realloc(
    live->widgets,
    (live->widget_count + 1) * sizeof(*live->widgets));
if (grown == NULL)
    return EXTRACTPDF_ERROR_NOMEM;
live->widgets = grown;
live->widgets[live->widget_count].object = pdf_keep_obj(ctx, obj);
live->widgets[live->widget_count].page_index = page_index;
++live->widget_count;
```

Because reconciliation scans page order then raw `/Annots`, each field's provenance Widget sequence is the public global Widget sequence filtered to that field.

- [ ] **Step 6: Factor one reusable immutable snapshot builder**

`extractpdf_pdf_form_snapshot_from_pdf()` must:

```text
reset *out_form = NULL
call extractpdf_pdf_form_build(ctx, document, 0, &model, NULL)
allocate extractpdf_form
attach model
publish only after all steps succeed
```

Make `extractpdf_document_form()` validate/obtain `pdf_document *` and delegate to it. `extractpdf_pdf_edit_form_snapshot()` calls the same helper against `edit->document`.

- [ ] **Step 7: Add a separate form-ref registry**

In `src/pdf_edit_internal.h`:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

Add to `extractpdf_pdf_edit`:

```c
extractpdf_pdf_edit_form_entry *form_entries;
size_t form_entry_count;
size_t form_entry_capacity;
```

Use:

```c
#define EXTRACTPDF_FORM_REF_DOMAIN UINT64_C(0x464f524d5f524546)
```

Token encoding:

```c
out_ref->opaque[0] = edit->session_cookie ^ EXTRACTPDF_FORM_REF_DOMAIN;
out_ref->opaque[1] =
    ((uint64_t)entry->tag << 32) | (uint64_t)(slot + 1);
```

Tag derivation must include `EXTRACTPDF_FORM_REF_DOMAIN`, so a bitwise annotation ref fails form-token validation even when its slot number matches.

- [ ] **Step 8: Implement editor snapshot/ref and minimal setter shell**

`extractpdf_pdf_edit_form_snapshot()`:

```c
if (out_form == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
*out_form = NULL;
if (edit == NULL || edit->ctx == NULL || edit->document == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
return extractpdf_pdf_form_snapshot_from_pdf(
    edit->ctx, edit->document, out_form);
```

`field_ref_at()` must zero output, build current form with provenance, bounds-check index, register/reuse `group_head`, then drop model/provenance.

For this task only, the setter is:

```c
extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update)
{
    if (edit == NULL || ref == NULL || update == NULL)
        return EXTRACTPDF_ERROR_ARGUMENT;
    return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

Tasks 4-8 replace this shell; do not implement value writes in Task 3.

- [ ] **Step 9: Dispose retained form entries**

Before dropping `edit->document`:

```c
for (index = 0; index < edit->form_entry_count; ++index)
    pdf_drop_obj(edit->ctx, edit->form_entries[index].group_head);
free(edit->form_entries);
```

- [ ] **Step 10: Run identity/discovery tests**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Expected: compile succeeds and all snapshot/ref assertions pass.

- [ ] **Step 11: Commit**

```bash
git add include/extractpdf/extractpdf.h CMakeLists.txt \
  src/pdf_form_common.h src/pdf_form_common.c src/pdf_form_widgets.c src/pdf_form.c \
  src/pdf_edit_internal.h src/pdf_edit.c src/pdf_edit_forms.c \
  tests/test_pdf_form_mutation.c
git commit -m "feat: add editor AcroForm snapshots and field refs"
```

---

### Task 4: Prove MuPDF Widget-wrapper preparation is byte-preserving

**Files:**
- Create: `src/pdf_edit_form_widgets.c`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_edit_internal.h`
- Modify: `tests/pdf_edit_test_api.h`
- Modify: `tests/pdf_edit_fault_hook.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces private Widget-handle prepare/drop functions.
- Produces fault `FORM_AFTER_WIDGET_PREPARE`.
- Does not yet produce a successful Widget-backed mutation.

- [ ] **Step 1: Add reusable output-copy and field-ref-by-name test helpers**

After Task 3 APIs exist:

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
    CHECK(size != 0);
    *out_data = malloc(size);
    CHECK(*out_data != NULL);
    memcpy(*out_data, data, size);
    *out_size = size;
    extractpdf_drop_output(out);
}

static extractpdf_form_field_ref field_ref_by_name(
    extractpdf_pdf_edit *edit,
    const char *wanted)
{
    extractpdf_form *form = NULL;
    extractpdf_form_field_ref ref = {{0, 0}};
    size_t count = 0;
    size_t i;

    CHECK(extractpdf_pdf_edit_form_snapshot(edit, &form) == EXTRACTPDF_OK);
    CHECK(extractpdf_form_field_count(form, &count) == EXTRACTPDF_OK);
    for (i = 0; i < count; ++i) {
        const char *name = NULL;
        size_t size = 0;
        CHECK(extractpdf_form_field_name(form, i, &name, &size) == EXTRACTPDF_OK);
        if (name != NULL && size == strlen(wanted) &&
            memcmp(name, wanted, size) == 0) {
            CHECK(extractpdf_pdf_edit_form_field_ref_at(edit, i, &ref) == EXTRACTPDF_OK);
            extractpdf_drop_form(form);
            return ref;
        }
    }
    extractpdf_drop_form(form);
    CHECK(0);
    return ref;
}
```

- [ ] **Step 2: Add the safety-gate test before the helper implementation**

```c
static void test_widget_prepare_is_byte_preserving(void)
{
    extractpdf_document *d = NULL;
    extractpdf_pdf_edit *edit = NULL;
    extractpdf_form_field_ref ref;
    extractpdf_form_value_input value = {0};
    extractpdf_form_value_update update = {0};
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;

    CHECK(extractpdf_open(FORM_MUTATION_BASIC_PDF, NULL, &d) == EXTRACTPDF_OK);
    CHECK(extractpdf_pdf_edit_begin(d, &edit) == EXTRACTPDF_OK);
    extractpdf_close(d);
    ref = field_ref_by_name(edit, "textWidget");

    value.struct_size = sizeof(value);
    value.kind = EXTRACTPDF_FORM_VALUE_UTF8;
    value.option_index = SIZE_MAX;
    value.utf8 = "new-text";
    value.utf8_size = 8;
    update.struct_size = sizeof(update);
    update.presence = EXTRACTPDF_FORM_VALUE_PRESENT;
    update.values = &value;
    update.value_count = 1;

    copy_editor_output(edit, &before, &before_size);
    extractpdf_test_pdf_edit_set_fault(
        edit,
        EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE);
    CHECK(extractpdf_pdf_edit_form_set_values(edit, &ref, &update) ==
        EXTRACTPDF_ERROR_MUPDF);
    copy_editor_output(edit, &after, &after_size);
    CHECK(before_size == after_size);
    CHECK(memcmp(before, after, before_size) == 0);

    free(before);
    free(after);
    extractpdf_drop_pdf_edit(edit);
}
```

- [ ] **Step 3: Add fault ID 4 without renumbering existing Annotation Mutation IDs**

Test API:

```c
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_WIDGET_PREPARE = 4
```

Internal enum:

```c
EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE = 4
```

- [ ] **Step 4: Implement exact Widget-wrapper preparation**

Private structures:

```c
typedef struct extractpdf_pdf_edit_form_widget_handle {
    pdf_page *page;
    pdf_annot *widget;
    int previous_editing;
} extractpdf_pdf_edit_form_widget_handle;

typedef struct extractpdf_pdf_edit_form_widget_handles {
    extractpdf_pdf_edit_form_widget_handle *items;
    size_t count;
} extractpdf_pdf_edit_form_widget_handles;
```

Preparation must group target provenance Widgets by page index so each target page is loaded exactly once. For one page:

```c
page = pdf_load_page(edit->ctx, edit->document, page_index);
for (widget = pdf_first_widget(edit->ctx, page);
     widget != NULL;
     widget = pdf_next_widget(edit->ctx, widget)) {
    pdf_obj *candidate = pdf_annot_obj(edit->ctx, widget);
    for (target = 0; target < live->widget_count; ++target) {
        if (live->widgets[target].page_index != page_index)
            continue;
        if (!extractpdf_pdf_form_same_identity(
                edit->ctx, candidate, live->widgets[target].object))
            continue;
        /* Store this borrowed widget with the owned page exactly once. */
    }
}
```

Require every target Widget to resolve exactly once. Drop every owned page in `extractpdf_pdf_edit_form_drop_widget_handles()`.

- [ ] **Step 5: Wire only the injected preparatory path**

The setter shell must validate the form ref, build current model/provenance, locate the current field, and for a syntactically valid Widget-backed Text update call prepare. Immediately after successful preparation:

```c
#if defined(EXTRACTPDF_TESTING)
if (edit->test_fault ==
    EXTRACTPDF_PDF_EDIT_TEST_FAULT_FORM_AFTER_WIDGET_PREPARE) {
    edit->test_fault = EXTRACTPDF_PDF_EDIT_TEST_FAULT_NONE;
    extractpdf_pdf_edit_form_drop_widget_handles(edit, &handles);
    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return EXTRACTPDF_ERROR_MUPDF;
}
#endif
```

Without the injected fault, return `EXTRACTPDF_ERROR_UNSUPPORTED` in Task 4; successful mutation begins in Task 5.

- [ ] **Step 6: Run the mandatory STOP gate**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required: injected failure returns with byte-identical output.

**STOP:** any byte change, sentinel change, or unrelated AP change during Widget preparation invalidates the approved design for pinned MuPDF 1.28.2. Do not hide it with an extra outer journal or private MuPDF struct construction.

- [ ] **Step 7: Commit only if the gate passes**

```bash
git add CMakeLists.txt src/pdf_edit_form_widgets.c src/pdf_edit_internal.h \
  src/pdf_edit_forms.c tests/pdf_edit_test_api.h tests/pdf_edit_fault_hook.c \
  tests/test_pdf_form_mutation.c
git commit -m "test: prove safe form Widget preparation"
```

---

### Task 5: Add typed validation, mutation preflight, Text semantics, group ownership, and no-op

**Files:**
- Create: `src/pdf_edit_form_values.c`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces `extractpdf_pdf_edit_form_assignment` plus validate/equal/write helpers.
- Makes `set_values()` fully support Text, including zero-Widget and Widget-backed Text; Widget AP refresh itself remains Task 8, so Task 5 success cases use zero-Widget Text only.

- [ ] **Step 1: Add failing Text, preflight, inheritance, and error-precedence tests**

Add exact assertions for:

```text
zero-Widget Text MISSING -> alpha -> MISSING
empty text distinct from MISSING
valid UTF-8 round-trip
invalid UTF-8 -> ARGUMENT
embedded NUL -> ARGUMENT
NOT_APPLICABLE input -> ARGUMENT
too-small outer/nested struct -> ARGUMENT
XFA -> UNSUPPORTED
NeedAppearances true -> UNSUPPORTED
non-Boolean NeedAppearances -> FORMAT
RichText/FileSelect -> UNSUPPORTED
PushButton/Signature/UNKNOWN -> UNSUPPORTED
ReadOnly + valid Text update -> STATE
ReadOnly + invalid cardinality -> ARGUMENT
Required and NoExport allow valid assignment
same-group descendant /V overrides removed
external inherited /V PRESENT override works
external inherited /V MISSING -> UNSUPPORTED
sibling remains unchanged
same Text value -> byte-identical no-op
```

Primary oracle for every success: editor form snapshot, `pdf_edit_snapshot()`, reopen, `extractpdf_document_form()`.

- [ ] **Step 2: Define the owned normalized assignment**

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

`extractpdf_pdf_edit_form_assignment_drop()` frees `utf8` and `option_indices` and zeroes the struct.

- [ ] **Step 3: Implement strict generic input validation**

Minimum sizes:

```c
const size_t value_min =
    offsetof(extractpdf_form_value_input, utf8_size) + sizeof(value->utf8_size);
const size_t update_min =
    offsetof(extractpdf_form_value_update, value_count) + sizeof(update->value_count);
```

Validation sequence:

```text
pointer/minimum size
presence is exactly MISSING or PRESENT
value_count/pointer relationship
nested minimum size
kind is UTF8 or OPTION
UTF8: non-NULL pointer, strict UTF-8, no embedded NUL, option_index == SIZE_MAX
OPTION: utf8 == NULL, utf8_size == 0
field-specific cardinality and option-index range
unsupported field mode
ReadOnly
external inherited MISSING boundary
```

Copy UTF8 bytes and OPTION indices before entering journal mutation.

- [ ] **Step 4: Implement mutation-only raw AcroForm preflight**

```c
pdf_obj *root = pdf_dict_get(edit->ctx,
    pdf_trailer(edit->ctx, edit->document), PDF_NAME(Root));
pdf_obj *acroform = pdf_dict_get(edit->ctx, root, PDF_NAME(AcroForm));
pdf_obj *obj = NULL;

if (pdf_is_dict(edit->ctx, acroform) &&
    extractpdf_pdf_dict_find(edit->ctx, acroform, PDF_NAME(XFA), &obj))
    return EXTRACTPDF_ERROR_UNSUPPORTED;

if (pdf_is_dict(edit->ctx, acroform) &&
    extractpdf_pdf_dict_find(
        edit->ctx, acroform, PDF_NAME(NeedAppearances), &obj)) {
    if (!pdf_is_bool(edit->ctx, obj))
        return EXTRACTPDF_ERROR_FORMAT;
    if (pdf_to_bool(edit->ctx, obj))
        return EXTRACTPDF_ERROR_UNSUPPORTED;
}
```

Run this after strict parse/ref resolution and before no-op detection.

- [ ] **Step 5: Resolve a valid ref against current strict provenance**

Each setter call must:

```text
validate form token -> registry entry
extractpdf_pdf_form_build(..., want_provenance=1)
find exactly one current field whose provenance.group_head matches entry.group_head
use current model/provenance only
```

If a structurally valid token no longer maps to a current field, return `STATE` without mutation.

- [ ] **Step 6: Implement Text no-op comparison**

```c
if (assignment->presence == EXTRACTPDF_FORM_VALUE_MISSING) {
    equal = field->value_presence == EXTRACTPDF_FORM_VALUE_MISSING;
} else {
    const extractpdf_pdf_form_value_internal *current =
        &model->values[field->first_value];
    const char *current_text = model->strings + current->utf8.offset;
    equal = field->value_presence == EXTRACTPDF_FORM_VALUE_PRESENT &&
        field->value_count == 1 &&
        current->kind == EXTRACTPDF_FORM_VALUE_UTF8 &&
        current->utf8.size == assignment->utf8_size &&
        memcmp(current_text, assignment->utf8, assignment->utf8_size) == 0;
}
```

No-op returns before Widget preparation and before `pdf_begin_operation()`.

- [ ] **Step 7: Implement group membership and canonical Text writes**

```c
static int live_contains(
    fz_context *ctx,
    const extractpdf_pdf_form_live_field *live,
    pdf_obj *object)
{
    size_t i;
    for (i = 0; i < live->group_node_count; ++i)
        if (extractpdf_pdf_form_same_identity(ctx, live->group_nodes[i], object))
            return 1;
    return 0;
}
```

PRESENT:

```c
pdf_dict_put_text_string(
    edit->ctx, live->group_head, PDF_NAME(V), assignment->utf8);
for (i = 0; i < live->group_node_count; ++i)
    if (!extractpdf_pdf_form_same_identity(
            edit->ctx, live->group_nodes[i], live->group_head))
        pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
```

MISSING:

```c
if (live->effective_v_present &&
    !live_contains(edit->ctx, live, live->effective_v_owner))
    return EXTRACTPDF_ERROR_UNSUPPORTED;
for (i = 0; i < live->group_node_count; ++i)
    pdf_dict_del(edit->ctx, live->group_nodes[i], PDF_NAME(V));
```

Do not mutate `/DV`.

- [ ] **Step 8: Make zero-Widget Text assignment one journal operation**

```c
pdf_begin_operation(edit->ctx, edit->document,
    "ExtractPDF set form value");
operation_open = 1;
extractpdf_pdf_edit_form_write_semantic(...);
pdf_end_operation(edit->ctx, edit->document);
operation_open = 0;
```

Catch:

```c
if (operation_open) {
    pdf_abandon_operation(edit->ctx, edit->document);
    operation_open = 0;
}
```

Do not set `document->recalculate`.

- [ ] **Step 9: Run focused and regression tests**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R 'extractpdf\.(pdf_annotation_mutation|pdf_form|pdf_form_mutation)$' --output-on-failure
```

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt src/pdf_edit_internal.h src/pdf_edit_forms.c \
  src/pdf_edit_form_values.c tests/test_pdf_form_mutation.c
git commit -m "feat: add atomic Text form value mutation"
```

---

### Task 6: Add Checkbox and Radio mutation while preserving `/AP`

**Files:**
- Modify: `src/pdf_edit_form_values.c`
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Consumes: normalized assignment/current model/current live field.
- Produces: button `/V` + `/AS` mutation in the same outer journal operation.

- [ ] **Step 1: Add failing button tests**

Lock:

```text
Checkbox Off -> OPTION(0)
Checkbox OPTION(0) -> explicit Off
Checkbox explicit Off -> Missing; Missing != Off after reopen
Radio /One -> OPTION(/Two)
repeated /One Widgets all enter /One together
NoToggleToOff permits programmatic Off and Missing
RadiosInUnison does not change option-index API semantics
CHECK-AP-KEEP and RADIO-AP-KEEP fixture markers remain present exactly once after mutations
```

- [ ] **Step 2: Validate button assignments**

```text
MISSING requires value_count 0
PRESENT/0 = explicit Off
PRESENT/1 requires OPTION and index < field.option_count
PRESENT/>1 -> ARGUMENT
UTF8 -> ARGUMENT
```

Selected state:

```c
const char *selected_state = NULL;
if (assignment->presence == EXTRACTPDF_FORM_VALUE_PRESENT &&
    assignment->value_count == 1) {
    size_t oi = assignment->option_indices[0];
    selected_state = model->options[field->first_option + oi].button_state;
}
```

- [ ] **Step 3: Write canonical button `/V`**

```text
MISSING   -> delete /V on every same-group node
Off       -> group-head /V /Off; delete descendant /V
OPTION(i) -> group-head /V /private-state; delete descendant /V
```

Use `pdf_dict_put_name()` for selected private state and `pdf_dict_put(..., PDF_NAME(Off))` for Off.

- [ ] **Step 4: Map each provenance Widget to its existing `button_option_index` precisely**

Because provenance Widget order is the model's global Widget order filtered to this field:

```c
size_t live_index = 0;
for (model_widget_index = 0;
     model_widget_index < model->widget_count;
     ++model_widget_index) {
    const extractpdf_pdf_form_widget_internal *mw =
        &model->widgets[model_widget_index];
    size_t widget_option;
    const char *widget_state = NULL;
    pdf_obj *widget_obj;

    if (mw->field_index != field_index)
        continue;
    if (live_index >= live->widget_count)
        return EXTRACTPDF_ERROR_STATE;

    widget_obj = live->widgets[live_index].object;
    widget_option = mw->button_option_index;
    if (widget_option != SIZE_MAX)
        widget_state = model->options[
            field->first_option + widget_option].button_state;

    if (selected_state != NULL &&
        widget_state != NULL &&
        strcmp(selected_state, widget_state) == 0)
        pdf_dict_put_name(edit->ctx, widget_obj, PDF_NAME(AS), widget_state);
    else
        pdf_dict_put(edit->ctx, widget_obj, PDF_NAME(AS), PDF_NAME(Off));

    ++live_index;
}
if (live_index != live->widget_count)
    return EXTRACTPDF_ERROR_STATE;
```

Do not call `pdf_button_field_on_state()`, which can invent `/Yes`. Do not touch `/AP` or request button resynthesis.

- [ ] **Step 5: Run button tests**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/pdf_edit_form_values.c src/pdf_edit_form_widgets.c \
  src/pdf_edit_forms.c tests/test_pdf_form_mutation.c
git commit -m "feat: add checkbox and radio value mutation"
```

---

### Task 7: Add Combo and List semantic mutation with exact `/I`

**Files:**
- Modify: `src/pdf_edit_form_values.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Produces Choice validation/no-op/semantic writes. Widget AP refresh remains Task 8.

- [ ] **Step 1: Add failing Choice tests**

Lock:

```text
non-edit Combo OPTION assignment
editable Combo OPTION assignment
editable Combo UTF8 custom and UTF8 empty
non-edit Combo UTF8 -> ARGUMENT
duplicate export Combo OPTION(1) reopens as OPTION(1) via /I [1]
single List OPTION
single List MISSING
single List PRESENT/0 -> ARGUMENT
multi List PRESENT/0 -> PRESENT with zero values
multi List one and multiple OPTION values
multi duplicate option indices -> ARGUMENT
multi order [2,0] reopens as [2,0]
```

- [ ] **Step 2: Validate Choice assignments**

```text
Combo non-edit: PRESENT exactly one OPTION
Combo edit: PRESENT exactly one OPTION or exactly one UTF8
single List: PRESENT exactly one OPTION
multi List: PRESENT zero or more unique OPTION values
MISSING: zero values for all Choice fields
```

Every OPTION index is checked against current `field->option_count` before unsupported/ReadOnly checks.

- [ ] **Step 3: Write exact Choice representation**

Single OPTION:

```c
pdf_dict_put_text_string(
    edit->ctx, live->group_head, PDF_NAME(V), export_text);
pdf_obj *indices = pdf_new_array(edit->ctx, edit->document, 1);
pdf_array_push_int(edit->ctx, indices, (int64_t)option_index);
pdf_dict_put_drop(edit->ctx, live->group_head, PDF_NAME(I), indices);
```

Editable custom:

```c
pdf_dict_put_text_string(
    edit->ctx, live->group_head, PDF_NAME(V), assignment->utf8);
pdf_dict_del(edit->ctx, live->group_head, PDF_NAME(I));
```

Multi List:

```c
pdf_obj *values = pdf_new_array(
    edit->ctx, edit->document, (int)assignment->value_count);
pdf_obj *indices = pdf_new_array(
    edit->ctx, edit->document, (int)assignment->value_count);
for (i = 0; i < assignment->value_count; ++i) {
    size_t oi = assignment->option_indices[i];
    const extractpdf_pdf_form_option_internal *option =
        &model->options[field->first_option + oi];
    const char *export_text = model->strings + option->export_text.offset;
    pdf_array_push_text_string(edit->ctx, values, export_text);
    pdf_array_push_int(edit->ctx, indices, (int64_t)oi);
}
pdf_dict_put_drop(edit->ctx, live->group_head, PDF_NAME(V), values);
pdf_dict_put_drop(edit->ctx, live->group_head, PDF_NAME(I), indices);
```

For explicit empty multi-selection, write two empty arrays. For MISSING, apply external `/V` provider rule, then delete same-group `/V` and `/I`.

After any successful Choice write, delete descendant same-group local `/V` and `/I` overrides.

- [ ] **Step 4: Compare Choice no-op by normalized option identity**

OPTION values compare option indices, not export strings. Editable custom compares UTF8 bytes. Multi-list compares count and exact ordered option-index sequence.

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

### Task 8: Refresh Text/Combo/List target Widget appearances without form-runtime execution

**Files:**
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `src/pdf_edit_forms.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Consumes: Task 4 proven-safe wrapper preparation; Tasks 5/7 semantic writes.
- Produces: target-only AP resynthesis inside the same public setter operation.

- [ ] **Step 1: Add failing appearance/no-execution tests**

For Widget-backed Text and Combo, use public rendering to avoid depending on MuPDF private AP object structure:

```text
render a fixed clip around target Widget before mutation
render the same clip after mutation/reopen; bytes must differ
render a fixed clip around unrelated Widget before/after; bytes must match
unrelated field value stays UNCHANGED
CALC-SENTINEL stays unchanged
all event/action sentinel values stay unchanged
historical extractpdf_output captured before mutation stays byte-identical
```

For List:

```text
setter succeeds
/V + /I round-trip exactly
reopened PDF renders/loads successfully
no selected-row pixel-highlight assertion
```

- [ ] **Step 2: Prepare every target Widget before journal begin**

Call Task 4 prepare helper after no-op detection and before `pdf_begin_operation()`. Any missing or duplicate wrapper -> `STATE` with no mutation.

- [ ] **Step 3: Enable editing state before each target resynthesis**

```c
for (i = 0; i < handles.count; ++i) {
    handles.items[i].previous_editing = pdf_get_widget_editing_state(
        edit->ctx, handles.items[i].widget);
    pdf_set_widget_editing_state(edit->ctx, handles.items[i].widget, 1);
}
```

Cleanup always restores:

```c
for (i = 0; i < handles.count; ++i)
    pdf_set_widget_editing_state(
        edit->ctx,
        handles.items[i].widget,
        handles.items[i].previous_editing);
```

- [ ] **Step 4: Refresh target Widgets after semantic write, inside the same outer operation**

```c
for (i = 0; i < handles.count; ++i) {
    pdf_annot_request_resynthesis(edit->ctx, handles.items[i].widget);
    (void)pdf_update_widget(edit->ctx, handles.items[i].widget);
}
```

Never call `pdf_update_page()` or `pdf_update_open_pages()`.

Outer transaction order is exactly:

```text
prepare wrappers
begin operation
write semantic /V,/I
refresh each target Widget
end operation
restore editing states
release loaded pages
```

Exception order is:

```text
abandon outer operation if open
restore editing states
release loaded pages
return mapped error
```

- [ ] **Step 5: Prove no hidden deferred page-wide pass is required**

After one successful setter, call `extractpdf_pdf_edit_snapshot()` twice. Reopen both outputs and assert identical normalized field values. No extra page load/update call is allowed between setter and snapshots.

- [ ] **Step 6: Run render + form regression tests**

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

### Task 9: Lock fault-injected rollback, ref stability, and editor reuse

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `src/pdf_edit_form_widgets.c`
- Modify: `tests/pdf_edit_test_api.h`
- Modify: `tests/pdf_edit_fault_hook.c`
- Modify: `tests/test_pdf_form_mutation.c`

**Interfaces:**
- Adds test-only fault IDs 5-7; production public ABI is unchanged.

- [ ] **Step 1: Add exact fault IDs**

Keep existing IDs 1-4 unchanged:

```c
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE = 5,
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE = 6,
EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH = 7
```

Mirror internal names with `EXTRACTPDF_PDF_EDIT_TEST_FAULT_...`.

- [ ] **Step 2: Add a byte-level rollback helper**

```c
static void expect_failed_mutation_atomic(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update,
    extractpdf_test_pdf_edit_fault fault)
{
    unsigned char *before = NULL;
    unsigned char *after = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
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

Every fault test then immediately reuses the same `ref` for one valid assignment and asserts success.

- [ ] **Step 3: Inject after semantic write**

Immediately after canonical `/V`/`/I` plus descendant override deletion:

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

- [ ] **Step 4: Inject after the first button Widget `/AS` write**

Count actual target `/AS` writes inside the operation. After write #1, throw the same way. Required rollback: canonical `/V` and every `/AS` return to pre-call bytes.

- [ ] **Step 5: Inject after the first successful Text/Choice AP refresh**

After first `pdf_update_widget()` and before `pdf_end_operation()`, throw. Required rollback: generated AP/xref changes and semantic `/V`/`/I` all revert.

- [ ] **Step 6: Prove direct-field ref stability across rollback**

On `FORM_MUTATION_DIRECT_FIELD_PDF`:

```text
discover direct field ref
inject AFTER_SEMANTIC_WRITE on value "failed"
assert byte-identical rollback
reuse same ref to assign "after"
editor snapshot and reopened document both read "after"
```

**STOP:** if direct-field ref fails solely because journal rollback replaced the direct object identity, return to the private identity design. Do not reject direct fields or weaken the public ref contract.

- [ ] **Step 7: Run static and sanitizer fault tests**

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

### Task 10: Complete the public contract matrix and establish Linux 21/21 GREEN

**Files:**
- Modify: `tests/test_pdf_form_mutation.c`
- Modify only when a new assertion finds a real defect: the form/edit files already listed in this plan

**Interfaces:**
- Produces: complete V1 tests and one frozen Linux feature candidate.

- [ ] **Step 1: Add final API/reset/error-domain assertions**

Lock exactly:

```text
form_snapshot(NULL,&out) -> ARGUMENT and out NULL
form_snapshot(edit,NULL) -> ARGUMENT
field_ref_at invalid index -> ARGUMENT and zero ref
field_ref_at NULL output -> ARGUMENT
forged form ref -> ARGUMENT
form ref from another editor -> ARGUMENT
bitwise annotation ref passed as form ref -> ARGUMENT
old form ref used with reopened editor -> ARGUMENT
unknown value kind -> ARGUMENT
OPTION with utf8 != NULL or utf8_size != 0 -> ARGUMENT
UTF8 with option_index != SIZE_MAX -> ARGUMENT
out-of-range option -> ARGUMENT
multi duplicate option -> ARGUMENT
larger struct_size accepted and trailing bytes ignored
```

- [ ] **Step 2: Add byte-identical no-op tests for every supported family**

```text
same Text
same Checkbox state
same Radio state
same Combo OPTION
same editable Combo custom UTF8
same single List OPTION
same multi-list ordered sequence
```

For each: `copy_editor_output()` before/after and require equal size + `memcmp == 0`.

- [ ] **Step 3: Add ref-survival integration assertions**

One form ref must remain valid across:

```text
multiple editor form snapshots
multiple editor output snapshots
annotation create/update/delete on an unrelated annotation
another field's successful form mutation
```

- [ ] **Step 4: Run fresh complete static suite**

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

- [ ] **Step 5: Run fresh complete ASan/UBSan suite**

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

Required: **100% / 21 tests passed**.

- [ ] **Step 6: Commit only contract-driven fixes and freeze the feature candidate**

```bash
git add include/extractpdf/extractpdf.h CMakeLists.txt src tests
git commit -m "feat: complete AcroForm value mutation V1"
git push origin feat/acroform-value-mutation
```

After the first 21/21 static+sanitizer candidate, no opportunistic refactor is allowed unless review/platform proof identifies a real defect.

---

### Task 11: Exact-head full CI, scope/review gate, evidence, and STOP

**Files:**
- No code changes expected
- GitHub metadata only: draft PR, #46, roadmap #2

**Interfaces:**
- Consumes: frozen feature SHA from Task 10.
- Produces: same-SHA cross-platform evidence and merge-readiness decision; does not merge.

- [ ] **Step 1: Fresh-read the draft PR source SHA**

Require PR target `master` and source head exactly equal to the frozen feature SHA. If the branch advanced unexpectedly, investigate before any proof or integration action; never force-reset shared history.

- [ ] **Step 2: Require normal PR Linux proof on that exact source SHA**

```text
Linux static 21/21
Linux ASan/UBSan 21/21
```

Reject stale-source proof.

- [ ] **Step 3: Apply `full-ci` without workflow edits**

The existing workflow enables macOS/Windows when a PR is labeled `full-ci`. Required same-SHA result:

```text
Linux static + sanitizer 21/21
macOS 21/21
Windows DLL 21/21
```

Windows log must show `extractpdf.dll`, `extractpdf_test_pdf_form_mutation.exe`, and `extractpdf.pdf_form_mutation` test 21/21.

- [ ] **Step 4: Review exact base-to-head scope**

Compare:

```text
fdcb2f6cd489de34802d09989ab61a1af8cd1861...<frozen-feature-sha>
```

Allowed paths only:

```text
docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md
docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation.md
include/extractpdf/extractpdf.h
CMakeLists.txt
src/pdf_form_common.h
src/pdf_form_common.c
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

No workflow/render/text/image/link/outline/metadata/composition changes are allowed.

- [ ] **Step 5: Fresh Critical/Important review against the spec**

Explicit review checklist:

```text
form token domain separation
wrong-editor/forged token rejection
raw observation contains no pdf_load_page
provenance drop/ownership correctness
direct-field ref rollback stability
error precedence
external inherited /V MISSING boundary
same-group /V,/I override removal
button /AP preservation and /AS mapping
choice /I exactness and duplicate export identity
Task 4 page-load safety proof
editing-state restoration on every exception
no pdf_update_page/recalculate path
one outer operation + abandon on every post-begin failure
byte-identical no-op
previous outputs/forms remain immutable
```

No Critical or Important blocker may remain.

- [ ] **Step 6: Record evidence and STOP**

Comment on the draft PR, #46, and roadmap #2 with:

```text
strict compile RED SHA/workflow
raw observation source/byte proof
Task 4 Widget preparation safety proof
final feature SHA
Linux static 21/21
Linux ASan/UBSan 21/21
same-SHA full-ci Linux/macOS/Windows 21/21
base-to-head changed-path count
final review result
```

Keep #46 open and PR draft/open. **STOP.** Do not mark ready, merge, close #46, or start another Forms slice until explicit user integration authorization.

---

### Task 12: Integration only after explicit authorization

**Files:**
- No planned source changes
- GitHub state/evidence only

**Interfaces:**
- Consumes: frozen proven feature SHA + explicit user authorization.
- Produces: integrated-master proof and completed #46.

- [ ] **Step 1: Fresh-read the integration gate**

Immediately before integration require:

```text
PR still targets master
source head == frozen proven feature SHA
required exact-head workflows all successful
no new review blocker
master compatibility rechecked
```

If master moved incompatibly, produce and prove a new feature SHA; old proof never covers new content automatically.

- [ ] **Step 2: Ready and merge with an exact-head guard**

Use the canonical PR if draft->ready works. If the known connector GraphQL `fullDatabaseId` failure recurs, create a non-draft exact-SHA carrier branch/PR pointing to the already-proven feature commit with **zero new content commits**, then merge that carrier with expected-head protection. Record the workaround on canonical PR and #46.

- [ ] **Step 3: Require integrated-master push proof on the exact merge commit**

Required:

```text
Linux static 21/21
Linux ASan/UBSan 21/21
macOS 21/21
Windows DLL 21/21
```

Do not close #46 on feature-branch proof alone.

- [ ] **Step 4: Close only after master proof**

After integrated-master success:

```text
close #46 as completed
update roadmap #2: Form Value Mutation V1 integrated
record feature SHA, merge SHA, feature full-ci, master-push workflow
leave deferred structure/signature/XFA/flattening work unchecked
```

Do not create or implement another Forms slice unless separately requested.
