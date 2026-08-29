# ExtractPDF AcroForm Value Mutation V1 Design

Status: approved architecture for issue #46 under roadmap #2.

Base integrated master: `fdcb2f6cd489de34802d09989ab61a1af8cd1861`.

Design branch: `feat/acroform-value-mutation`.

Pinned PDF engine: MuPDF 1.28.2.

## 1. Purpose and scope

AcroForm Value Mutation V1 adds deterministic field-value assignment to the already-integrated isolated `extractpdf_pdf_edit` layer.

It deliberately does **not** make the immutable `extractpdf_form` snapshot mutable. Observation and mutation remain separate:

```text
extractpdf_document
    -> extractpdf_document_form()
    -> immutable extractpdf_form

extractpdf_pdf_edit
    -> extractpdf_pdf_edit_form_snapshot()
    -> immutable extractpdf_form

extractpdf_pdf_edit
    -> field_index discovery
    -> session-local extractpdf_form_field_ref
    -> extractpdf_pdf_edit_form_set_values()
```

The V1 mutation surface covers semantic value assignment only:

- Text values;
- Checkbox and Radio selected state / explicit Off / missing value;
- ComboBox option assignment and editable custom text;
- single- and multi-select ListBox values;
- target Widget `/AS` state updates for buttons;
- target Widget appearance refresh for Text/Combo/List;
- immutable editor-form snapshots for observation;
- journal-atomic rollback on any mutation or appearance failure.

The API is a deterministic data-assignment surface. It is **not** an Acrobat form-runtime emulator and does not simulate typing, clicking, validation, calculation, submission, reset, or JavaScript execution.

## 2. Architectural placement

The existing PDF editor remains the only mutable PDF handle:

```text
extractpdf_pdf_edit
  private fz_context
  private pdf_document
  MuPDF journal
  session cookie
  annotation registry
  form-field registry       <- V1 addition
```

No second mutable form handle is introduced.

The immutable snapshot remains the canonical observation model:

```text
current editor PDF
      |
      +-- strict AcroForm parser
      |      |
      |      +-- immutable semantic model -> extractpdf_form
      |      |
      |      +-- optional live provenance sidecar
      |             field[i] -> canonical logical group head
      |
      +-- form-field registry -> session-local opaque refs
```

The public `extractpdf_form` remains deep-owned, document-independent, and free of MuPDF pointers.

## 3. Public identity model

### 3.1 Form field refs

Add:

```c
typedef struct extractpdf_form_field_ref {
    uint64_t opaque[2];
} extractpdf_form_field_ref;
```

A form field ref identifies one **logical terminal field group** inside one `extractpdf_pdf_edit` session.

It does not identify:

- an individual Widget;
- a snapshot field index;
- a fully-qualified field name;
- a PDF object number as public identity;
- a field across editor sessions;
- a persistent identity after save/reopen.

### 3.2 Separate token domain from annotation refs

Form refs and annotation refs use independent token domains even though both are opaque two-word values.

The internal form token must incorporate a form-specific domain salt in addition to the editor session cookie, slot, and tag. A bitwise-copied annotation ref passed to a form API must fail validation as `EXTRACTPDF_ERROR_ARGUMENT`.

The intended properties are:

```text
same edit + same field       -> same form ref
same edit + different field  -> different form ref
wrong edit                   -> ARGUMENT
forged token                 -> ARGUMENT
annotation token as form ref -> ARGUMENT
old token after reopen       -> ARGUMENT
```

### 3.3 Registry entry

The registry stores identity only, not cached semantic state.

Conceptually:

```c
typedef struct extractpdf_pdf_edit_form_entry {
    pdf_obj *group_head;
    uint32_t tag;
} extractpdf_pdf_edit_form_entry;
```

`group_head` is the canonical logical-group head established by the strict form parser. It may be an indirect object or a retained direct dictionary. The registry retains it with the editor context and releases it when the editor is dropped.

The registry does not cache:

- field value;
- field flags;
- options;
- name or label;
- Widget list;
- appearance state.

Every observation or setter re-parses the current private PDF state strictly.

### 3.4 Ref lifetime

A successfully issued form ref remains valid while the owning editor remains alive across:

- mutations of the same field;
- mutations of another field;
- annotation create/update/delete;
- editor form snapshots;
- editor PDF output snapshots;
- repeated output serialization.

V1 does not create/delete/reparent fields, so form refs have no public tombstone lifecycle. The namespace ends when `extractpdf_drop_pdf_edit()` is called.

## 4. Discovery and observation API

Add exactly two read/discovery functions to the editor surface:

```c
extractpdf_status extractpdf_pdf_edit_form_snapshot(
    extractpdf_pdf_edit *edit,
    extractpdf_form **out_form);

extractpdf_status extractpdf_pdf_edit_form_field_ref_at(
    extractpdf_pdf_edit *edit,
    size_t field_index,
    extractpdf_form_field_ref *out_ref);
```

Do **not** duplicate the immutable form getter API with editor-specific `*_count`, `*_get_info`, `*_option`, or `*_widget` functions.

Typical use:

```text
edit
  -> form_snapshot()
  -> inspect immutable field/index/options
  -> field_ref_at(index)
  -> set_values(ref, update)
  -> new form_snapshot()
```

### 4.1 Editor form snapshot

`extractpdf_pdf_edit_form_snapshot()` runs the same strict semantic parser used by `extractpdf_document_form()` against the editor's current private PDF state and publishes the same immutable `extractpdf_form` type.

Rules:

- `out_form` is required;
- `*out_form` is set to `NULL` before later validation;
- invalid editor -> `ARGUMENT`;
- malformed current form -> `FORMAT`;
- valid structural depth greater than 256 -> `UNSUPPORTED`;
- allocation failure -> `NOMEM`;
- success -> non-NULL immutable deep-owned snapshot.

A successful editor form snapshot:

- remains unchanged after later mutations;
- survives `extractpdf_drop_pdf_edit()`;
- contains no live MuPDF pointers;
- does not register refs;
- may coexist with any number of older/newer snapshots.

### 4.2 Field-ref discovery

`extractpdf_pdf_edit_form_field_ref_at()` re-runs the current strict parser with an optional private provenance sidecar.

Rules:

- `out_ref` is required;
- output ref is zeroed before later validation;
- invalid editor -> `ARGUMENT`;
- out-of-range `field_index` -> `ARGUMENT`;
- malformed form -> `FORMAT`;
- valid depth greater than 256 -> `UNSUPPORTED`;
- success registers or reuses the canonical logical group head and returns a stable ref.

`field_index` is only the current snapshot-order discovery coordinate. It is never mutation identity.

All public field types may receive a ref, including PushButton, Signature, and UNKNOWN. Mutation capability is decided by the setter.

### 4.3 Mutation-only restrictions do not block observation

`extractpdf_pdf_edit_form_snapshot()` and `extractpdf_pdf_edit_form_field_ref_at()` continue to use normal AcroForm Snapshot V1 read semantics.

The mutation-only restrictions on `/XFA` and `/NeedAppearances` apply only to `extractpdf_pdf_edit_form_set_values()`.

## 5. Public typed assignment ABI

Add:

```c
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
```

Setter:

```c
extractpdf_status extractpdf_pdf_edit_form_set_values(
    extractpdf_pdf_edit *edit,
    const extractpdf_form_field_ref *ref,
    const extractpdf_form_value_update *update);
```

There is no field-update bitmask. V1 replaces one field's complete semantic value in one call.

### 5.1 Struct-size compatibility

Minimum accepted sizes are:

```c
offsetof(extractpdf_form_value_input, utf8_size)
    + sizeof(input->utf8_size)

offsetof(extractpdf_form_value_update, value_count)
    + sizeof(update->value_count)
```

Too-small structs return `ARGUMENT`. Current-size and larger structs are accepted; unknown trailing bytes are ignored.

## 6. Generic input validation

Writable presence values are exactly:

```text
MISSING
PRESENT
```

`NOT_APPLICABLE` is an observation state and is not a mutation command; it returns `ARGUMENT`.

Generic validation is performed before field-specific capability checks:

```text
update == NULL                         -> ARGUMENT
value_count > 0 && values == NULL      -> ARGUMENT
unknown value kind                     -> ARGUMENT
too-small nested struct                -> ARGUMENT
UTF8 with utf8 == NULL                 -> ARGUMENT
UTF8 invalid UTF-8                     -> ARGUMENT
UTF8 with embedded NUL                 -> ARGUMENT
UTF8 with option_index != SIZE_MAX     -> ARGUMENT
OPTION with utf8 != NULL               -> ARGUMENT
OPTION with utf8_size != 0             -> ARGUMENT
OPTION index outside current options   -> ARGUMENT
```

Present-empty UTF-8 is represented by a non-NULL pointer with size zero. `NULL + 0` does not mean an empty string.

## 7. Type-specific assignment matrix

| Field type | `MISSING` | `PRESENT` |
| --- | --- | --- |
| TEXT | zero values | exactly one UTF8 |
| CHECKBOX | zero values | zero values = explicit Off, or exactly one OPTION |
| RADIO_BUTTON | zero values | zero values = explicit Off, or exactly one OPTION |
| COMBO_BOX | zero values | exactly one OPTION; editable combo may use exactly one UTF8 |
| LIST_BOX single | zero values | exactly one OPTION |
| LIST_BOX multi | zero values | zero or more unique OPTION values |
| PUSH_BUTTON | unsupported | unsupported |
| SIGNATURE | unsupported | unsupported |
| UNKNOWN | unsupported | unsupported |

A field type that supports value mutation but receives an invalid kind/cardinality returns `ARGUMENT`.

Examples:

```text
TEXT + two UTF8 values       -> ARGUMENT
single List + PRESENT/0      -> ARGUMENT
non-editable Combo + UTF8    -> ARGUMENT
Radio + UTF8                 -> ARGUMENT
```

## 8. Text-field semantics

For a supported Text field:

```text
MISSING
    -> effective /V becomes absent

PRESENT UTF8("")
    -> /V is a present empty PDF text string

PRESENT UTF8("abc")
    -> /V is a PDF text string containing abc
```

Missing and present-empty remain distinct after editor snapshot, PDF serialization, reopen, and immutable form extraction.

V1 does not mutate `/DV`, `/T`, `/TU`, `/Ff`, or `/MaxLen`.

Supported ordinary Text flags include Multiline, Password, DoNotSpellCheck, DoNotScroll, and Comb.

Text fields with FileSelect or RichText flags return `UNSUPPORTED` for value mutation V1. Snapshot observation remains supported.

V1 does not enforce user-interface constraints such as MaxLen or input formatting.

## 9. Checkbox and Radio semantics

Button-state assignment uses field-local OPTION indices from the immutable semantic model. Raw PDF Name bytes remain private.

For selected OPTION `i`:

```text
field /V = private PDF Name represented by option[i]
```

For explicit Off:

```text
presence = PRESENT
value_count = 0
field /V = /Off
all Widgets /AS = /Off
```

For missing value:

```text
presence = MISSING
value_count = 0
/V removed within the target logical group
all Widgets /AS = /Off
```

Missing and explicit Off remain observably distinct.

For a selected option, each Widget in the logical field is assigned:

```text
its matching on-state -> /AS = that on-state
otherwise             -> /AS = /Off
```

Existing `/AP` streams are preserved. V1 never regenerates checkbox/radio appearance streams merely because value changed.

`NoToggleToOff` does not prevent explicit Off or Missing assignment because this API is deterministic data assignment, not simulated user clicking.

`RadiosInUnison` does not redefine API semantics. Multiple Widgets that represent the same normalized option state naturally receive the same selected state.

## 10. Choice-field semantics

### 10.1 OPTION assignment

Combo/List OPTION assignment uses a field-local option index rather than an export string supplied by the caller.

For a selected choice option, write the option's export value to `/V` and write the exact option index to `/I`.

For single selection:

```text
/V = export string
/I = [index]
```

Writing `/I` for single selection preserves exact option identity even when multiple options share the same export string.

### 10.2 Editable Combo custom text

Only ComboBox with the Edit flag accepts UTF8 assignment.

Custom text writes:

```text
/V = custom PDF text string
/I removed within target group
```

UTF8 input remains custom-text intent even when its bytes equal an existing option export value. OPTION input is the only way to express option identity.

### 10.3 Multi-select List

Multi-select List accepts zero or more unique OPTION values.

For selections `i0, i1, ...`:

```text
/V = [export(i0) export(i1) ...]
/I = [i0 i1 ...]
```

Caller selection order is preserved. Duplicate option indices return `ARGUMENT`; V1 does not silently deduplicate.

`PRESENT + zero values` is a valid explicit empty selection for a multi-select List and is persisted as explicit empty `/V []` and `/I []` so it round-trips as PRESENT/0 rather than MISSING.

A single-select List does not accept `PRESENT + zero values`.

## 11. Logical-group value ownership

A logical field may contain unnamed descendants that remain in the same semantic group. Successful mutation must not leave conflicting local value providers inside that group.

For a successful explicit assignment:

```text
write canonical /V at logical group head
remove local /V overrides from same-group descendants
```

For Choice fields:

```text
write canonical /I at logical group head when applicable
remove local /I overrides from same-group descendants
```

Only `/V` and `/I` ownership are canonicalized by this slice. `/FT`, `/Ff`, `/Opt`, `/DV`, `/T`, `/TU`, and other properties are not relocated.

## 12. Inherited-value boundary

A target field may inherit `/V` from a provider outside its logical group.

Explicit PRESENT assignment is supported because a local group-head `/V` can safely override the external provider without changing siblings.

MISSING is unsupported when the effective `/V` provider lies outside the target logical group:

```text
external ancestor /V
       |
       +-- target group
       +-- sibling

set target MISSING
    deleting ancestor would change sibling
    deleting target local value would still inherit ancestor
    -> UNSUPPORTED
```

When the effective value is absent or all local `/V` providers are inside the target group, MISSING deletes `/V` from the group head and every same-group descendant. Choice mutation likewise removes local `/I` values.

`/DV` is never deleted.

## 13. Field flags and capability

### 13.1 ReadOnly

After the update command has been proven structurally and type-wise valid, effective ReadOnly blocks mutation with:

```text
EXTRACTPDF_ERROR_STATE
```

This means the command is supported and well-formed but current mutable field state forbids it.

### 13.2 Required and NoExport

Required does not block Missing, empty text, Off, or explicit empty multi-selection. It is a validation/submission constraint, not a storage-mutation prohibition.

NoExport does not block value mutation.

### 13.3 Unsupported field modes

Value mutation V1 returns `UNSUPPORTED` for:

- PushButton;
- Signature;
- UNKNOWN field type;
- Text FileSelect;
- Text RichText.

## 14. Mutation-only document preflight

Before any journal operation, each setter checks the current AcroForm mutation capability.

### 14.1 XFA

Any present `/XFA` entry makes Form Value Mutation V1 unsupported:

```text
/XFA absent  -> continue
/XFA present -> UNSUPPORTED
```

V1 does not try to interpret XFA packets or determine whether they are operationally relevant.

### 14.2 NeedAppearances

`/NeedAppearances` rules:

```text
missing       -> false / supported
Boolean false -> supported
Boolean true  -> UNSUPPORTED
non-Boolean   -> FORMAT
```

NeedAppearances true is unsupported because V1 must not enter document-wide appearance-rewrite or page-wide recalculation paths.

Mutation capability preflight occurs before semantic no-op detection. Therefore a same-value assignment still returns `UNSUPPORTED` when the current document is outside V1 mutation capability.

## 15. No execution model

Form Value Mutation V1 must not execute or simulate:

- Keystroke events;
- Validate events;
- Format events;
- Calculate events;
- Widget activation actions;
- SubmitForm / ResetForm actions;
- JavaScript;
- AcroForm calculation order `/CO`;
- page-wide form recalculation.

The existing editor already disables JavaScript when the private PDF document is opened. Form V1 adds stricter local behavior: it must not call MuPDF setters or page-update entry points that opt into form events/recalculation.

Forbidden implementation paths include:

```text
pdf_set_field_value
pdf_set_annot_field_value
pdf_set_text_field_value
pdf_set_choice_field_value
pdf_choice_widget_set_value
pdf_toggle_widget
pdf_update_page
pdf_update_open_pages
pdf_calculate_form
pdf_reset_form
```

The setter writes normalized raw PDF state directly according to the already-validated ExtractPDF semantic model.

## 16. Widget reconciliation for mutation

The strict parser/provenance layer must identify all Widgets owned by the target logical field using the same tree/page reconciliation contract as immutable AcroForm Snapshot V1.

A setter must not mutate a field unless current Widget reconciliation succeeds completely. Orphan/missing/duplicate/multi-page/direct/P-mismatched Widget structures remain `FORMAT` before journal mutation.

No partial field or Widget publication/mutation is allowed.

## 17. Appearance behavior

### 17.1 Checkbox / Radio

Checkbox and Radio value changes update semantic `/V` and every owned Widget `/AS` but preserve the existing `/AP` dictionary and appearance streams.

The setter does not use MuPDF button appearance synthesis.

### 17.2 Text / Combo / List

After normalized `/V` and `/I` assignment, each owned Text/Combo/List Widget is refreshed individually.

The intended path is:

```text
load the Widget as pdf_annot
remember current widget editing state
set widget editing state = true
request target Widget resynthesis
call pdf_update_widget / pdf_update_annot for that Widget only
restore previous editing state in exception-safe cleanup
```

The editing/ignore-trigger-events state prevents appearance synthesis from running form trigger behavior. The implementation must never call `pdf_update_page()` as a shortcut.

### 17.3 ListBox renderer limitation

MuPDF 1.28.2 can resynthesize ListBox appearance text but does not guarantee pixel-level selected-row highlighting. V1 therefore guarantees:

```text
/V + /I semantic selection correct
Widget resynthesis attempted successfully
```

but does not guarantee or test pixel-perfect selected-row highlight rendering.

A future custom List appearance slice may improve this without changing the V1 value API.

## 18. Journal transaction and atomicity

Each successful non-noop public setter is exactly one outer MuPDF journal operation.

The order is:

```text
validate all public inputs
resolve form ref
strictly parse current form/provenance
mutation capability preflight
validate assignment against actual type/options/state
prepare ExtractPDF-owned allocations
compare normalized current/requested value
if semantic no-op -> return OK without journal operation

pdf_begin_operation
    write canonical /V and /I state
    remove same-group local overrides
    update all button Widget /AS states
      OR
    refresh every target Text/Combo/List Widget appearance
pdf_end_operation
```

Any exception or failure after `pdf_begin_operation` and before successful `pdf_end_operation` must execute `pdf_abandon_operation`.

A failed setter must leave:

- current editor semantic form state identical to pre-call;
- newly serialized editor output semantically identical to pre-call;
- previously published outputs unchanged;
- field ref still valid;
- editor reusable for subsequent snapshots and mutations.

Private transient loaded pages/widgets are always dropped in exception-safe cleanup.

## 19. Semantic no-op contract

After all capability and caller validation succeeds, compare the normalized requested value to the current normalized semantic value.

If equal:

```text
return EXTRACTPDF_OK
no pdf_begin_operation
no /V rewrite
no /I rewrite
no /AS repair
no appearance synthesis
no dirtying
no recalculation
```

For no-op tests, output snapshots before and after the call must have identical size and bytes.

Same-value assignment is not a repair API. Existing unrelated `/AS` or appearance irregularities are not normalized as a side effect of a semantic no-op.

## 20. Error precedence

The public setter applies errors in this order:

```text
1. API pointers / struct_size / generic value-input shape
      -> ARGUMENT

2. form-ref token domain/session/tag/slot
      -> ARGUMENT

3. strict current-form parse
      -> FORMAT / UNSUPPORTED(depth)

4. mutation-only document preflight
   XFA / NeedAppearances
      -> UNSUPPORTED / FORMAT

5. assignment vs current field type/options/cardinality
      -> ARGUMENT

6. unsupported field mode
   PushButton / Signature / UNKNOWN / RichText / FileSelect
      -> UNSUPPORTED

7. ReadOnly
      -> STATE

8. MISSING cannot defeat external inherited /V
      -> UNSUPPORTED

9. journal mutation / appearance engine / allocation failure
      -> NOMEM or mapped MuPDF error
```

This preserves stable meanings:

```text
ARGUMENT    caller command/ref is invalid
FORMAT      current PDF structure/value metadata is malformed
UNSUPPORTED valid input/model outside V1 capability
STATE       valid supported assignment blocked by current field state
```

## 21. Output reset and ownership

`extractpdf_pdf_edit_form_snapshot()` resets `*out_form = NULL` before later validation.

`extractpdf_pdf_edit_form_field_ref_at()` zeroes both opaque words before later validation.

`extractpdf_pdf_edit_form_set_values()` has no output object to publish and never partially changes caller-owned inputs.

UTF-8 input is borrowed only for the duration of the call. The setter copies/prepares any required data before entering mutation where needed; it never retains caller pointers.

## 22. Verification strategy

The integrated suite starts at 20 CTests. Form Value Mutation V1 adds one independent target:

```text
extractpdf.pdf_form_mutation
```

Target progression:

```text
20 -> 21 CTests
```

Read-only AcroForm tests remain in `extractpdf.pdf_form`; mutation tests do not enlarge that target.

## 23. Strict first RED boundary

The first implementation commit contains only:

- `tests/test_pdf_form_mutation.c`;
- deterministic mutation-specific fixtures;
- `tests/CMakeLists.txt` registration.

It references the final approved public ABI while production/header declarations remain absent.

Required RED evidence:

```text
ExtractPDF library builds
existing test targets #1-#20 build
only new #21 form-mutation target fails compilation
failure is caused by absent approved mutation ABI
```

No production header/source scaffold may be added before this RED is proven.

## 24. Required identity/discovery tests

The mutation suite must lock:

- editor form snapshot matches ordinary snapshot semantics;
- editor form snapshot survives editor drop;
- historical snapshot is unchanged after later mutation;
- valid field-ref discovery;
- repeated discovery of same field returns same ref;
- different fields return different refs;
- wrong editor ref -> `ARGUMENT`;
- forged ref -> `ARGUMENT`;
- annotation-ref bits passed as form ref -> `ARGUMENT`;
- old ref used after reopen/new editor -> `ARGUMENT`;
- ref survives editor form snapshots;
- ref survives editor PDF output snapshots;
- ref survives annotation mutations.

## 25. Required Text tests

Lock:

- missing -> text;
- text -> missing;
- empty text distinct from missing;
- valid UTF-8;
- invalid UTF-8 -> `ARGUMENT`;
- embedded NUL -> `ARGUMENT`;
- Multiline supported;
- Password supported;
- Comb supported;
- RichText -> `UNSUPPORTED`;
- FileSelect -> `UNSUPPORTED`.

## 26. Required Checkbox/Radio tests

Lock:

- missing value;
- explicit Off;
- OPTION selection;
- switching selected option;
- Radio multi-Widget field;
- repeated same on-state Widgets;
- Missing vs Off preserved;
- NoToggleToOff does not block programmatic Off/Missing;
- RadiosInUnison does not redefine API semantics;
- raw `/AP` objects/bytes remain unchanged;
- `/AS` exactly follows selected field option.

## 27. Required Combo/List tests

Combo:

- OPTION assignment;
- editable custom UTF8;
- editable custom empty UTF8;
- non-editable custom UTF8 -> `ARGUMENT`;
- duplicate export values remain unambiguous via OPTION index and `/I`;
- custom assignment removes local `/I`.

List:

- single OPTION;
- single MISSING;
- single PRESENT/0 -> `ARGUMENT`;
- multi PRESENT/0 explicit empty selection;
- multi one option;
- multi many options;
- duplicate indices -> `ARGUMENT`;
- caller selection order preserved in `/V`;
- `/I` exactly matches option indices.

## 28. Required group/inheritance tests

Lock:

- canonical `/V` written at logical group head;
- same-group descendant local `/V` overrides removed;
- canonical `/I` ownership for Choice;
- same-group descendant local `/I` overrides removed;
- external inherited `/V` may be overridden by PRESENT;
- external inherited `/V` cannot be removed to MISSING -> `UNSUPPORTED`;
- sibling field semantic value remains unchanged.

## 29. Required state/capability tests

Lock:

- ReadOnly -> `STATE` after valid command validation;
- Required does not block missing/empty/off;
- NoExport does not block mutation;
- PushButton -> `UNSUPPORTED`;
- Signature -> `UNSUPPORTED`;
- UNKNOWN -> `UNSUPPORTED`;
- XFA present -> `UNSUPPORTED`;
- NeedAppearances true -> `UNSUPPORTED`;
- malformed NeedAppearances -> `FORMAT`.

## 30. Required no-execution tests

Deterministic fixtures must contain visible sentinel side effects for:

- Keystroke;
- Validate;
- Format;
- Calculate;
- Widget activation JavaScript/action;
- AcroForm `/CO` calculation order.

After every value mutation:

```text
no trigger/action executed
sentinel state unchanged
unrelated field unchanged
```

Tests must not need `pdf_update_page()` or an equivalent page-wide form-runtime pass to make the mutation correct.

## 31. Required appearance tests

Checkbox/Radio:

- `/AP` unchanged;
- `/AS` changed correctly.

Text/Combo:

- target Widget appearance is refreshed;
- unrelated Widget appearance/value remains unchanged;
- historical immutable output remains unchanged.

List:

- `/V` and `/I` exact;
- target Widget resynthesis completes;
- no assertion requires pixel-level selected-row highlighting.

## 32. Required atomicity tests

Deterministic test-only fault injection must cover at least:

```text
after canonical semantic /V,/I write
after first Widget state update
during/after first Text/Choice appearance refresh before commit
```

For each injected failure:

- setter returns failure;
- editor form snapshot equals pre-call semantic state;
- new editor output represents pre-call state;
- previously published output is unchanged;
- field ref remains valid;
- editor remains reusable;
- a subsequent valid mutation succeeds.

## 33. Required no-op tests

At minimum:

- same Text value;
- same Checkbox/Radio state;
- same Combo option;
- same multi-list sequence.

Each returns `OK` and produces byte-identical editor output before and after.

## 34. Round-trip correctness oracle

Successful mutation tests use the public semantic surface as the primary correctness oracle:

```text
begin editor
   -> editor form snapshot
   -> discover field/ref
   -> set_values
   -> editor form snapshot asserts new semantic state
   -> pdf_edit_snapshot
   -> open output as new extractpdf_document
   -> extractpdf_document_form
   -> assert same semantic state
```

This proves consistency across live editor state, serialization, reopen, and immutable parsing.

Raw `/V`, `/I`, `/AS`, and `/AP` assertions are supplementary representation tests and do not replace public round-trip tests.

## 35. Cross-platform gate

The final frozen feature SHA must pass:

```text
Linux strict static   21/21
Linux ASan/UBSan      21/21
macOS                 21/21
Windows DLL           21/21
```

Windows evidence must show that `extractpdf.dll` and `extractpdf_test_pdf_form_mutation.exe` are actually built and that `extractpdf.pdf_form_mutation` runs as test 21/21.

After same-SHA feature proof, integration still requires explicit authorization, exact-head merge, and an integrated-master push workflow on the resulting merge SHA before issue #46 can close.

## 36. Explicit non-goals

Form Value Mutation V1 does not add:

- field creation/deletion;
- field rename/reparent;
- Widget creation/deletion/move;
- field flag `/Ff` mutation;
- option `/Opt` mutation;
- default value `/DV` mutation;
- `/T` or `/TU` mutation;
- `/MaxLen` mutation;
- form reset;
- form submission;
- JavaScript/event/calculation runtime;
- signature signing/clearing/verification details;
- rich-text `/RV` mutation;
- FileSelect mutation;
- NeedAppearances rewriting;
- XFA mutation;
- flattening/baking;
- custom List selected-row appearance renderer;
- public Widget refs;
- public option refs;
- persistent field identity across editor sessions.

## 37. Implementation boundary

This document defines architecture only. No implementation or RED may begin until this committed design is reviewed and approved.

After design approval, the next artifact is a separate TDD implementation plan created with the project planning workflow. The plan must preserve the strict sequence:

```text
compile RED
-> incremental GREEN slices
-> Linux static + ASan/UBSan 21/21
-> same-SHA macOS + Windows DLL full-ci
-> fresh scope/review gate
-> explicit integration authorization
-> exact-head merge
-> integrated-master push proof
-> close #46 / update roadmap
```
