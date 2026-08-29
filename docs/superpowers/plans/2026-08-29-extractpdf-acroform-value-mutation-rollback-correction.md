# ExtractPDF AcroForm Value Mutation #266 Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct workflow #266 by making `extractpdf_form_field_ref` identity independent of journal-sensitive MuPDF object incarnations and proving failed form mutations are byte-identical, ref-stable, and editor-reusable.

**Architecture:** Keep the already-built AcroForm Value Mutation V1 semantics and #264 GREEN implementation. Extend the strict field-tree provenance with a private structural locator made from the `/AcroForm/Fields` ordinal followed by `/Kids` ordinals to the logical group head; copy that locator into the editor form-ref registry, then resolve every public ref against a fresh strict parse instead of retaining `pdf_obj *group_head` as durable identity. MuPDF journal rollback remains the PDF mutation boundary; transient page/Widget objects remain per-call resources and are never durable ref identity.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, GitHub Actions Linux/macOS/Windows.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md`

## Global Constraints

- Corrected-spec gate is commit `2cabcd032702e9b12582df9e3dd68ec0d14a6cc2` on `feat/acroform-value-mutation`.
- Historical feature base remains `fdcb2f6cd489de34802d09989ab61a1af8cd1861`.
- Workflow #264 at `d6fd08e50e48fc448896c551d3d4224c5ed272f1` is the pre-atomicity GREEN checkpoint.
- `904012e256c4cec555a429be00203ac8d7fbaf45` / workflow #265 and `8a8f2a0c5a9e2f78fe10227dad734bc85c40e3c9` / workflow #266 are rollback/ref RED evidence.
- The original plan `docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation.md` remains historical record for Tasks 1-8. This plan supersedes its Task 9+ identity/rollback implementation where the corrected spec differs.
- Continue forward from the current branch head; do not replay, squash, or rewrite historical commits.
- MuPDF stays pinned at **1.28.2**. Do not change vcpkg pins, overlay ports, CMake policy, or `.github/workflows/ci.yml` to make this correction pass.
- Public ABI is unchanged. Do not add/remove/rename any public form type or API.
- `extractpdf_form` remains immutable, deep-owned, document-independent, and free of MuPDF pointers.
- Durable form-ref identity must not contain or derive from a retained direct `pdf_obj *`, `pdf_page *`, `pdf_annot *`, Widget-wrapper address, or any other journal-sensitive MuPDF object incarnation.
- Durable identity is a private structural locator: `/AcroForm/Fields[top]` followed by zero or more `/Kids[child]` ordinals to the logical group head.
- Public `field_index`, field name, Widget index, PDF object num/gen, option text, `/V`, and `/I` are not durable mutation identity.
- Pointer equality and num/gen may remain local witnesses inside one strict parse/reconciliation lifetime only.
- V1 does not mutate `/Fields`, `/Kids`, `/Parent`, `/T`, field topology, or Widget topology; structural-locator stability depends on that explicit scope.
- Every setter re-runs the strict current-form parser and resolves the registry locator to exactly one current terminal field. No name-based rebinding, value matching, post-abandon registry repair, or silent replacement identity is allowed.
- A syntactically valid session ref whose locator cannot resolve after a successful strict parse returns `EXTRACTPDF_ERROR_STATE`.
- Observation/ref discovery remains raw-page only and must not add `pdf_load_page()`, `fz_load_page()`, `pdf_update_page()`, `pdf_update_open_pages()`, or form runtime/event execution.
- Text/Combo/List `pdf_load_page()` remains allowed only in the already-proven pre-mutation Widget-wrapper preparation layer.
- Every successful non-noop setter remains exactly one `pdf_begin_operation()` / `pdf_end_operation()` pair. Every failure after begin executes `pdf_abandon_operation()`.
- Failed setters must yield a new deterministic editor snapshot with exactly the pre-call size and bytes, preserve all previously valid form refs, preserve same-token rediscovery, and leave the editor reusable.
- Do not add application-level repair writes after `pdf_abandon_operation()`. If locator-backed identity plus existing cleanup cannot satisfy the byte/ref oracle, STOP and return to design.
- PR #47 stays draft/open through same-SHA proof. Do not merge or close #46 without explicit integration authorization.
- Final frozen correction SHA must pass Linux static 21/21, Linux ASan/UBSan 21/21, macOS 21/21, and Windows DLL 21/21 on that exact SHA.

## File Structure

**Modify during execution**

- `tests/test_pdf_form_rollback.c` — sharpen #266 RED with same-token rediscovery after each injected failure.
- `src/pdf_form_common.h` — add private locator data to live provenance.
- `src/pdf_form_common.c` — record structural ordinals, materialize locators, free locator storage.
- `src/pdf_edit_internal.h` — replace retained form `group_head` registry identity with owned locator steps/count.
- `src/pdf_edit_forms.c` — register/reuse/resolve refs by locator while mutation continues to use fresh current provenance objects.
- `src/pdf_edit.c` — free locator memory instead of dropping retained form group-head objects.

**Do not modify in this correction**

- `include/extractpdf/extractpdf.h`
- `src/pdf_edit_form_values.c`
- `src/pdf_edit_form_buttons.c`
- `src/pdf_edit_form_choices.c`
- `src/pdf_edit_form_widgets.c`
- `src/pdf_form_widgets.c`
- `.github/workflows/ci.yml`
- vcpkg pins/ports

If a test requires changing one of those files, stop at that exact failure and return to design before widening scope.

---

### Task 1: Sharpen the #266 RED around public ref identity

**Files:**
- Modify: `tests/test_pdf_form_rollback.c`

**Interfaces:**
- Consumes: existing `rollback_field_ref()`, `rollback_expect_failed_atomic()`, fault IDs 5-7.
- Produces: explicit same-token rediscovery before the original ref is reused.

- [ ] **Step 1: Add token equality**

Add after `rollback_field_ref()`:

```c
static void rollback_expect_same_ref(
    const extractpdf_form_field_ref *left,
    const extractpdf_form_field_ref *right)
{
    ROLLBACK_CHECK(left != NULL);
    ROLLBACK_CHECK(right != NULL);
    ROLLBACK_CHECK(memcmp(left, right, sizeof(*left)) == 0);
}
```

- [ ] **Step 2: Pass the field name into the failure helper**

Change:

```c
static void rollback_expect_failed_atomic(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update,
    extractpdf_test_pdf_edit_fault fault)
```

to:

```c
static void rollback_expect_failed_atomic(
    extractpdf_pdf_edit *edit,
    const char *field_name,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update,
    extractpdf_test_pdf_edit_fault fault)
```

- [ ] **Step 3: Rediscover after byte rollback**

Immediately after the existing pre/post output `memcmp` and successful post-failure form snapshot, add:

```c
extractpdf_form_field_ref rediscovered = rollback_field_ref(edit, field_name);
rollback_expect_same_ref(ref, &rediscovered);
```

Keep byte comparison before rediscovery so PDF rollback and registry identity failures remain distinguishable.

- [ ] **Step 4: Update all three calls**

Use exactly:

```c
rollback_expect_failed_atomic(
    edit, "direct", &ref, &update,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_SEMANTIC_WRITE);
```

```c
rollback_expect_failed_atomic(
    edit, "check", &ref, &update,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_WIDGET_STATE);
```

```c
rollback_expect_failed_atomic(
    edit, "target", &ref, &update,
    EXTRACTPDF_TEST_PDF_EDIT_FAULT_FORM_AFTER_FIRST_AP_REFRESH);
```

Keep each subsequent successful setter call through the original `ref`.

- [ ] **Step 5: Prove RED**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required on the pointer-backed registry: compile/link succeeds and the mutation target fails inside rollback/ref identity behavior. If the byte comparison fails before rediscovery, STOP and return to design because that is a separate journal-atomicity defect.

- [ ] **Step 6: Commit test-only RED**

```bash
git add tests/test_pdf_form_rollback.c
git commit -m "test: lock journal-independent form ref identity"
git push origin feat/acroform-value-mutation
```

Record `git rev-parse HEAD` and its workflow run on PR #47. Keep the PR draft.

---

### Task 2: Emit structural locators from strict provenance

**Files:**
- Modify: `src/pdf_form_common.h`
- Modify: `src/pdf_form_common.c`

**Interfaces:**
- Consumes: current strict traversal nodes with `parent_node`, `group_index`, and `depth`.
- Produces: `extractpdf_pdf_form_live_field.locator` for every public terminal field.

- [ ] **Step 1: Add the private locator type**

In `src/pdf_form_common.h`, add before `extractpdf_pdf_form_live_field`:

```c
typedef struct extractpdf_pdf_form_locator {
    size_t *steps;
    size_t step_count;
} extractpdf_pdf_form_locator;
```

Then make it the first member of the live field:

```c
typedef struct extractpdf_pdf_form_live_field {
    extractpdf_pdf_form_locator locator;
    pdf_obj *group_head;
    pdf_obj **group_nodes;
    size_t group_node_count;
    pdf_obj *effective_v_owner;
    int effective_v_present;
    extractpdf_pdf_form_live_widget *widgets;
    size_t widget_count;
} extractpdf_pdf_form_live_field;
```

- [ ] **Step 2: Record each node's structural array ordinal**

Extend the private parse node:

```c
typedef struct extractpdf_pdf_form_node {
    pdf_obj *object;
    size_t parent_node;
    size_t group_index;
    size_t depth;
    size_t tree_index;
    int has_local_t;
    int is_widget;
} extractpdf_pdf_form_node;
```

Change traversal signature to:

```c
static extractpdf_status extractpdf_pdf_form_traverse(
    extractpdf_pdf_form_parse_state *state,
    pdf_obj *object,
    size_t parent_node,
    size_t parent_group,
    size_t depth,
    size_t tree_index,
    int top_level)
```

Publish:

```c
state->nodes[node_index].tree_index = tree_index;
```

Top-level call:

```c
status = extractpdf_pdf_form_traverse(
    state, field, SIZE_MAX, SIZE_MAX, 1, (size_t)index, 1);
```

Child call:

```c
status = extractpdf_pdf_form_traverse(
    state, child, node_index, group_index,
    depth + 1, (size_t)index, 0);
```

- [ ] **Step 3: Materialize the locator from the parent chain**

Add before `extractpdf_pdf_form_materialize_provenance()`:

```c
static extractpdf_status extractpdf_pdf_form_materialize_locator(
    extractpdf_pdf_form_parse_state *state,
    size_t head_node,
    extractpdf_pdf_form_locator *out_locator)
{
    size_t cursor;
    size_t at;
    size_t count;
    size_t *steps;

    if (out_locator == NULL || head_node >= state->node_count)
        return EXTRACTPDF_ERROR_FORMAT;
    out_locator->steps = NULL;
    out_locator->step_count = 0;

    count = state->nodes[head_node].depth;
    if (count == 0)
        return EXTRACTPDF_ERROR_FORMAT;
    if (count > SIZE_MAX / sizeof(*steps))
        return EXTRACTPDF_ERROR_NOMEM;
    steps = (size_t *)malloc(count * sizeof(*steps));
    if (steps == NULL)
        return EXTRACTPDF_ERROR_NOMEM;

    cursor = head_node;
    at = count;
    while (cursor != SIZE_MAX) {
        if (cursor >= state->node_count || at == 0) {
            free(steps);
            return EXTRACTPDF_ERROR_FORMAT;
        }
        steps[--at] = state->nodes[cursor].tree_index;
        cursor = state->nodes[cursor].parent_node;
    }
    if (at != 0) {
        free(steps);
        return EXTRACTPDF_ERROR_FORMAT;
    }

    out_locator->steps = steps;
    out_locator->step_count = count;
    return EXTRACTPDF_OK;
}
```

Top-level nodes already begin at depth `1`, so step count equals the number of `/Fields` + `/Kids` array selections.

- [ ] **Step 4: Attach locator to every public live field**

Inside the terminal-group loop in `extractpdf_pdf_form_materialize_provenance()`, add `extractpdf_status status;` to the loop-local declarations, then immediately after assigning `live` add:

```c
status = extractpdf_pdf_form_materialize_locator(
    state, group->head_node, &live->locator);
if (status != EXTRACTPDF_OK)
    return status;
```

Do not remove fresh `group_head`, `group_nodes`, `effective_v_owner`, or Widget provenance; setters still consume those per-call objects.

- [ ] **Step 5: Free locator storage with provenance**

In `extractpdf_pdf_form_drop_provenance()`, for each field add:

```c
free(field->locator.steps);
field->locator.steps = NULL;
field->locator.step_count = 0;
```

Keep existing object/widget cleanup unchanged.

- [ ] **Step 6: Run parser regression and preserve RED**

```bash
cmake --build build --target extractpdf_test_pdf_form extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form$' --output-on-failure
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required: `extractpdf.pdf_form` passes; mutation remains RED at the pointer-backed registry invariant.

- [ ] **Step 7: Commit provenance unit**

```bash
git add src/pdf_form_common.h src/pdf_form_common.c
git commit -m "refactor: expose structural form provenance"
git push origin feat/acroform-value-mutation
```

Record `git rev-parse HEAD` on PR #47.

---

### Task 3: Make the form-ref registry locator-backed

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `src/pdf_edit.c`

**Interfaces:**
- Consumes: Task 2 `extractpdf_pdf_form_live_field.locator` and fresh current provenance objects.
- Produces: stable refs backed only by structural locator + slot/tag.

- [ ] **Step 1: Replace registry entry layout**

Change to:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    size_t *locator_steps;
    size_t locator_step_count;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

Remove `pdf_obj *group_head`; do not keep both models.

- [ ] **Step 2: Add locator equality**

In `src/pdf_edit_forms.c`:

```c
static int extractpdf_pdf_edit_form_locator_equal(
    const extractpdf_pdf_edit_form_entry *entry,
    const extractpdf_pdf_form_locator *locator)
{
    if (entry == NULL || locator == NULL)
        return 0;
    if (entry->locator_step_count != locator->step_count)
        return 0;
    if (entry->locator_step_count == 0)
        return 1;
    if (entry->locator_steps == NULL || locator->steps == NULL)
        return 0;
    return memcmp(
        entry->locator_steps,
        locator->steps,
        entry->locator_step_count * sizeof(*entry->locator_steps)) == 0;
}
```

- [ ] **Step 3: Register/reuse by locator**

Change register signature to:

```c
static extractpdf_status extractpdf_pdf_edit_form_register(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_live_field *live,
    extractpdf_form_field_ref *out_ref)
```

Validate:

```c
if (live == NULL || live->locator.step_count == 0 ||
    live->locator.steps == NULL)
    return EXTRACTPDF_ERROR_FORMAT;
```

Reuse:

```c
for (slot = 0; slot < edit->form_entry_count; ++slot) {
    if (extractpdf_pdf_edit_form_locator_equal(
            &edit->form_entries[slot], &live->locator)) {
        extractpdf_pdf_edit_form_make_token(edit, slot, out_ref);
        return EXTRACTPDF_OK;
    }
}
```

Copy before publication:

```c
size_t *locator_copy;

if (live->locator.step_count > SIZE_MAX / sizeof(*locator_copy))
    return EXTRACTPDF_ERROR_NOMEM;
locator_copy = (size_t *)malloc(
    live->locator.step_count * sizeof(*locator_copy));
if (locator_copy == NULL)
    return EXTRACTPDF_ERROR_NOMEM;
memcpy(locator_copy, live->locator.steps,
    live->locator.step_count * sizeof(*locator_copy));

status = extractpdf_pdf_edit_form_reserve_entries(
    edit, edit->form_entry_count + 1);
if (status != EXTRACTPDF_OK) {
    free(locator_copy);
    return status;
}

slot = edit->form_entry_count;
edit->form_entries[slot].locator_steps = locator_copy;
edit->form_entries[slot].locator_step_count = live->locator.step_count;
edit->form_entries[slot].tag =
    extractpdf_pdf_edit_form_tag_for_slot(edit, slot);
++edit->form_entry_count;
extractpdf_pdf_edit_form_make_token(edit, slot, out_ref);
return EXTRACTPDF_OK;
```

Do not call `pdf_keep_obj()` in form registration.

- [ ] **Step 4: Register current field locator in `field_ref_at()`**

Use:

```c
status = extractpdf_pdf_edit_form_register(
    edit, &provenance->fields[field_index], out_ref);
```

Keep output zeroing, strict parse, range checks, and cleanup unchanged.

- [ ] **Step 5: Validate token without a retained live object**

In `extractpdf_pdf_edit_form_resolve_ref()` replace `group_head` validity with:

```c
if (entry->tag != tag ||
    entry->locator_step_count == 0 ||
    entry->locator_steps == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
```

- [ ] **Step 6: Move Widget capture after locator resolution and match fresh provenance**

In `extractpdf_pdf_edit_form_find_current_field()` keep the existing `extractpdf_pdf_form_build(..., 1, ...)` call, **remove the current pre-scan call** to `extractpdf_pdf_form_capture_provenance_widgets()`, and replace the `group_head` identity scan with:

```c
for (i = 0; i < provenance->field_count; ++i) {
    if (!extractpdf_pdf_edit_form_locator_equal(
            entry, &provenance->fields[i].locator))
        continue;
    if (match != SIZE_MAX) {
        extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
        extractpdf_pdf_form_drop_model(model);
        return EXTRACTPDF_ERROR_STATE;
    }
    match = i;
}
if (match == SIZE_MAX) {
    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return EXTRACTPDF_ERROR_STATE;
}
```

Then invoke Widget capture exactly once, after the unique locator match:

```c
status = extractpdf_pdf_form_capture_provenance_widgets(
    edit->ctx, edit->document, model, provenance);
if (status != EXTRACTPDF_OK) {
    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return status;
}
```

Finally publish `model`, `provenance`, and `match` as before. Identity resolution is structural; Widget objects are fresh per-call provenance only.

- [ ] **Step 7: Free registry locator memory in editor disposal**

Delete the form-entry `pdf_drop_obj(...group_head)` loop. Add outside the `edit->ctx != NULL` block:

```c
for (index = 0; index < edit->form_entry_count; ++index) {
    free(edit->form_entries[index].locator_steps);
    edit->form_entries[index].locator_steps = NULL;
    edit->form_entries[index].locator_step_count = 0;
}
```

Keep annotation-object and document/context cleanup unchanged.

- [ ] **Step 8: Require #266 fault GREEN**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required for semantic-write, first-button-state, and first-AP-refresh faults: byte-identical rollback, same-token rediscovery, original-ref reuse, and unchanged unrelated/calc sentinel state.

If identity assertions pass but any byte/state assertion still fails, STOP at that exact assertion and return to design. Do not repair the registry or widen into setter/Widget implementation files.

- [ ] **Step 9: Run parser + mutation together**

```bash
ctest --test-dir build -R '^extractpdf\.(pdf_form|pdf_form_mutation)$' --output-on-failure
```

Required: both pass.

- [ ] **Step 10: Commit minimal production GREEN**

```bash
git add src/pdf_edit_internal.h src/pdf_edit_forms.c src/pdf_edit.c
git commit -m "fix: make AcroForm refs journal-independent"
git push origin feat/acroform-value-mutation
```

Task 2 parser files are already committed separately; Task 1 tests are already committed separately.

---

### Task 4: Fresh Linux proof and candidate freeze

**Files:**
- No source changes expected

**Interfaces:**
- Consumes: Task 3 GREEN.
- Produces: frozen correction SHA with fresh static/sanitizer evidence.

- [ ] **Step 1: Fresh static 21/21**

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

Required: **21/21**.

- [ ] **Step 2: Fresh ASan/UBSan 21/21**

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

Required: **21/21**, no sanitizer finding.

- [ ] **Step 3: Audit rejected identity removal**

```bash
git grep -n 'form_entries.*group_head' -- src || true
git grep -n 'form_entries\[.*\]\.group_head' -- src || true
git grep -n 'pdf_keep_obj.*group_head' -- src/pdf_edit_forms.c || true
```

Required: no durable registry storage/reuse by `group_head`. Fresh provenance/setter `group_head` use elsewhere remains valid.

- [ ] **Step 4: Audit raw-observation and runtime boundaries**

```bash
git grep -nE 'pdf_load_page|fz_load_page|pdf_update_page|pdf_update_open_pages' -- \
  src/pdf_form_common.c src/pdf_form_widgets.c src/pdf_edit_forms.c || true
```

Required: no match. The only V1 page load remains in `src/pdf_edit_form_widgets.c` pre-mutation preparation.

```bash
git grep -nE 'pdf_set_field_value|pdf_set_annot_field_value|pdf_set_text_field_value|pdf_set_choice_field_value|pdf_choice_widget_set_value|pdf_toggle_widget|pdf_calculate_form|pdf_reset_form' -- src || true
```

Required: no match.

- [ ] **Step 5: Freeze exact SHA**

```bash
git status --short
FROZEN_SHA="$(git rev-parse HEAD)"
printf '%s\n' "$FROZEN_SHA"
```

Required: working tree clean; printed SHA is the exact head that passed both fresh suites.

- [ ] **Step 6: Record Linux checkpoint**

On PR #47 record the outputs of:

```bash
git log -1 --format=%H -- docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation-rollback-correction.md
git log -1 --format=%H --grep='test: lock journal-independent form ref identity'
git log -1 --format=%H --grep='refactor: expose structural form provenance'
git log -1 --format=%H --grep='fix: make AcroForm refs journal-independent'
printf '%s\n' "$FROZEN_SHA"
```

Alongside Linux static 21/21 and ASan/UBSan 21/21. Keep PR draft.

---

### Task 5: Same-SHA full CI and correction review gate

**Files:**
- No source changes expected

**Interfaces:**
- Consumes: `FROZEN_SHA` from Task 4.
- Produces: same-SHA Linux/macOS/Windows proof and review decision; does not merge.

- [ ] **Step 1: Rebind the frozen SHA from the proven branch head**

At the start of this task:

```bash
FROZEN_SHA="$(git rev-parse HEAD)"
printf '%s\n' "$FROZEN_SHA"
```

Fresh-read PR #47 and require target `master`, head exactly equal to `$FROZEN_SHA`, and draft/open state. If branch head differs, repeat Task 4 on the new head before using any CI evidence.

- [ ] **Step 2: Require normal Linux PR proof on `$FROZEN_SHA`**

Required workflow `head_sha` equals `$FROZEN_SHA`, with Linux static 21/21 and Linux ASan/UBSan 21/21.

- [ ] **Step 3: Use existing `full-ci` without workflow edits**

Required on the same `$FROZEN_SHA`:

```text
Linux static + sanitizer 21/21
macOS 21/21
Windows DLL 21/21
```

Windows must show `extractpdf.dll`, `extractpdf_test_pdf_form_mutation.exe`, and `extractpdf.pdf_form_mutation` built/ran.

- [ ] **Step 4: Review exact correction scope**

```bash
git diff --stat 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2..."$FROZEN_SHA"
git diff --name-only 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2..."$FROZEN_SHA"
```

Allowed paths:

```text
docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation-rollback-correction.md
tests/test_pdf_form_rollback.c
src/pdf_form_common.h
src/pdf_form_common.c
src/pdf_edit_internal.h
src/pdf_edit_forms.c
src/pdf_edit.c
```

Any other changed path blocks integration until separately explained/reviewed.

- [ ] **Step 5: Critical/Important review**

Require all of:

```text
registry contains locator steps/count + tag, not retained form pdf_obj *
locator derives only from /Fields and /Kids ordinals
locator is independent of name/public field_index/num-gen/value/Widget identity
provenance locator ownership is balanced
registry locator ownership is balanced
same field discovery reuses same locator-backed slot/token
token domain/session/slot/tag validation is preserved
valid token + unresolved locator -> STATE
setter receives fresh current group_head/group_nodes/widgets
all three injected failures are byte-identical
all three preserve same-token rediscovery and original-ref reuse
no post-abandon repair writes
raw observation remains page-runtime free
Text/Choice page load remains pre-mutation only
forbidden runtime setters/recalculation remain absent
public ABI unchanged
```

No Critical or Important blocker may remain.

- [ ] **Step 6: Record evidence and STOP**

Post on PR #47:

```bash
printf 'corrected-spec=%s\n' 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2
printf 'correction-plan=%s\n' "$(git log -1 --format=%H -- docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation-rollback-correction.md)"
printf 'red=%s\n' "$(git log -1 --format=%H --grep='test: lock journal-independent form ref identity')"
printf 'provenance=%s\n' "$(git log -1 --format=%H --grep='refactor: expose structural form provenance')"
printf 'registry-green=%s\n' "$(git log -1 --format=%H --grep='fix: make AcroForm refs journal-independent')"
printf 'frozen=%s\n' "$FROZEN_SHA"
```

Add Linux static 21/21, Linux ASan/UBSan 21/21, macOS 21/21, Windows DLL 21/21, changed-path list, and review result. Keep PR #47 draft/open and #46 open. **STOP.**

---

### Task 6: Integration only after explicit authorization

**Files:**
- No planned source changes

**Interfaces:**
- Consumes: exact frozen SHA with Task 5 same-SHA proof plus explicit user authorization.
- Produces: integrated-master proof and completed #46.

- [ ] **Step 1: Re-read integration gate**

Immediately before any PR state change require: PR #47 still targets `master`; head equals the frozen proven SHA; same-SHA Linux/macOS/Windows checks are green; no new Critical/Important blocker; master compatibility is still valid.

- [ ] **Step 2: Ready/merge only exact proven head**

Use PR #47 when draft-to-ready works. If the known connector draft-ready metadata path fails, an exact-SHA carrier PR is allowed only when it points to the already-proven feature commit and adds zero content commits. Record the workaround on PR #47 and #46. Never merge a different head.

- [ ] **Step 3: Require integrated-master proof**

On the exact merge SHA require:

```text
Linux static 21/21
Linux ASan/UBSan 21/21
macOS 21/21
Windows DLL 21/21
```

Feature-branch proof alone does not close #46.

- [ ] **Step 4: Close only after master proof**

After integrated-master success: close #46 as completed; update roadmap #2 with corrected-spec SHA, frozen feature SHA, merge SHA, feature full-ci workflow, and master-push workflow; leave deferred field-structure/signature/XFA/flattening work unchecked. Do not start another Forms slice unless separately requested.
