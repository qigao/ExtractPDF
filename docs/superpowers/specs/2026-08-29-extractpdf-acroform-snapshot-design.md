# ExtractPDF AcroForm Snapshot V1 Design

Status: approved architecture, self-reviewed design for issue #43 under roadmap #2.

Base integrated master: `0e4b769753215725797a557c4f18c4654e444e30`.

Design branch: `feat/acroform-snapshot`.

Pinned PDF engine: MuPDF 1.28.2.

## 1. Purpose and scope

AcroForm Snapshot V1 adds a read-only, document-independent representation of PDF AcroForm logical fields and page Widget instances.

The design separates two concepts:

- **logical field group** — semantic state: type, flags, fully-qualified name, label, value, options;
- **Widget instance** — one visual/page occurrence owned by one logical field group.

A Widget never owns an independent field value. One logical field may have zero, one, or many Widgets.

This issue is observation only. It does not add form mutation, mutation refs, field/Widget creation or deletion, JavaScript/event execution, signing, or flattening.

XFA is outside V1.

## 2. Architectural placement

```text
extractpdf_document
  ├─ page/content snapshots
  ├─ metadata / outline
  ├─ annotation snapshots
  └─ AcroForm snapshot
       ├─ terminal logical field groups[]
       │    ├─ type / flags
       │    ├─ fully-qualified name / label
       │    ├─ typed value[]
       │    └─ option[]
       └─ Widget instances[]
            ├─ field_index
            ├─ page_index
            ├─ Fitz page-space bounds
            ├─ raw annotation flags
            └─ button option mapping
```

A later Form Value Mutation V1 will reuse the same strict semantic parser inside the already-integrated `extractpdf_pdf_edit`. That later mutation work is not part of #43.

## 3. Public snapshot lifetime

Add:

```c
typedef struct extractpdf_form extractpdf_form;

extractpdf_status extractpdf_document_form(
    extractpdf_document *document,
    extractpdf_form **out_form);

void extractpdf_drop_form(extractpdf_form *form);
```

Rules:

- `out_form` is required and is set to `NULL` before later validation;
- success publishes one immutable non-NULL snapshot;
- all public strings, values, options, and Widget records are deep-owned by the snapshot;
- source document/page lifetime is not retained publicly;
- source may be closed immediately after successful extraction;
- snapshot data remains valid until `extractpdf_drop_form()`;
- `extractpdf_drop_form(NULL)` is a no-op;
- extraction never mutates, repairs, canonicalizes, dirties, or recalculates the source PDF;
- any failure publishes no snapshot and no partial prefix.

## 4. Empty and unsupported input

For a valid PDF:

```text
no /AcroForm
    -> OK + non-NULL empty snapshot

/AcroForm dictionary, no /Fields
    -> OK + non-NULL empty snapshot

/AcroForm dictionary, /Fields []
    -> OK + non-NULL empty snapshot
```

Empty means:

```text
field_count = 0
widget_count = 0
```

A non-PDF source returns:

```text
EXTRACTPDF_ERROR_UNSUPPORTED
*out_form = NULL
```

A present malformed AcroForm is `FORMAT`, never silently treated as empty.

## 5. Public ABI

### 5.1 Field types

```c
typedef enum extractpdf_form_field_type {
    EXTRACTPDF_FORM_FIELD_UNKNOWN = 0,
    EXTRACTPDF_FORM_FIELD_PUSH_BUTTON = 1,
    EXTRACTPDF_FORM_FIELD_CHECKBOX = 2,
    EXTRACTPDF_FORM_FIELD_RADIO_BUTTON = 3,
    EXTRACTPDF_FORM_FIELD_TEXT = 4,
    EXTRACTPDF_FORM_FIELD_COMBO_BOX = 5,
    EXTRACTPDF_FORM_FIELD_LIST_BOX = 6,
    EXTRACTPDF_FORM_FIELD_SIGNATURE = 7
} extractpdf_form_field_type;
```

Known classification uses effective `/FT` plus effective `/Ff`:

```text
/FT /Btn + PushButton flag -> PUSH_BUTTON
/FT /Btn + Radio flag      -> RADIO_BUTTON
/FT /Btn                    -> CHECKBOX
/FT /Tx                     -> TEXT
/FT /Ch + Combo flag        -> COMBO_BOX
/FT /Ch                     -> LIST_BOX
/FT /Sig                    -> SIGNATURE
valid unrecognized /FT      -> UNKNOWN
```

A terminal logical field group without effective `/FT` is `FORMAT`.

A present `/FT` that is not a PDF Name is `FORMAT`.

For malformed contradictory type flags that would produce ambiguous subtype semantics, extraction returns `FORMAT` rather than choosing an arbitrary subtype.

### 5.2 Value presence and value kind

```c
typedef enum extractpdf_form_value_presence {
    EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    EXTRACTPDF_FORM_VALUE_MISSING = 1,
    EXTRACTPDF_FORM_VALUE_PRESENT = 2
} extractpdf_form_value_presence;

typedef enum extractpdf_form_value_kind {
    EXTRACTPDF_FORM_VALUE_UTF8 = 1,
    EXTRACTPDF_FORM_VALUE_OPTION = 2
} extractpdf_form_value_kind;

typedef struct extractpdf_form_value_info {
    size_t struct_size;
    extractpdf_form_value_kind kind;
    size_t option_index;
} extractpdf_form_value_info;
```

`NOT_APPLICABLE` is used when the ordinary value surface intentionally does not model a field's value (PushButton, Signature, UNKNOWN).

`MISSING` means no effective value exists.

`PRESENT` includes present-empty text, explicit button `/Off`, and explicit empty selection.

For a UTF8 value, `option_index == SIZE_MAX`.

For an OPTION value, `option_index < option_count`.

### 5.3 Option kinds

```c
typedef enum extractpdf_form_option_kind {
    EXTRACTPDF_FORM_OPTION_BUTTON_STATE = 1,
    EXTRACTPDF_FORM_OPTION_CHOICE = 2
} extractpdf_form_option_kind;

typedef struct extractpdf_form_option_info {
    size_t struct_size;
    extractpdf_form_option_kind kind;
} extractpdf_form_option_info;
```

BUTTON_STATE is a semantic checkbox/radio state derived from private PDF Name appearance states. Raw PDF Name bytes are never exposed as UTF-8 public API.

CHOICE models Combo/List `/Opt` export/display strings.

### 5.4 Field info

```c
typedef struct extractpdf_form_field_info {
    size_t struct_size;
    extractpdf_form_field_type type;
    uint32_t flags;
    extractpdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} extractpdf_form_field_info;
```

`flags` preserves the complete non-negative 32-bit effective `/Ff` value.

`is_multiselect` is meaningful only for LIST_BOX and is zero otherwise.

`is_signed` is meaningful only for SIGNATURE and is zero otherwise.

For UNKNOWN:

```text
value_presence = NOT_APPLICABLE
value_count = 0
option_count = 0
is_multiselect = 0
is_signed = 0
```

Widgets belonging to UNKNOWN fields are still published if their structure is otherwise valid.

### 5.5 Widget info

```c
typedef struct extractpdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_form_widget_info;
```

`button_option_index == SIZE_MAX` for non-button Widgets and for a valid checkbox/radio Widget with no usable non-Off state.

## 6. Public accessors

```c
extractpdf_status extractpdf_form_field_count(
    const extractpdf_form *form,
    size_t *out_count);

extractpdf_status extractpdf_form_field_get_info(
    const extractpdf_form *form,
    size_t field_index,
    extractpdf_form_field_info *out_info);

extractpdf_status extractpdf_form_field_name(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

extractpdf_status extractpdf_form_field_label(
    const extractpdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);

extractpdf_status extractpdf_form_field_value_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    extractpdf_form_value_info *out_info);

extractpdf_status extractpdf_form_field_value_utf8(
    const extractpdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size);

extractpdf_status extractpdf_form_field_option_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    extractpdf_form_option_info *out_info);

extractpdf_status extractpdf_form_field_option_export(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

extractpdf_status extractpdf_form_field_option_display(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);

extractpdf_status extractpdf_form_widget_count(
    const extractpdf_form *form,
    size_t *out_count);

extractpdf_status extractpdf_form_widget_get_info(
    const extractpdf_form *form,
    size_t widget_index,
    extractpdf_form_widget_info *out_info);
```

No field-to-widget convenience array is added in V1: `widget_info.field_index` is the canonical relation.

## 7. Field-tree preflight and logical field groups

This is the core structural contract.

### 7.1 Root structure

```text
/AcroForm absent
    -> empty snapshot

/AcroForm present but non-dictionary
    -> FORMAT

/Fields absent
    -> empty snapshot

/Fields present but non-array
    -> FORMAT
```

Every `/Fields` member must resolve to a dictionary.

### 7.2 Parent/Kids graph

For every field-tree node:

- `/Kids`, when present, must be an array;
- every field child must resolve to a dictionary;
- a child field's `/Parent` must be exactly the traversed parent;
- a top-level field must not have an external `/Parent`;
- a field node may be reached from only one field-tree path;
- cycle -> `FORMAT`;
- repeated node -> `FORMAT`;
- otherwise valid field-tree depth >256 -> `UNSUPPORTED`.

A child field that participates in Parent/Kids must be identifiable consistently as the same PDF object. A Widget that must be reconciled between field tree and page `/Annots` must be an indirect object; a direct Widget cannot satisfy cross-structure identity and is `FORMAT`.

### 7.3 `/T` creates logical-name boundaries

A PDF field child is allowed to omit `/T` when it represents another appearance of the same field. Therefore public logical fields are **field groups**, not simply individual leaf dictionaries.

Grouping rule:

```text
field node with /T
    -> starts a new logical-name group boundary

field descendant without /T
    -> remains in nearest ancestor logical group

unnamed top-level field with no named ancestor
    -> starts one anonymous logical group
```

Descendants in the same group may carry appearance/Widget structure but do not create a second public field.

A group becomes a public terminal logical field only when it has no descendant child field that introduces a new `/T` logical-name boundary.

A named group that has named child groups is a non-terminal namespace/container and is not published.

A logical group that simultaneously contains Widget instances and named child logical groups is `FORMAT`: the same group cannot be both a terminal interactive field and a namespace container.

### 7.4 Same-group semantic consistency

Field nodes in one logical group represent the same semantic field. Their effective semantic state must agree.

For nodes in the same group that participate in field semantics or own Widgets, effective `/FT`, `/Ff`, and `/V` must normalize to the same semantic values. Conflicting overrides inside one same-name group are `FORMAT`.

A same-group descendant may not introduce conflicting semantic option/selection definitions that would make `/Opt` or `/I` ambiguous for the group. Such conflicts are `FORMAT`.

This allows appearance-only child fields while refusing multiple semantic value owners for one public logical field.

## 8. Field names and labels

### 8.1 Fully-qualified name

A field name is constructed from `/T` components that introduce logical-name boundaries.

Components are concatenated with `.` in ancestor order.

A partial `/T` containing a literal period is `FORMAT` because period is the fully-qualified-name separator.

Missing `/T` does not add a component and keeps the node in its current logical group.

An anonymous top-level group with no `/T` has a missing public name (`NULL + 0`).

A present-empty `/T` creates a present-empty component and is distinct from missing `/T`; the parser preserves that distinction internally. If the resulting public fully-qualified name is empty, the accessor returns a non-NULL pointer with size 0.

Two distinct public logical fields with the same non-empty fully-qualified name are `FORMAT`.

Field index remains snapshot-local identity even when a public name is missing/empty.

### 8.2 Label

`extractpdf_form_field_label()` exposes effective alternate field name `/TU` resolved through the validated group/parent graph.

It does **not** fall back to `/T` or the fully-qualified field name. This preserves missing label vs actual name.

String contract for name and label:

```text
missing        -> OK + NULL + 0
present empty  -> OK + non-NULL + 0
present value  -> OK + non-NULL + UTF-8 byte size
```

All returned strings are snapshot-owned.

Malformed non-string `/T` or `/TU` where observed is `FORMAT`.

## 9. Validated inheritance

Inheritance is resolved only over the already-validated Parent graph.

The immutable parser does not use a repairing parent walker as the authority for public semantics.

The shared private semantic representation records both:

```text
effective value
+
which field-tree node supplied it
```

At minimum V1 resolves effective:

- `/FT`;
- `/Ff`;
- `/V`;
- `/TU` for the public label contract.

This provenance is intentionally retained so a future editor can detect inherited-value removal ambiguity without inventing a different field model.

## 10. Field flag validation

Effective `/Ff`:

```text
missing -> 0
integer in [0, UINT32_MAX] -> exact raw uint32_t
otherwise -> FORMAT
```

No signed narrowing is allowed.

Type-specific contradictory flag combinations that make field subtype or value cardinality ambiguous are `FORMAT`.

## 11. Value normalization

### 11.1 Text

```text
no effective /V
    -> MISSING, value_count=0

/V valid PDF text string, empty
    -> PRESENT, one UTF8 value, size 0

/V valid PDF text string, non-empty
    -> PRESENT, one UTF8 value

/V any other object type, including stream
    -> FORMAT
```

Stream `/V` is deliberately not normalized by mutating the source.

### 11.2 Checkbox and radio

Options come from associated Widget normal appearance state dictionaries.

```text
no effective /V
    -> MISSING, 0 values

/V /Off
    -> PRESENT, 0 values

/V PDF Name matching one normalized field BUTTON_STATE option
    -> PRESENT, one OPTION value

/V non-name or unknown state
    -> FORMAT
```

Radio may have many semantic button options. Checkbox may technically have one or more deduplicated button states across repeated Widgets; the value must still resolve to exactly one field option when selected.

### 11.3 Combo box

For a normal Choice option selection:

```text
/V matching one option export value
    -> PRESENT, one OPTION
```

For an editable Combo:

```text
/V valid text string not matching an option
    -> PRESENT, one UTF8 custom value
```

For a non-editable Combo, an unmatched custom value is `FORMAT`.

If `/I` is present it is preferred option identity and must be structurally valid and consistent with `/V`.

Duplicate export values without valid `/I` disambiguation are `FORMAT`.

### 11.4 List box

Single-select:

```text
missing -> MISSING / 0
selected option -> PRESENT / one OPTION
explicit empty selection -> PRESENT / 0
```

Multi-select:

```text
missing -> MISSING / 0
selected options -> PRESENT / N OPTION values
explicit empty selection -> PRESENT / 0
```

When `/I` is present:

- it must be an array of integers;
- every index must be unique and in range;
- its cardinality must obey single/multiselect semantics;
- selected indices must agree with `/V` export values.

Contradiction or ambiguity is `FORMAT`.

### 11.5 PushButton

```text
value_presence = NOT_APPLICABLE
value_count = 0
option_count = 0
```

### 11.6 Signature

```text
value_presence = NOT_APPLICABLE
value_count = 0
option_count = 0
```

Signed-state normalization is exact:

```text
no effective /V
    -> is_signed = 0

/V is dictionary and /Type is absent
    -> is_signed = 1

/V is dictionary and /Type /Sig
    -> is_signed = 1

/V is dictionary and /Type is a different name
    -> FORMAT

/V is any non-dictionary object
    -> FORMAT
```

V1 does not expose signer/certificate/digest/reason/location/date details.

### 11.7 Unknown

UNKNOWN does not guess a type-specific value model:

```text
value_presence = NOT_APPLICABLE
value_count = 0
option_count = 0
```

## 12. Choice options

Choice `/Opt`:

```text
/Opt missing
    -> option_count = 0

entry is PDF text string
    -> export = decoded entry
    -> display = decoded entry

entry is two-element array of PDF text strings
    -> export = decoded element 0
    -> display = decoded element 1

anything else
    -> FORMAT
```

Public accessors:

```c
extractpdf_status extractpdf_form_field_option_export(...);
extractpdf_status extractpdf_form_field_option_display(...);
```

For CHOICE options they return snapshot-owned UTF-8.

For BUTTON_STATE options they reset outputs and return `UNSUPPORTED`.

A choice field with present selected values but zero options is only valid for an editable Combo custom UTF8 value or an explicit empty selection. Any OPTION-style selected value without a corresponding option is `FORMAT`.

## 13. Button-state options and Widget `/AP/N`

For checkbox/radio Widgets, inspect raw normal appearance `/AP/N`.

A usable button Widget state requires `/AP/N` to be a dictionary containing at most one non-`Off` PDF Name key.

Rules:

```text
/AP absent, /AP/N absent, or /AP/N has zero non-Off states
    -> Widget button_option_index = SIZE_MAX

/AP/N has exactly one non-Off state
    -> that private PDF Name is the Widget's button state

/AP/N has more than one non-Off state
    -> FORMAT

/AP/N present but not a dictionary
    -> FORMAT
```

Field BUTTON_STATE option order is:

```text
public Widget order
    -> first usable non-Off state
    -> private bytewise deduplication
```

If the effective field `/V` selects a non-Off state, that state must exist among the field's normalized options. A selected state with no matching Widget option is `FORMAT`.

A Widget with `SIZE_MAX` is therefore valid only when it is not needed to represent the currently selected non-Off state.

Raw PDF Name bytes remain private and are not exposed through export/display accessors.

## 14. Widget discovery and page reconciliation

Widget order is:

```text
page 0 raw /Annots order
page 1 raw /Annots order
...
```

Within each page:

- missing `/Annots` -> empty page Widget list;
- non-array `/Annots` -> empty page Widget list, matching the existing tolerant page-annotation collection policy;
- non-dictionary `/Annots` members are ignored for Widget discovery;
- dictionary members with `/Subtype /Widget` participate in reconciliation;
- other annotations do not participate.

A published Widget must satisfy both:

```text
reachable from exactly one terminal logical field group
AND
appears exactly once in exactly one page /Annots array
```

Errors:

```text
page Widget not reachable from field model -> FORMAT
field-model Widget absent from all pages -> FORMAT
same Widget appears twice on one page -> FORMAT
same Widget appears on multiple pages -> FORMAT
Widget /P exists and disagrees with containing page -> FORMAT
direct Widget unable to provide stable cross-structure identity -> FORMAT
```

A terminal logical field with zero Widgets is valid.

`field_info.widget_count` equals the number of published Widgets whose `field_index` points to that field.

## 15. Widget geometry and flags

Widget `/Rect`:

- exactly four numeric entries;
- all finite;
- malformed/non-finite -> `FORMAT`.

Rect coordinates are transformed from PDF page user space into the same Fitz page-space coordinate contract already used by current page/link/annotation APIs.

Widget annotation `/F`:

```text
missing -> 0
integer [0, UINT32_MAX] -> exact uint32_t
otherwise -> FORMAT
```

No signed narrowing.

## 16. Ownership and output resets

### 16.1 Counts

For count accessors:

- reset non-NULL `*out_count = 0` first;
- then require `out_count`;
- then validate snapshot/index semantics.

### 16.2 Strings

For string accessors:

- reset each non-NULL output independently: pointer `NULL`, size `0`;
- then require both output pointers;
- then validate field/value/option kind and index.

Missing semantic string returns `OK + NULL + 0`.

Present-empty returns `OK + non-NULL + 0`.

### 16.3 Info structs

`out_info` is required.

`struct_size` uses the existing minimum-known-size forward-compatible rule:

- too small -> `ARGUMENT`;
- exact current size -> accepted;
- larger -> accepted, unknown trailing bytes ignored;
- once minimum size is accepted, known output fields are reset before later semantic validation.

For `extractpdf_form_value_info`, UTF8 resets `option_index` to `SIZE_MAX`.

## 17. Error model

```text
ARGUMENT
    null required pointer
    invalid public handle
    out-of-range public index
    too-small struct_size

UNSUPPORTED
    non-PDF source
    structurally valid field depth >256
    UTF8 accessor requested for OPTION value
    export/display requested for BUTTON_STATE option

FORMAT
    malformed AcroForm/field graph
    ambiguous logical field grouping
    malformed names/types/flags/value/options/selection
    inconsistent same-group semantic state
    malformed or unreconciled Widget
    malformed Widget geometry/flags

NOMEM
    ExtractPDF allocation failure

MUPDF or mapped engine status
    unexpected engine failure not more specifically classified
```

No failure publishes a partial snapshot.

## 18. Immutable-source constraints

`extractpdf_document_form()` is pure observation at the ExtractPDF contract level.

It must not:

- call an API that writes normalized field values back into source dictionaries;
- repair Parent/Kids structure;
- update Widget appearance;
- dirty fields;
- request synthesis;
- calculate forms;
- execute JavaScript;
- execute Keystroke, Validate, Calculate, Format, activation, submit, or reset actions.

MuPDF convenience helpers may be used only when their required operation is proven side-effect-free.

In particular:

- `pdf_field_value()` is not the value contract because MuPDF may normalize stream `/V` by writing a decoded string back to the field;
- generic inheritable getters are not the structural authority because this design requires inheritance over the already-validated, non-repairing Parent graph.

## 19. Private implementation boundary

Recommended units:

```text
src/pdf_form_common.c/.h
    AcroForm root preflight
    Parent/Kids graph validation
    logical field-group construction
    validated inheritance + provenance
    field classification
    value normalization
    option normalization
    Widget semantic ownership/mapping
    shared semantic model for later editor reuse

src/pdf_form.c
    page Widget reconciliation
    immutable deep-copy snapshot
    public accessors
    drop
```

Planning may split these further if needed for focused responsibility, but immutable snapshot and future editor must not implement separate field/value parsers.

Forms do not become a special public annotation subtype.

## 20. Security behavior

Snapshot extraction never executes embedded behavior.

Encrypted PDFs are observed only through the existing authenticated `extractpdf_open()` source handle; Snapshot V1 adds no new encryption policy.

Signed PDFs may be observed. Signature fields expose only normalized `is_signed`.

No signing, clearing, validation, incremental preservation, or mutation occurs here.

## 21. Explicit non-goals

V1 does not include:

- XFA;
- form value mutation;
- `extractpdf_form_field_ref`;
- persistent field/Widget identity;
- field/Widget create/delete/move/rename;
- field flag mutation;
- option mutation;
- `/DV` mutation;
- rich text `/RV`;
- button style/caption editing;
- reset/submit actions;
- JavaScript/event/calculation execution;
- signing/clearing/validation details;
- encrypted editing;
- signed-PDF editing;
- NeedAppearances rewriting;
- form flattening/baking.

Flattening remains Phase 6.

## 22. Required deterministic RED matrix

The implementation plan must lock every contract below before production behavior.

### 22.1 Strict first RED

- new test references the approved public form ABI before production declarations/implementation exist;
- all pre-existing targets continue to build;
- only the new form target fails at the intentional missing-ABI compile boundary.

### 22.2 Empty and unsupported

- non-PDF -> `UNSUPPORTED`, output NULL;
- no AcroForm -> non-NULL empty snapshot;
- AcroForm without Fields -> non-NULL empty snapshot;
- empty Fields -> non-NULL empty snapshot.

### 22.3 Logical field-group semantics

- named parent + named child -> parent namespace not published, child field published;
- named parent + unnamed appearance child -> one logical field, not two;
- multiple unnamed appearance children -> one logical field;
- anonymous top-level terminal group -> missing name;
- present-empty `/T` -> present-empty name;
- partial `/T` containing period -> FORMAT;
- group with Widgets plus named child logical group -> FORMAT;
- same-group conflicting effective semantic value -> FORMAT;
- terminal field with zero Widgets;
- field that is itself a merged Widget;
- field with separate Widget Kids.

### 22.4 Field order, names, labels, inheritance

- terminal logical-field DFS order;
- fully-qualified names;
- duplicate non-empty full names -> FORMAT;
- label missing vs present-empty;
- label does not fall back to field name;
- inherited FT;
- inherited Ff including value above `INT_MAX`;
- inherited V;
- source remains unchanged after extraction.

### 22.5 Values

- Text missing / empty / UTF-8 value;
- stream Text V -> FORMAT without write-back;
- Checkbox missing / Off / selected state;
- Radio mapping across multiple Widgets;
- Combo option selection;
- editable Combo custom UTF-8;
- non-editable Combo custom value -> FORMAT;
- List single selection;
- List multiselect;
- List explicit empty selection;
- PushButton NOT_APPLICABLE;
- Signature unsigned (missing V);
- Signature signed dict without Type;
- Signature signed dict Type Sig;
- Signature malformed non-dict V -> FORMAT;
- Signature dict Type non-Sig -> FORMAT;
- UNKNOWN -> NOT_APPLICABLE.

### 22.6 Choice options and selection identity

- missing Opt;
- single-string option export==display;
- export/display pair;
- malformed Opt entry;
- valid I disambiguates duplicate export values;
- duplicate export without valid I -> FORMAT;
- I non-array / non-integer -> FORMAT;
- I out of range -> FORMAT;
- duplicate I index -> FORMAT;
- I contradicts V -> FORMAT.

### 22.7 Button appearance states

- AP/N absent -> button_option_index SIZE_MAX when no selected state depends on it;
- AP/N zero non-Off -> SIZE_MAX;
- AP/N exactly one non-Off -> OPTION mapping;
- AP/N more than one non-Off -> FORMAT;
- AP/N present non-dict -> FORMAT;
- field selected state absent from normalized button options -> FORMAT;
- repeated Widgets sharing the same private state deduplicate to one field option;
- option order follows public Widget order;
- BUTTON_STATE export/display accessor -> UNSUPPORTED with reset outputs.

### 22.8 Widget reconciliation/order

- page+Annots global Widget order;
- missing page Annots tolerated as empty;
- non-array page Annots tolerated as empty;
- non-dict Annots members ignored;
- orphan page Widget -> FORMAT;
- field Widget absent from pages -> FORMAT;
- duplicate Widget on one page -> FORMAT;
- same Widget on multiple pages -> FORMAT;
- direct Widget cross-structure identity -> FORMAT;
- P mismatch -> FORMAT;
- correct owning field_index;
- correct field widget_count.

### 22.9 Widget geometry/flags

- Fitz page-space bounds;
- malformed Rect;
- non-finite Rect;
- missing F -> 0;
- full uint32 F above INT_MAX;
- negative/non-integer/out-of-range F -> FORMAT.

### 22.10 Structural failure

- AcroForm non-dict;
- Fields non-array;
- non-dict field member;
- Kids non-array;
- non-dict child;
- cycle;
- repeated node;
- broken Parent;
- illegal root Parent;
- depth 257 -> UNSUPPORTED;
- missing effective FT;
- valid unknown FT -> UNKNOWN;
- malformed FT/Ff;
- deterministic late-malformed fixture proves no partial publication.

### 22.11 Lifetime and side-effect proof

- names/labels/values/options survive source close;
- complete snapshot survives source close;
- repeated extraction leaves source-observed behavior unchanged;
- passive extraction does not trigger JavaScript/actions.

### 22.12 Cross-platform gate

Target suite progression:

```text
current integrated suite: 19 CTests
AcroForm Snapshot V1:     20 CTests
```

Before integration, one frozen exact feature head must pass:

- Linux static build + 20/20;
- Linux ASan/UBSan build + 20/20;
- same-head macOS configure/build/test + 20/20;
- same-head Windows DLL configure/build/test + 20/20;
- Windows shared-library export/registration of the complete new form ABI.

## 23. Delivery decomposition and gate

Issue #43 is Slice A only:

```text
approved committed design
    ↓
committed TDD implementation plan
    ↓
strict compile RED
    ↓
semantic parser + immutable snapshot GREEN
    ↓
exact-head Linux static + sanitizers
    ↓
same-head macOS + Windows DLL full-ci
    ↓
fresh scope/review check
    ↓
explicit integration authorization
    ↓
integrated-master push proof
```

Only after #43 is integrated may a separate Form Value Mutation V1 issue be created.

That later slice may reuse:

- this logical field-group model;
- this strict parser and value/option normalization;
- existing `extractpdf_pdf_edit`;
- session-local field refs;
- one journal operation per set-values call;
- local Widget appearance refresh;
- no JavaScript/Validate/Calculate/Format execution.

No mutation issue, mutation RED, mutation fixture, or mutation production code is part of this design step.

## 24. Design acceptance criteria

This spec is complete when:

1. logical field groups and Widget instances are separate;
2. unnamed appearance-only child fields cannot become duplicate public logical fields;
3. public ABI is fully specified;
4. missing/empty/Off/multiselect/custom-combo/signature semantics are deterministic;
5. Parent/Kids validation, grouping, inheritance, and Widget reconciliation have explicit malformed policy;
6. immutable extraction cannot repair or write back source state;
7. ownership, ordering, identity, struct-size, and output-reset rules are explicit;
8. every public semantic contract has a deterministic RED boundary;
9. Snapshot V1 remains separate from future mutation;
10. cross-platform exact-head and explicit integration gates are preserved.
