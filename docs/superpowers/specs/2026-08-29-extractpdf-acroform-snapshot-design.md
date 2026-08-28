# ExtractPDF AcroForm Snapshot V1 Design

Status: approved architecture, committed design for issue #43 under roadmap #2.

Base integrated master: `0e4b769753215725797a557c4f18c4654e444e30`.

Design branch: `feat/acroform-snapshot`.

Pinned PDF engine: MuPDF 1.28.2.

## 1. Purpose

AcroForm Snapshot V1 adds a read-only, document-independent representation of PDF form fields and Widget instances.

The design deliberately models two different PDF concepts separately:

- a **logical field**, which owns semantic state such as type, flags, name, value, and options;
- a **Widget instance**, which owns page placement and visual interaction geometry for one logical field.

A Widget is not a value owner. One logical field may have zero, one, or many Widget instances. Radio groups and repeated fields across pages are the primary cases where this distinction is required for correctness.

This slice is observation only. It does not add form mutation, field mutation identity, field creation/deletion, Widget creation/deletion, JavaScript execution, signing, or form flattening.

## 2. Architectural placement

The public object model becomes:

```text
extractpdf_document
  ├─ page/content snapshots
  ├─ metadata / outline
  ├─ annotation snapshots
  └─ AcroForm snapshot
       ├─ logical terminal fields[]
       │    ├─ type / flags
       │    ├─ fully-qualified name / label
       │    ├─ typed value[]
       │    └─ option[]
       └─ widget instances[]
            ├─ field_index
            ├─ page_index
            ├─ Fitz page-space bounds
            ├─ raw annotation flags
            └─ button option mapping
```

A future Form Value Mutation V1 will reuse the same semantic parser inside the already-integrated `extractpdf_pdf_edit` isolated editor. That mutation work is explicitly not part of issue #43.

## 3. Public handle and snapshot lifetime

Add:

```c
typedef struct extractpdf_form extractpdf_form;
```

Extraction:

```c
extractpdf_status extractpdf_document_form(
    extractpdf_document *document,
    extractpdf_form **out_form);
```

Lifetime rules:

- `out_form` is required and is set to `NULL` before later validation.
- successful extraction publishes one immutable, non-NULL snapshot;
- all public strings and arrays are deep-copied into the snapshot;
- the source document may be closed immediately after successful extraction;
- the snapshot remains valid until `extractpdf_drop_form()`;
- extraction never changes source PDF objects or source public behavior;
- publication is atomic: a malformed later field or Widget never exposes an earlier valid prefix.

Drop:

```c
void extractpdf_drop_form(extractpdf_form *form);
```

Dropping `NULL` is allowed and is a no-op, consistent with existing drop helpers.

## 4. Empty and unsupported documents

For a valid PDF:

```text
no /AcroForm
    -> OK + non-NULL empty snapshot

/AcroForm dictionary, no /Fields
    -> OK + non-NULL empty snapshot

/AcroForm dictionary, /Fields []
    -> OK + non-NULL empty snapshot
```

An empty snapshot has:

```text
field_count = 0
widget_count = 0
```

A non-PDF source returns:

```text
EXTRACTPDF_ERROR_UNSUPPORTED
*out_form = NULL
```

A present but malformed AcroForm structure is `EXTRACTPDF_ERROR_FORMAT`, not an empty snapshot.

## 5. Logical field types

Public field types are stable enum values:

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

Classification follows effective field type `/FT` plus effective field flags `/Ff`:

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

A terminal logical field without an effective `/FT` is malformed and returns `FORMAT`.

A present `/FT` that is not a PDF Name is malformed and returns `FORMAT`.

## 6. Field flags

Public field flags are exposed as raw `uint32_t`:

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

The parser preserves the full non-negative 32-bit `/Ff` range without signed narrowing.

Rules:

- missing effective `/Ff` -> `0`;
- present `/Ff` must be an integer in `[0, UINT32_MAX]`;
- negative, non-integer, or too-large values -> `FORMAT`.

`is_multiselect` is meaningful for LIST_BOX and reflects the effective Choice multiselect flag. It is zero for other field types.

`is_signed` is meaningful for SIGNATURE. It is zero for non-signature fields.

## 7. Field ordering and identity

Public fields are only **terminal logical fields**.

Intermediate namespace/container nodes in `/AcroForm/Fields` are not published as fields.

Field order is deterministic:

```text
raw /AcroForm/Fields array order
        ↓
depth-first traversal of child field nodes
        ↓
publish terminal logical fields in first-encounter order
```

Public `field_index` is snapshot-local only.

It is not:

- a PDF object number/generation;
- a fully-qualified field name;
- a persistent identifier;
- a future mutation handle;
- stable across save/reopen.

## 8. Field name and label

Accessors:

```c
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
```

Name is the field's fully-qualified name derived from the validated parent chain and partial field names `/T`.

Example:

```text
customer
  address
    city

-> customer.address.city
```

The fully-qualified name is descriptive data only, never public identity.

Duplicate non-empty fully-qualified names belonging to distinct terminal logical fields are `FORMAT` because they make field lookup semantics ambiguous.

Label is the effective alternate field name `/TU` when present.

String tri-state for both accessors:

```text
missing        -> OK + NULL + 0
present empty  -> OK + non-NULL + 0
present value  -> OK + non-NULL + UTF-8 byte size
```

All returned pointers are snapshot-owned and valid until `extractpdf_drop_form()`.

Invalid PDF text-string encoding or malformed required string object types return `FORMAT` during extraction.

## 9. Value presence model

Value presence is separate from the value payload:

```c
typedef enum extractpdf_form_value_presence {
    EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    EXTRACTPDF_FORM_VALUE_MISSING = 1,
    EXTRACTPDF_FORM_VALUE_PRESENT = 2
} extractpdf_form_value_presence;
```

Meaning:

```text
NOT_APPLICABLE
    ordinary value model does not apply to this field type

MISSING
    no effective /V exists

PRESENT
    effective value exists, including an empty string, empty selection,
    or an explicit /Off button state
```

This distinction is required to avoid collapsing:

- absent `/V`;
- empty text string;
- explicit empty multi-selection;
- explicit button `/Off`.

## 10. Typed field values

Value entries are tagged:

```c
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

Accessors:

```c
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
```

For `UTF8`, `option_index` is reset to `SIZE_MAX`.

For `OPTION`, `option_index` must be `< option_count`.

Calling `extractpdf_form_field_value_utf8()` for an OPTION value returns `UNSUPPORTED` after resetting the outputs.

Calling it for a UTF8 value returns the snapshot-owned UTF-8 bytes.

Out-of-range field/value indices return `ARGUMENT`.

## 11. Field-specific value normalization

### 11.1 Text

Effective `/V` rules:

```text
missing /V
    -> MISSING, value_count=0

/V is valid empty PDF string
    -> PRESENT, one UTF8 value of size 0

/V is valid non-empty PDF string
    -> PRESENT, one UTF8 value

/V any other object type, including stream
    -> FORMAT
```

A stream-valued `/V` is intentionally `FORMAT` in V1. The immutable reader must not invoke MuPDF normalization that decodes a stream then writes a string back into the source field dictionary.

### 11.2 Checkbox

Button semantic options are derived from Widget normal appearance states.

```text
no effective /V
    -> MISSING, 0 values

/V /Off
    -> PRESENT, 0 values

/V known non-Off button state
    -> PRESENT, one OPTION value

/V unknown/non-name state
    -> FORMAT
```

### 11.3 Radio button

Radio buttons use the same logical value representation as checkbox fields, but the field may have multiple semantic button options mapped from multiple Widgets.

One selected radio state is represented as one OPTION value.

An explicit `/Off` is `PRESENT` with zero selected values.

### 11.4 Combo box

Choice option export values define normal option identity.

```text
missing /V
    -> MISSING, 0 values

/V maps unambiguously to one option export value
    -> PRESENT, one OPTION value

editable combo and /V does not match an option
    -> PRESENT, one UTF8 custom value

non-editable combo and /V does not match an option
    -> FORMAT
```

A valid `/I` is preferred option identity when present and must agree with `/V`.

Ambiguous duplicate export values without a valid `/I` disambiguation are `FORMAT`; the parser never silently selects the first match.

### 11.5 List box

Single-select list:

```text
missing /V
    -> MISSING, 0 values

one selected option
    -> PRESENT, one OPTION value

explicit empty selection
    -> PRESENT, 0 values
```

Multi-select list:

```text
missing /V
    -> MISSING, 0 values

selected options
    -> PRESENT, 1..N OPTION values

explicit empty selection
    -> PRESENT, 0 values
```

A valid `/I` is preferred identity and all indices must be unique, in range, and consistent with `/V`.

Malformed `/I`, contradictory `/I` and `/V`, invalid value object forms, or ambiguous value mapping return `FORMAT`.

### 11.6 Push button

```text
value_presence = NOT_APPLICABLE
value_count = 0
```

Ordinary field `/V` is not exposed as a value for PushButton in V1.

### 11.7 Signature

```text
value_presence = NOT_APPLICABLE
value_count = 0
is_signed = 0 or 1
```

V1 does not expose signer details, validation state, signing time, reason, location, certificate information, or signature bytes.

## 12. Option model

Options have two public kinds:

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

Accessor:

```c
extractpdf_status extractpdf_form_field_option_get_info(
    const extractpdf_form *form,
    size_t field_index,
    size_t option_index,
    extractpdf_form_option_info *out_info);
```

### 12.1 Choice options

For `/Opt`:

```text
(single-string)
    export = decoded string
    display = decoded string

[(export) (display)]
    export = decoded first string
    display = decoded second string
```

Anything else is malformed.

Accessors:

```c
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
```

Both accessors return snapshot-owned UTF-8 strings for CHOICE options.

### 12.2 Button-state options

Checkbox/radio options are derived from non-Off PDF Name appearance states of associated Widgets.

The raw PDF Name bytes are private implementation data and are not exposed as UTF-8.

For BUTTON_STATE options:

```text
option_export()  -> UNSUPPORTED
option_display() -> UNSUPPORTED
```

Both reset their outputs before returning.

Button option order is deterministic:

```text
global public Widget order
      ↓
each Widget's non-Off normal appearance state
      ↓
first appearance wins
      ↓
private bytewise state deduplication
```

## 13. Widget model

Widget info:

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

Accessors:

```c
extractpdf_status extractpdf_form_widget_count(
    const extractpdf_form *form,
    size_t *out_count);

extractpdf_status extractpdf_form_widget_get_info(
    const extractpdf_form *form,
    size_t widget_index,
    extractpdf_form_widget_info *out_info);
```

Widget order is document visual order:

```text
page_index ascending
    ↓
raw /Annots relative order on each page
```

`field_index` identifies the owning logical field inside this snapshot.

`button_option_index`:

- points to the owning field's BUTTON_STATE option for checkbox/radio Widgets with a usable on-state;
- is `SIZE_MAX` for non-button Widgets;
- is `SIZE_MAX` if the Widget has no usable non-Off appearance state and that absence is otherwise compatible with the field's current state.

Widget indices are snapshot-local only.

## 14. Widget geometry and flags

Widget `/Rect` is read in PDF page user space and transformed into the same Fitz page-space coordinate model already used by page bounds, annotations, and links.

Rules:

- `/Rect` must contain exactly four finite numeric values;
- malformed/non-finite Rect -> `FORMAT`;
- no silent input normalization is performed beyond applying the document/page transform used by existing page-space APIs.

Widget annotation `/F`:

- missing -> `0`;
- present -> integer in `[0, UINT32_MAX]`;
- negative, non-integer, or too-large -> `FORMAT`.

## 15. Raw field-tree structural preflight

The parser validates the complete AcroForm field tree before public materialization.

### 15.1 Root structure

```text
/AcroForm absent
    -> empty snapshot

/AcroForm present and non-dictionary
    -> FORMAT

/Fields absent
    -> empty snapshot

/Fields present and non-array
    -> FORMAT
```

Every `/Fields` member must resolve to a dictionary.

### 15.2 Parent/Kids graph

For every traversed field node:

- `/Kids`, when present, must be an array;
- every Kids member must resolve to a dictionary;
- a child field's `/Parent` must point to the exact traversed parent;
- a top-level field must not claim an unrelated external `/Parent`;
- cycles return `FORMAT`;
- a node reached twice through the field graph returns `FORMAT`;
- otherwise structurally valid depth beyond 256 returns `UNSUPPORTED`.

### 15.3 Terminal classification

A field node may be:

1. an intermediate field container whose `/Kids` are child field nodes;
2. a terminal logical field whose `/Kids` are Widget leaves;
3. a terminal logical field that is itself `/Subtype /Widget`;
4. a terminal logical field with no Widget.

A `/Kids` array mixing child field nodes and Widget leaves is `FORMAT`.

Intermediate field containers are never published as fields.

## 16. Validated inheritance

Form inheritance is resolved only over the private validated parent graph.

The implementation must not define public behavior by calling generic inheritable helpers whose parent-walk path may repair structure.

The parser must be able to distinguish:

```text
value
+
which field-tree node supplied that effective value
```

This provenance is private in Snapshot V1 but is intentionally retained in the shared semantic representation so future mutation can detect inherited-value removal ambiguity without reparsing a different model.

At minimum the parser handles effective:

- `/FT`;
- `/Ff`;
- `/V`;
- `/TU` where appropriate for label observation.

The parser does not mutate, repair, canonicalize, or rewrite source field dictionaries.

## 17. Widget/page reconciliation

Field-tree Widget discovery is not sufficient by itself because public Widget page identity and order come from page `/Annots`.

The extraction therefore performs a second pass over all pages.

A valid published Widget must satisfy both:

```text
reachable through the validated AcroForm field model
AND
appears exactly once in exactly one page /Annots array
```

Required errors:

```text
page /Annots Widget not reachable from AcroForm field model
    -> FORMAT

field-tree Widget absent from all page /Annots arrays
    -> FORMAT

same Widget appears twice in one /Annots array
    -> FORMAT

same Widget appears on two pages
    -> FORMAT

Widget /P exists but does not refer to actual containing page
    -> FORMAT
```

Non-Widget page annotations do not participate in this reconciliation and remain governed by annotation APIs.

## 18. No source mutation or repair

`extractpdf_document_form()` is an observation API.

It must not:

- execute JavaScript;
- execute field actions;
- calculate forms;
- update Widget appearances;
- repair Parent/Kids relationships;
- normalize `/V` into a different object type;
- write decoded stream values back into a field;
- mark fields dirty;
- request appearance synthesis.

MuPDF 1.28.2 convenience helpers are used only where they are proven side-effect-free for the required operation. Raw PDF object inspection is the default for structural semantics.

In particular, Snapshot V1 does not use `pdf_field_value()` as its value contract because that helper may normalize stream-valued `/V` by writing a decoded string back into the field dictionary.

It also does not use generic inheritable getters as the authority for structural semantics because the project requires a validated, non-repairing parent graph.

## 19. Public ABI

The complete intended Snapshot V1 public surface is:

```c
typedef struct extractpdf_form extractpdf_form;

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

typedef enum extractpdf_form_option_kind {
    EXTRACTPDF_FORM_OPTION_BUTTON_STATE = 1,
    EXTRACTPDF_FORM_OPTION_CHOICE = 2
} extractpdf_form_option_kind;

typedef struct extractpdf_form_option_info {
    size_t struct_size;
    extractpdf_form_option_kind kind;
} extractpdf_form_option_info;

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

typedef struct extractpdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_form_widget_info;

extractpdf_status extractpdf_document_form(
    extractpdf_document *document,
    extractpdf_form **out_form);

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

void extractpdf_drop_form(extractpdf_form *form);
```

## 20. Output reset and `struct_size` rules

The API follows existing ExtractPDF conventions.

### 20.1 Counts

For non-NULL count outputs:

```text
reset *out_count = 0 first
then validate handle/arguments
```

A NULL required count output is `ARGUMENT`.

### 20.2 String outputs

For each string accessor:

```text
reset each non-NULL output independently
    *out_utf8 = NULL
    *out_size = 0
then require both outputs
then validate handle/index/type
```

A missing semantic string returns `OK + NULL + 0`.

### 20.3 Info structs

`out_info` is required.

The caller-provided `struct_size` must be at least the size through the final currently known field of that public struct.

Rules:

- too small -> `ARGUMENT`;
- exact current size -> accepted;
- larger -> accepted, unknown trailing bytes ignored;
- known output fields are reset before later semantic validation once minimum `struct_size` has been accepted.

This is the same forward-compatible ABI policy used by current ExtractPDF info/update structs.

## 21. Error model

Public status use:

```text
ARGUMENT
    NULL required pointer
    invalid public handle
    out-of-range field/value/option/widget index
    insufficient struct_size

UNSUPPORTED
    non-PDF document
    otherwise structurally valid field-tree depth >256
    option_export/display requested for BUTTON_STATE option
    value_utf8 requested for OPTION value

FORMAT
    malformed AcroForm structure
    malformed Parent/Kids graph
    malformed effective type/flags/value/options/indices
    malformed Widget geometry/flags/page association
    ambiguous choice mapping
    duplicate logical field name ambiguity

NOMEM
    allocation failure

MUPDF / mapped errors
    unexpected MuPDF engine failure not more specifically classified
```

No partial snapshot is published on any failure.

## 22. Private implementation boundaries

The intended implementation should keep responsibilities separated.

Recommended private units:

```text
src/pdf_form_common.c/.h
    strict AcroForm tree preflight
    validated parent graph
    field classification / inheritance
    value normalization
    option normalization
    Widget semantic mapping
    shared semantic representation suitable for later editor reuse

src/pdf_form.c
    public immutable snapshot materialization
    deep-copy ownership
    public accessors / drop
```

The exact implementation file split may change during planning if a smaller focused split is proven clearer, but the semantic parser must not be duplicated between immutable snapshot and future editor work.

Annotation logic remains in its existing annotation modules. Forms do not become a special annotation subtype in public APIs.

## 23. Security behavior

Snapshot extraction is passive observation.

It does not execute:

- document JavaScript;
- field Keystroke actions;
- field Validate actions;
- field Calculate actions;
- field Format actions;
- Widget activation actions;
- submit/reset actions.

Encrypted PDFs remain readable under the existing `extractpdf_open()` authentication model; Snapshot V1 does not introduce a new encryption policy because it does not modify data.

Signed PDFs may be observed. Signature fields expose only `is_signed`; no signing or signature-preservation mutation occurs in this slice.

## 24. Explicit non-goals

Snapshot V1 does not implement:

- XFA;
- form value mutation;
- `extractpdf_form_field_ref`;
- field or Widget persistent identity;
- field/Widget create, delete, move, or rename;
- field flag mutation;
- option mutation;
- `/DV` mutation;
- rich text `/RV`;
- button caption/style editing;
- form reset or submit;
- JavaScript/event execution;
- form calculations;
- signature signing, clearing, or validation details;
- encrypted PDF editing;
- signed-PDF incremental mutation;
- NeedAppearances rewriting;
- form flattening/baking.

Form flattening remains a Phase 6 rewrite/transform concern.

## 25. Required deterministic RED matrix

The implementation plan must provide deterministic fixtures/tests for at least the following before production behavior is introduced.

### 25.1 Basic surface

- new public symbols are referenced by the new test before declarations/implementation exist;
- every pre-existing target still builds;
- the new form test is the only intentional compile RED at the first boundary.

### 25.2 Empty/unsupported

- non-PDF -> `UNSUPPORTED`, output NULL;
- no `/AcroForm` -> non-NULL empty snapshot;
- AcroForm without `/Fields` -> non-NULL empty snapshot;
- empty `/Fields` -> non-NULL empty snapshot.

### 25.3 Field structure and order

- raw field DFS order;
- intermediate namespace containers are not published;
- fully-qualified names;
- missing vs present-empty name/label;
- terminal field with Widget Kids;
- terminal field that is itself Widget;
- terminal field with zero Widgets.

### 25.4 Inheritance

- inherited `/FT`;
- inherited `/Ff`;
- inherited `/V`;
- correct fully-qualified name through parent chain;
- source remains unchanged after extraction.

### 25.5 Values

- Text missing value;
- Text present-empty value;
- Text normal UTF-8 value;
- Checkbox missing, Off, On;
- Radio option mapping across multiple Widgets;
- Combo option selection;
- editable Combo custom UTF-8 value;
- List single selection;
- List multiselect;
- List explicit empty selection;
- PushButton NOT_APPLICABLE;
- Signature NOT_APPLICABLE + `is_signed`.

### 25.6 Options

- Choice single-string option export==display;
- Choice export/display pair;
- duplicate export disambiguated by valid `/I`;
- duplicate export without disambiguation -> FORMAT;
- Button option ordering follows Widget order;
- Button state is not exposed through export/display UTF-8 accessors.

### 25.7 Widget model

- Widget global ordering: page then `/Annots` relative order;
- correct owning `field_index`;
- Fitz page-space bounds;
- full uint32 Widget `/F`;
- checkbox/radio `button_option_index`;
- non-button `button_option_index == SIZE_MAX`.

### 25.8 Malformed field tree

- AcroForm non-dict;
- Fields non-array;
- non-dict field member;
- non-array Kids;
- non-dict Kids member;
- field-tree cycle;
- repeated child/node;
- broken Parent;
- illegal root Parent;
- mixed child-field + Widget Kids;
- duplicate non-empty full field name;
- depth 257 -> UNSUPPORTED;
- missing effective `/FT`;
- syntactically valid unknown `/FT` -> UNKNOWN;
- malformed `/Ff`.

### 25.9 Malformed values/options

- stream `/V` -> FORMAT without source rewrite;
- invalid Text `/V` type;
- invalid Checkbox/Radio `/V` state;
- malformed `/Opt` entry;
- malformed `/I` type;
- `/I` out of range;
- duplicate invalid `/I` indices;
- `/I` contradicts `/V`;
- non-editable combo custom value -> FORMAT.

### 25.10 Widget reconciliation

- page Widget orphaned from field tree -> FORMAT;
- field-tree Widget absent from all pages -> FORMAT;
- duplicate Widget in one page Annots -> FORMAT;
- same Widget on two pages -> FORMAT;
- `/P` mismatch -> FORMAT;
- malformed Widget Rect -> FORMAT;
- non-finite Widget Rect -> FORMAT;
- malformed Widget `/F` -> FORMAT.

### 25.11 Ownership and atomic publication

- snapshot strings/values/options survive source close;
- complete snapshot survives source close;
- deterministic late-malformed fixture proves no partial prefix is published;
- repeated extraction does not change source-observed behavior.

### 25.12 Cross-platform

Target progression:

```text
existing suite: 19 CTests
Forms Snapshot V1: 20 CTests
```

Before integration the exact feature head must pass:

- Linux static build + all 20 CTests;
- Linux ASan/UBSan build + all 20 CTests;
- same-head macOS configure/build/test + all 20 CTests;
- same-head Windows DLL configure/build/test + all 20 CTests;
- Windows shared-library symbol registration for the new form surface.

## 26. Delivery decomposition

The overall Forms roadmap is intentionally split.

### Slice A: issue #43 — AcroForm Snapshot V1

This spec covers Slice A only.

Completion requires:

```text
committed approved design
    ↓
committed TDD implementation plan
    ↓
strict compile RED
    ↓
shared semantic parser + immutable snapshot GREEN
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

### Slice B: Form Value Mutation V1

Slice B starts only after Slice A is integrated and proven.

It will reuse:

- the same field/value/option semantics;
- the same strict parser;
- the existing `extractpdf_pdf_edit` isolated editor;
- session-local field refs;
- one journal operation per value mutation;
- local Widget appearance refresh;
- no JavaScript/Validate/Calculate/Format execution.

No Slice B issue, implementation plan, RED fixture, or production code is created by this design step.

## 27. Acceptance criteria for this design

This design is complete when all of the following are true:

1. logical field and Widget instance are separate public concepts;
2. public field/value/option/widget ABI is fully specified;
3. missing/empty/Off/multiselect/custom-combo semantics are unambiguous;
4. field-tree preflight and Widget/page reconciliation define malformed policy;
5. immutable extraction has no repair or write-back behavior;
6. ordering, ownership, identity, and output-reset contracts are deterministic;
7. Snapshot V1 is isolated from future mutation work;
8. every public semantic contract has a corresponding required RED boundary;
9. cross-platform proof and explicit integration gate are retained.
