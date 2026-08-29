# ExtractPDF AcroForm Value Mutation #266 Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the workflow #266 rollback/ref regression by making `extractpdf_form_field_ref` identity independent of journal-sensitive MuPDF object incarnations and proving failed form mutations are byte-identical, ref-stable, and editor-reusable.

**Architecture:** Preserve the already-built AcroForm Value Mutation V1 semantics and its #264 GREEN implementation. Extend the existing strict field-tree provenance with a private structural locator consisting of the `/AcroForm/Fields` ordinal followed by `/Kids` ordinals to the logical group head; copy that locator into the editor's form-ref registry, then resolve every public ref against a fresh strict parse instead of retaining `pdf_obj *group_head` as durable identity. Keep MuPDF journal rollback as the PDF mutation boundary, discard transient page/Widget state after failures, and use the existing #265/#266 fault tests plus a same-token rediscovery assertion as the observable atomicity oracle.

**Tech Stack:** C11, MuPDF 1.28.2, CMake 3.20+, CTest, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, GitHub Actions Linux/macOS/Windows.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-value-mutation-design.md`

## Global Constraints

- Execution starts from committed corrected-spec head `2cabcd032702e9b12582df9e3dd68ec0d14a6cc2` on `feat/acroform-value-mutation`.
- Historical feature base remains exactly `fdcb2f6cd489de34802d09989ab61a1af8cd1861`.
- Historical workflow #264 at source `d6fd08e50e48fc448896c551d3d4224c5ed272f1` is the pre-atomicity GREEN checkpoint after target Widget appearance refresh.
- Historical source `904012e256c4cec555a429be00203ac8d7fbaf45` and workflow #265 introduced rollback/ref-reuse tests and are RED evidence.
- Historical source `8a8f2a0c5a9e2f78fe10227dad734bc85c40e3c9` and workflow #266 added semantic-write, first-Widget-state, and first-AP-refresh fault injection and are RED evidence.
- The original plan `docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation.md` remains the historical record for Tasks 1-8 and the original integration contract. This correction plan supersedes its Task 9+ identity/rollback implementation wherever the corrected spec differs.
- Do not replay or squash historical GREEN/RED commits. Continue forward from the current branch head with reviewable correction commits.
- Pinned PDF engine remains MuPDF **1.28.2**. Do not change vcpkg pins, overlay ports, CMake policy, or `.github/workflows/ci.yml` to make the correction pass.
- Public ABI is unchanged. Do not add, remove, or rename any public form type or function.
- `extractpdf_form` remains immutable, deep-owned, document-independent, and free of MuPDF pointers.
- Public form-ref tokens remain session-local opaque `{uint64_t opaque[2]}` values with a form-specific token domain distinct from annotation refs.
- Durable form-ref registry identity must not contain or derive from a retained direct `pdf_obj *`, loaded `pdf_page *`, `pdf_annot *`, Widget wrapper address, or other journal-sensitive MuPDF object incarnation.
- The durable registry key is a private structural locator: `/AcroForm/Fields[top]` followed by zero or more `/Kids[child]` ordinals to the logical group head.
- Snapshot public `field_index`, field name, PDF object num/gen, Widget index, option text, `/V`, and `/I` are not durable mutation identity.
- Pointer equality and indirect num/gen identity may remain local witnesses inside one strict parse/reconciliation lifetime; they are not registry keys across public calls or journal boundaries.
- V1 does not mutate `/Fields`, `/Kids`, `/Parent`, `/T`, field creation/deletion/reordering/reparenting, or Widget topology. Structural-locator stability relies on this explicit V1 non-goal.
- Every setter re-runs the strict current-form parser and resolves the registry locator to exactly one current terminal field before mutation. No name-based rebinding, semantic-value search, post-abandon registry repair, or silent replacement identity is allowed.
- A valid session ref whose locator does not resolve after a successful strict parse returns `EXTRACTPDF_ERROR_STATE`.
- Observation/ref discovery stays raw-page only and must not add `pdf_load_page()`, `fz_load_page()`, `pdf_update_page()`, `pdf_update_open_pages()`, or form runtime/event execution.
- Text/Combo/List `pdf_load_page()` remains allowed only in the already-proven pre-mutation Widget-wrapper preparation layer after capability/assignment/no-op validation.
- Every successful non-noop setter remains exactly one outer `pdf_begin_operation()` / `pdf_end_operation()` pair. Any failure after begin executes `pdf_abandon_operation()`.
- Failed setters must produce a new deterministic editor snapshot with exactly the same size and bytes as immediately before the call, preserve all existing valid form refs, preserve repeated-discovery token equality, and leave the editor reusable.
- Do not add application-level repair writes after `pdf_abandon_operation()`. If locator-backed identity plus existing journal/transient cleanup cannot satisfy the byte/ref oracle, STOP and return to design.
- Keep PR #47 draft/open through the same-SHA correction proof. Do not merge or close #46 without explicit integration authorization.
- Final frozen correction SHA must pass Linux static 21/21, Linux ASan/UBSan 21/21, macOS 21/21, and Windows DLL 21/21 on that exact SHA.

## Historical State and Correction Scope

Already implemented before this correction:

```text
strict compile RED
raw-page AcroForm observation
editor form snapshots + refs
Widget-wrapper zero-byte safety gate
Text mutation
Checkbox/Radio mutation
Combo/List mutation
target-only Text/Choice appearance refresh
fault IDs 5-7 and fault injection points
byte-level rollback helper
```

The rejected durable identity is currently:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

and current ref resolution re-parses the form then matches:

```c
extractpdf_pdf_form_same_identity(
    edit->ctx,
    entry->group_head,
    provenance->fields[i].group_head)
```

For direct objects, `extractpdf_pdf_form_same_identity()` falls back to pointer equality. That relation is valid inside one current graph but not across journal abandon.

## File Structure

**Create**

- No production source files.
- This correction plan file only.

**Modify during execution**

- `tests/test_pdf_form_rollback.c` — sharpen #266 RED with post-abandon same-token rediscovery for every injected failure.
- `src/pdf_form_common.h` — add private structural locator data to live provenance.
- `src/pdf_form_common.c` — record child ordinals during strict traversal, materialize locators for terminal groups, and free locator storage.
- `src/pdf_edit_internal.h` — replace retained `group_head` in form registry entries with owned locator steps/count.
- `src/pdf_edit_forms.c` — register/reuse by locator, validate ref token separately from live-object resolution, and match the locator against fresh current provenance.
- `src/pdf_edit.c` — free registry locator storage instead of dropping retained form group-head objects.

**Do not modify unless this plan explicitly returns to design**

- `include/extractpdf/extractpdf.h`
- `src/pdf_edit_form_values.c`
- `src/pdf_edit_form_buttons.c`
- `src/pdf_edit_form_choices.c`
- `src/pdf_edit_form_widgets.c`
- `src/pdf_form_widgets.c`
- `.github/workflows/ci.yml`
- vcpkg pins/ports

The existing semantic setter/appearance code is not being redesigned by #266. If the correction requires changing those files to make rollback appear green, STOP and explain the distinct residual failure before widening scope.

---

### Task 1: Sharpen the #266 RED around public ref identity

**Files:**
- Modify: `tests/test_pdf_form_rollback.c`
- Do not modify: `src/`, `include/`, CMake, fixtures

**Interfaces:**
- Consumes: existing `rollback_field_ref()`, `rollback_expect_failed_atomic()`, fault IDs 5-7, and deterministic rollback fixtures.
- Produces: an explicit post-abandon invariant that repeated discovery returns the same public token before the old token is reused.

- [ ] **Step 1: Add a token-equality helper**

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

- [ ] **Step 2: Make the atomic-failure helper rediscover the same field**

Change the helper signature from:

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

After the existing byte comparison and successful `extractpdf_pdf_edit_form_snapshot()`, add:

```c
extractpdf_form_field_ref rediscovered = rollback_field_ref(edit, field_name);
rollback_expect_same_ref(ref, &rediscovered);
```

Keep the byte oracle before rediscovery so a PDF rollback failure is distinguished from an identity-registry failure.

- [ ] **Step 3: Pass the exact target field name from all three fault tests**

Use:

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

Do not remove the subsequent valid mutation through the original `ref`; same-token rediscovery and old-token reuse are separate requirements.

- [ ] **Step 4: Run only the mutation target and confirm the correction RED**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required result on the current pointer-backed registry:

```text
compile/link succeed
extractpdf.pdf_form_mutation fails
failure is in rollback/ref identity behavior, not unrelated form semantics
```

The direct-field semantic-write case is the primary attribution boundary. If the failure occurs earlier because bytes do not roll back, record that exact assertion and STOP; the identity correction cannot mask a distinct journal-atomicity defect.

- [ ] **Step 5: Commit the test-only RED**

```bash
git add tests/test_pdf_form_rollback.c
git commit -m "test: lock journal-independent form ref identity"
git push origin feat/acroform-value-mutation
```

Record the exact RED SHA and workflow run on PR #47 without changing draft status.

---

### Task 2: Emit an owned structural locator in strict form provenance

**Files:**
- Modify: `src/pdf_form_common.h`
- Modify: `src/pdf_form_common.c`
- Test: existing `extractpdf.pdf_form` and RED `extractpdf.pdf_form_mutation`

**Interfaces:**
- Consumes: the existing strict field-tree traversal where each node already records `parent_node`, `group_index`, and `depth`.
- Produces: `extractpdf_pdf_form_live_field.locator`, an owned path from `/AcroForm/Fields` through `/Kids` to the current logical group head.

- [ ] **Step 1: Add the private locator type to `src/pdf_form_common.h`**

Insert before `extractpdf_pdf_form_live_field`:

```c
typedef struct extractpdf_pdf_form_locator {
    size_t *steps;
    size_t step_count;
} extractpdf_pdf_form_locator;
```

Add it as the first identity member of the live field:

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

This locator is private provenance. Do not add it to any public header/model type.

- [ ] **Step 2: Record the field-tree ordinal on every transient parse node**

Extend `extractpdf_pdf_form_node` in `src/pdf_form_common.c`:

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

Change the private traversal signature to receive `tree_index`:

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

When the node is published, store:

```c
state->nodes[node_index].tree_index = tree_index;
```

Top-level `/Fields` traversal passes its array ordinal:

```c
status = extractpdf_pdf_form_traverse(
    state, field, SIZE_MAX, SIZE_MAX, 1, (size_t)index, 1);
```

Every `/Kids` recursion passes that child ordinal:

```c
status = extractpdf_pdf_form_traverse(
    state, child, node_index, group_index, depth + 1, (size_t)index, 0);
```

Do not derive the locator from names, object numbers, values, or addresses.

- [ ] **Step 3: Materialize the locator from the validated parent chain**

Add this helper before `extractpdf_pdf_form_materialize_provenance()`:

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
    if (count == 0 || count > SIZE_MAX / sizeof(*steps))
        return EXTRACTPDF_ERROR_FORMAT;
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

The existing traversal begins top-level nodes at depth `1`, so locator length is exactly the number of structural array selections from `/Fields` to the group head.

- [ ] **Step 4: Attach one locator to every public terminal field provenance record**

Inside `extractpdf_pdf_form_materialize_provenance()`, immediately after resolving `live` and before retaining `group_head`, add:

```c
status = extractpdf_pdf_form_materialize_locator(
    state, group->head_node, &live->locator);
if (status != EXTRACTPDF_OK)
    return status;
```

Declare `extractpdf_status status;` in that loop scope if not already present.

Do not remove `live->group_head`, `group_nodes`, `effective_v_owner`, or Widget provenance. They remain the fresh current-call mutation objects; only durable registry identity changes in Task 3.

- [ ] **Step 5: Free provenance locator storage**

In `extractpdf_pdf_form_drop_provenance()`, for each live field add:

```c
free(field->locator.steps);
field->locator.steps = NULL;
field->locator.step_count = 0;
```

Keep the existing `pdf_drop_obj()` and Widget cleanup unchanged.

- [ ] **Step 6: Run strict observation regressions**

```bash
cmake --build build --target extractpdf_test_pdf_form extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form$' --output-on-failure
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required checkpoint:

```text
extractpdf.pdf_form passes
mutation target remains RED for the same pointer-backed registry invariant
no public form ordering/name/value/Widget behavior changes
```

If `extractpdf.pdf_form` fails, fix only locator bookkeeping in this task; do not alter semantic parsing rules.

- [ ] **Step 7: Commit the provenance unit**

```bash
git add src/pdf_form_common.h src/pdf_form_common.c
git commit -m "refactor: expose structural form provenance"
git push origin feat/acroform-value-mutation
```

---

### Task 3: Replace durable `pdf_obj *` registry identity with locator resolution

**Files:**
- Modify: `src/pdf_edit_internal.h`
- Modify: `src/pdf_edit_forms.c`
- Modify: `src/pdf_edit.c`
- Test: `tests/test_pdf_form_rollback.c` through the existing mutation target

**Interfaces:**
- Consumes: Task 2 `extractpdf_pdf_form_live_field.locator` plus current live `group_head/group_nodes/widgets` from every fresh strict parse.
- Produces: stable public form refs whose registry entries own only structural locator data + token tag.

- [ ] **Step 1: Replace the form registry entry layout**

Change `extractpdf_pdf_edit_form_entry` in `src/pdf_edit_internal.h` from:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

to:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    size_t *locator_steps;
    size_t locator_step_count;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

Do not retain both `group_head` and locator as a fallback. Keeping the pointer would preserve the rejected second identity model.

- [ ] **Step 2: Add exact locator comparison helpers in `src/pdf_edit_forms.c`**

Add:

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

The zero-length branch is defensive; valid terminal fields emitted by Task 2 always have at least the top-level `/Fields` step.

- [ ] **Step 3: Register/reuse refs by structural locator and copy registry-owned storage**

Change `extractpdf_pdf_edit_form_register()` to accept the current live field:

```c
static extractpdf_status extractpdf_pdf_edit_form_register(
    extractpdf_pdf_edit *edit,
    const extractpdf_pdf_form_live_field *live,
    extractpdf_form_field_ref *out_ref)
```

Start with strict private validation:

```c
if (live == NULL || live->locator.step_count == 0 ||
    live->locator.steps == NULL)
    return EXTRACTPDF_ERROR_FORMAT;
```

Reuse an existing slot only by locator equality:

```c
for (slot = 0; slot < edit->form_entry_count; ++slot) {
    if (extractpdf_pdf_edit_form_locator_equal(
            &edit->form_entries[slot], &live->locator)) {
        extractpdf_pdf_edit_form_make_token(edit, slot, out_ref);
        return EXTRACTPDF_OK;
    }
}
```

Before publishing a new entry, copy the locator:

```c
size_t *locator_copy;

if (live->locator.step_count >
    SIZE_MAX / sizeof(*locator_copy))
    return EXTRACTPDF_ERROR_NOMEM;
locator_copy = (size_t *)malloc(
    live->locator.step_count * sizeof(*locator_copy));
if (locator_copy == NULL)
    return EXTRACTPDF_ERROR_NOMEM;
memcpy(
    locator_copy,
    live->locator.steps,
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

- [ ] **Step 4: Update field-ref discovery to register the locator**

In `extractpdf_pdf_edit_form_field_ref_at()`, replace the `group_head` argument with the live provenance record:

```c
status = extractpdf_pdf_edit_form_register(
    edit, &provenance->fields[field_index], out_ref);
```

Keep the existing strict parse, range checks, provenance count checks, output zeroing, and cleanup.

- [ ] **Step 5: Validate token syntax without requiring a retained live object**

In `extractpdf_pdf_edit_form_resolve_ref()`, replace the old `entry->group_head == NULL` validation with locator validity:

```c
if (entry->tag != tag ||
    entry->locator_step_count == 0 ||
    entry->locator_steps == NULL)
    return EXTRACTPDF_ERROR_ARGUMENT;
```

Token/session/domain/slot/tag errors remain `ARGUMENT`. Do not inspect the current PDF in this helper; current-document resolution remains the next stage.

- [ ] **Step 6: Resolve the valid token against fresh current provenance by locator**

In `extractpdf_pdf_edit_form_find_current_field()`, keep the existing strict `extractpdf_pdf_form_build(..., 1, ...)` call. Replace the old `group_head` identity scan with:

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

After exactly one locator match, call the existing raw provenance Widget capture so the setter receives fresh current Widget objects:

```c
status = extractpdf_pdf_form_capture_provenance_widgets(
    edit->ctx, edit->document, model, provenance);
if (status != EXTRACTPDF_OK) {
    extractpdf_pdf_form_drop_provenance(edit->ctx, provenance);
    extractpdf_pdf_form_drop_model(model);
    return status;
}
```

Then publish `model`, `provenance`, and `match` exactly as before.

The order matters: ref identity resolution is structural; Widget capture remains current-call provenance and is not involved in durable identity.

- [ ] **Step 7: Remove retained form-object cleanup from editor disposal**

In `extractpdf_dispose_pdf_edit()` in `src/pdf_edit.c`, delete:

```c
for (index = 0; index < edit->form_entry_count; ++index) {
    if (edit->form_entries[index].group_head != NULL)
        pdf_drop_obj(edit->ctx, edit->form_entries[index].group_head);
}
```

Add registry-owned locator cleanup outside the `edit->ctx != NULL` requirement:

```c
for (index = 0; index < edit->form_entry_count; ++index) {
    free(edit->form_entries[index].locator_steps);
    edit->form_entries[index].locator_steps = NULL;
    edit->form_entries[index].locator_step_count = 0;
}
```

Keep annotation-entry `pdf_drop_obj()` cleanup and document/context disposal unchanged.

- [ ] **Step 8: Run the #266 fault target and require full GREEN**

```bash
cmake --build build --target extractpdf_test_pdf_form_mutation --parallel 2
ctest --test-dir build -R '^extractpdf\.pdf_form_mutation$' --output-on-failure
```

Required behavior for all existing fault cases:

```text
semantic-write failure -> byte-identical rollback
                           same direct-field ref on rediscovery
                           original ref immediately reusable
first button /AS failure -> byte-identical rollback
                            same ref on rediscovery
                            original ref reusable
first AP refresh failure -> byte-identical rollback
                            same ref on rediscovery
                            target/unrelated/calc values restored
                            original ref reusable
```

If the mutation target still fails **after** locator identity assertions pass, stop at the exact remaining byte/state assertion and return to the corrected design. Do not add registry repair writes or widen into semantic setter files under this task.

- [ ] **Step 9: Run read-only parser regression beside mutation GREEN**

```bash
ctest --test-dir build -R '^extractpdf\.(pdf_form|pdf_form_mutation)$' --output-on-failure
```

Required: both tests pass.

- [ ] **Step 10: Commit the minimal production correction**

```bash
git add src/pdf_form_common.h src/pdf_form_common.c \
  src/pdf_edit_internal.h src/pdf_edit_forms.c src/pdf_edit.c
git commit -m "fix: make AcroForm refs journal-independent"
git push origin feat/acroform-value-mutation
```

Do not include test changes from Task 1 in this commit; that RED already has its own commit.

---

### Task 4: Prove observable rollback and freeze the Linux correction candidate

**Files:**
- No planned source changes
- Evidence only unless a test exposes a spec contradiction, in which case STOP

**Interfaces:**
- Consumes: locator-backed GREEN from Task 3.
- Produces: fresh Linux static + sanitizer proof and a frozen candidate SHA.

- [ ] **Step 1: Fresh static configure/build/test**

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

- [ ] **Step 2: Fresh ASan/UBSan configure/build/test**

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

Required: **21/21**, no ASan/UBSan finding.

- [ ] **Step 3: Prove the rejected durable identity is gone**

Run:

```bash
git grep -n 'form_entries.*group_head' -- src || true
git grep -n 'form_entries\[.*\]\.group_head' -- src || true
git grep -n 'pdf_keep_obj.*group_head' -- src/pdf_edit_forms.c || true
```

Required: no match showing form registry storage/reuse by `group_head`.

`group_head` remains expected in fresh provenance and setter code; the audit is specifically against durable form registry entries.

- [ ] **Step 4: Prove public observation still has no page/runtime entry**

Run:

```bash
git grep -nE 'pdf_load_page|fz_load_page|pdf_update_page|pdf_update_open_pages' -- \
  src/pdf_form_common.c src/pdf_form_widgets.c src/pdf_edit_forms.c || true
```

Required: no matches.

The only V1 `pdf_load_page()` remains inside `src/pdf_edit_form_widgets.c` pre-mutation target-wrapper preparation.

- [ ] **Step 5: Prove forbidden form-runtime setters remain absent**

Run:

```bash
git grep -nE 'pdf_set_field_value|pdf_set_annot_field_value|pdf_set_text_field_value|pdf_set_choice_field_value|pdf_choice_widget_set_value|pdf_toggle_widget|pdf_calculate_form|pdf_reset_form' -- src || true
```

Required: no matches.

- [ ] **Step 6: Freeze and record the exact candidate SHA**

```bash
git status --short
git rev-parse HEAD
```

Required:

```text
working tree clean
HEAD is the same SHA that passed both fresh 21/21 runs
```

Post a PR #47 checkpoint containing:

```text
#266 correction RED SHA/workflow
locator provenance commit SHA
locator-backed registry GREEN SHA
fresh Linux static 21/21
fresh Linux ASan/UBSan 21/21
frozen candidate SHA
```

Do not mark the PR ready.

---

### Task 5: Same-SHA full CI and correction review gate

**Files:**
- No source changes expected
- GitHub metadata/evidence only

**Interfaces:**
- Consumes: frozen candidate SHA from Task 4.
- Produces: Linux/macOS/Windows same-SHA proof and a merge-readiness review; does not merge.

- [ ] **Step 1: Fresh-read PR #47 and reject stale proof**

Require:

```text
PR target == master
PR head == frozen candidate SHA
PR remains draft/open
```

If the branch advanced, discard old candidate proof and repeat Task 4 on the new exact head.

- [ ] **Step 2: Require normal Linux PR proof on that exact SHA**

Required workflow evidence:

```text
Linux static 21/21
Linux ASan/UBSan 21/21
```

The workflow's `head_sha` must equal the frozen candidate SHA.

- [ ] **Step 3: Apply/use the existing `full-ci` gate without editing workflow YAML**

Required same-SHA result:

```text
Linux static + sanitizer 21/21
macOS 21/21
Windows DLL 21/21
```

Windows evidence must show `extractpdf.dll`, `extractpdf_test_pdf_form_mutation.exe`, and `extractpdf.pdf_form_mutation` actually built/ran.

- [ ] **Step 4: Review exact correction scope since the committed spec gate**

Compare:

```bash
git diff --stat 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2...<frozen-candidate-sha>
git diff --name-only 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2...<frozen-candidate-sha>
```

Allowed correction paths are exactly:

```text
docs/superpowers/plans/2026-08-29-extractpdf-acroform-value-mutation-rollback-correction.md
tests/test_pdf_form_rollback.c
src/pdf_form_common.h
src/pdf_form_common.c
src/pdf_edit_internal.h
src/pdf_edit_forms.c
src/pdf_edit.c
```

Any additional source/test/workflow path requires an explicit scope explanation and a new review before integration.

- [ ] **Step 5: Fresh Critical/Important review against the corrected spec**

Review this exact checklist:

```text
registry entry contains locator steps/count + tag, not retained form pdf_obj *
locator is derived only from /Fields and /Kids ordinals in the strict validated tree
locator is independent of name, public field_index, num/gen, values, and Widget identity
provenance locator memory is owned/freed exactly once
registry locator memory is owned/freed exactly once
repeated discovery reuses the same locator-backed slot/token
token domain/session/slot/tag validation remains unchanged
valid token + unresolved current locator -> STATE, never ARGUMENT/name rebinding
setter uses fresh current provenance group_head/group_nodes/widgets after resolution
failed semantic write produces byte-identical snapshot
failed first button state produces byte-identical snapshot
failed first AP refresh produces byte-identical snapshot
all three failures preserve same-token rediscovery and old-token reuse
no post-abandon registry/PDF repair writes
raw observation remains page-runtime free
Text/Choice wrapper page load remains pre-mutation only
no forbidden form-runtime setters/recalculation
public ABI unchanged
```

No Critical or Important blocker may remain.

- [ ] **Step 6: Record evidence and STOP**

Post one PR #47 correction checkpoint containing:

```text
corrected spec commit: 2cabcd032702e9b12582df9e3dd68ec0d14a6cc2
correction plan commit
#266-derived RED commit/workflow
locator provenance commit
locator-backed registry commit
frozen feature SHA
Linux static 21/21
Linux ASan/UBSan 21/21
macOS 21/21
Windows DLL 21/21
correction changed-path list
Critical/Important review result
```

Keep PR #47 draft/open and issue #46 open. **STOP.** Do not mark ready, merge, close #46, or start another Forms slice without explicit user integration authorization.

---

### Task 6: Integration only after explicit authorization

**Files:**
- No planned source changes
- GitHub state/evidence only

**Interfaces:**
- Consumes: exact frozen feature SHA with Task 5 same-SHA proof plus explicit user authorization.
- Produces: integrated-master proof and completion of #46.

- [ ] **Step 1: Re-read the integration gate immediately before any PR state change**

Require:

```text
PR #47 still targets master
PR #47 source head == frozen proven feature SHA
same-SHA Linux/macOS/Windows proof is successful
no new Critical/Important review blocker
master compatibility is still valid
```

If master moved incompatibly, create/prove a new feature SHA; never reuse old CI evidence for changed content.

- [ ] **Step 2: Mark ready/merge only with exact-head protection**

Use PR #47 if draft-to-ready succeeds. If the existing connector's known draft-ready metadata path fails, an exact-SHA carrier PR is permitted only when it points to the already-proven feature commit and introduces **zero content commits**. Record that workaround on PR #47 and #46.

Do not merge a head different from the proven SHA.

- [ ] **Step 3: Require integrated-master push proof on the exact merge SHA**

Required:

```text
Linux static 21/21
Linux ASan/UBSan 21/21
macOS 21/21
Windows DLL 21/21
```

Feature-branch proof is not sufficient to close #46.

- [ ] **Step 4: Close only after integrated-master proof**

After the exact merge SHA is green:

```text
close #46 as completed
update roadmap #2 with Form Value Mutation V1 integrated
record corrected-spec SHA, frozen feature SHA, merge SHA,
feature full-ci workflow, and master-push workflow
leave deferred field-structure/signature/XFA/flattening work unchecked
```

Do not begin another Forms slice unless separately requested.
