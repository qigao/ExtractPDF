# QuantaPDF PDF Annotation Mutation V1 Design

## Status

Approved Phase 5 Interactive PDF design for issue #37 under roadmap #2.

Base: integrated annotation-enumeration master exact SHA `58525ce1b4b691ef53dfafc7c4e4d82753c966ba`, verified by integrated push workflow #180 (`33181186725`) across Linux static + ASan/UBSan, macOS, and Windows DLL.

Design branch: `feat/pdf-annotation-mutation`.

This slice introduces the first mutable PDF surface, but mutation is deliberately isolated from the existing read-only `quantapdf_document` / `quantapdf_page` model. The source document remains immutable. Mutation occurs only inside a private PDF editor fork and is published only as existing immutable `quantapdf_output` snapshots.

## Goals

- Preserve all existing source/document/page/snapshot semantics.
- Introduce an opaque isolated `quantapdf_pdf_edit` rather than making `quantapdf_document` mutable.
- Allow ordinary annotation create/update/delete through session-local opaque value references.
- Keep immutable enumeration indices non-persistent and unusable as mutation selectors.
- Make each public create/update/delete call atomic through MuPDF journalling.
- Produce non-consuming deterministic immutable output snapshots from an edit session.
- Reuse the existing Fitz page-space coordinate model and `quantapdf_output` ownership model.
- Fail closed for encrypted and already-signed PDFs in V1.
- Never execute PDF JavaScript as a side effect of annotation editing.
- Support only annotation types and fields whose semantics V1 can express correctly.

## Non-goals

V1 does not mutate the source `quantapdf_document`, expose PDF object numbers/generations or `/NM`, define persistent identity across save/reopen, expose public undo/redo, edit forms/widgets, edit links/popups, execute JavaScript, perform incremental save, preserve/rewrite encryption, preserve signatures through mutation, or model subtype-specific QuadPoints/vertices/ink/line geometry.

V1 also does not promise that a session-local annotation ref remains meaningful after `quantapdf_drop_pdf_edit()` or in another edit session.

## Architecture

```text
quantapdf_document                 read-only source
        |
        | quantapdf_pdf_edit_begin()
        | validate PDF / encryption / signed-input policy
        | materialize private full-document representation
        v
quantapdf_pdf_edit                 isolated mutable fork
        |
        |-- annotation discovery -> session-local annotation refs
        |-- create/update/delete -> one atomic journal operation per API call
        |-- annotation getters -> copies or value structs only
        |
        | quantapdf_pdf_edit_snapshot()
        v
quantapdf_output                   immutable owned bytes
        |
        | existing output_data / output_save_file / drop_output
        v
caller-controlled persistence
```

Once `quantapdf_pdf_edit_begin()` succeeds, the editor retains no public lifetime dependency on the source `quantapdf_document`; the caller may close the source immediately. Dropping the editor discards its private fork and invalidates the editor's ref namespace. Previously produced outputs remain valid.

## Why the Source Document Stays Immutable

The current `quantapdf_document` is a read/lifecycle wrapper around one `fz_context` and `fz_document`. Existing pages and immutable snapshots assume source state does not change underneath them. Making that object mutable would retroactively complicate page handles, render/text/image/link/outline/metadata snapshots, rollback semantics, and save semantics.

Mutation therefore belongs in a separate editing layer. `quantapdf_pdf_edit_begin()` creates a private editable PDF representation. No mutation API accepts `quantapdf_document *` or `quantapdf_page *` as the mutation target after begin.

The implementation may serialize/reopen, clone, graft, or otherwise materialize the private PDF as needed, but the public contract is independent of that mechanism. Begin must be atomic: on failure `*out_edit == NULL`, and the source remains usable with its pre-call observable semantics.

## Public ABI

Add the opaque editor and fixed-size session-local ref token:

```c
typedef struct quantapdf_pdf_edit quantapdf_pdf_edit;

typedef struct quantapdf_annotation_ref {
    uint64_t opaque[2];
} quantapdf_annotation_ref;
```

Add one new status at the end of the stable status enum:

```c
QUANTAPDF_ERROR_STATE = 8
```

Existing numeric status values do not change.

Add update-field bits:

```c
typedef enum quantapdf_annotation_update_field {
    QUANTAPDF_ANNOTATION_UPDATE_BOUNDS = 1u << 0,
    QUANTAPDF_ANNOTATION_UPDATE_FLAGS = 1u << 1,
    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS = 1u << 2
} quantapdf_annotation_update_field;
```

Create options:

```c
typedef struct quantapdf_annotation_create_options {
    size_t struct_size;
    quantapdf_annotation_type type;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_create_options;
```

Partial update:

```c
typedef struct quantapdf_annotation_update {
    size_t struct_size;
    uint32_t fields;
    quantapdf_rect bounds;
    uint32_t flags;
    const char *contents_utf8;
    size_t contents_size;
} quantapdf_annotation_update;
```

Public functions:

```c
QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_begin(
    quantapdf_document *source,
    quantapdf_pdf_edit **out_edit);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_count(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t *out_count);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_ref_at(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_get_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_create(
    quantapdf_pdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_update(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_annotation_delete(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref);

QUANTAPDF_API quantapdf_status quantapdf_pdf_edit_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output);

QUANTAPDF_API void quantapdf_drop_pdf_edit(
    quantapdf_pdf_edit *edit);
```

No MuPDF type, PDF object identity, filename, or mutable source handle is added to the public mutation ABI.

## Struct-size and Output-reset Convention

New option/update structs follow the existing QuantaPDF forward-compatible `struct_size` convention: a caller-provided struct must be at least large enough through the last field currently required by V1; larger structs are accepted and unknown trailing bytes are ignored. A too-small `struct_size` is `ARGUMENT`.

`quantapdf_annotation_info` keeps its existing minimum-size behavior in the live editor getter.

Every required output pointer follows reset-before-later-validation behavior wherever a non-NULL pointer can be reset safely:

```text
edit_begin:       out_edit == NULL -> ARGUMENT; otherwise *out_edit = NULL first
count:            if out_count != NULL, *out_count = 0 first; NULL out_count -> ARGUMENT
ref_at/create:    if out_ref != NULL, zero the whole token first; NULL out_ref -> ARGUMENT
get_info:         NULL out_info -> ARGUMENT; validate minimum struct_size, then reset known fields
contents:         reset each non-NULL output independently, then require both out_utf8 and out_size
snapshot:         out_output == NULL -> ARGUMENT; otherwise *out_output = NULL first
```

No later validation failure may leave stale caller-visible output values.

## Editor Begin Contract

`quantapdf_pdf_edit_begin()` validates and materializes the private editor after resetting `*out_edit`.

Required behavior:

- null source -> `ARGUMENT`;
- non-PDF source -> `UNSUPPORTED`;
- encrypted PDF -> `UNSUPPORTED`;
- PDF containing an already-signed signature widget -> `UNSUPPORTED`;
- allocation failure -> `NOMEM`;
- malformed PDF encountered while creating the private representation -> mapped existing error;
- success -> non-NULL editor independent of source lifetime.

An unsigned signature field does not by itself make the document unsupported. V1 rejects an already-signed signature value because full-rewrite mutation has no signature-preservation contract.

The edit-session security boundary detects signed AcroForm signature fields and
catalog `/Perms` entries for `/DocMDP`, `/UR`, or `/UR3` signatures.

The private PDF must have JavaScript disabled before any editing operation is exposed to the caller. Editor begin itself must not execute document JavaScript or action code.

## Annotation Discovery Contract

Editor annotation discovery deliberately reuses the semantic boundary of immutable Annotation Enumeration V1.

For a requested page, ordinary annotation discovery uses the same rules:

- missing/non-array `/Annots` -> empty;
- non-dictionary entries ignored;
- Link ignored;
- Popup ignored;
- Widget ignored;
- missing/non-name/unrecognized subtype -> `UNKNOWN`;
- surviving annotations preserve original relative `/Annots` order;
- surviving Rect/F/Contents materialization follows the same strict validation contract as immutable enumeration.

This prevents the editor from creating a second incompatible definition of "ordinary annotation".

`quantapdf_pdf_edit_annotation_count()` returns the count of current surviving ordinary annotations on the current edit state. Invalid page index is `ARGUMENT`.

`quantapdf_pdf_edit_annotation_ref_at(edit, page, index, &ref)` uses the current discovery index only to acquire a session-local ref. Out-of-range index is `ARGUMENT`. After a ref is issued, later update/delete/get operations use the ref, not the index.

If discovery of a page encounters a malformed surviving annotation, the discovery call fails atomically with `FORMAT`; it does not publish/register new refs for an earlier valid prefix. Existing refs already issued by successful operations remain unchanged.

`UNKNOWN` entries may receive refs and may be inspected with the common getters, but all mutation through such refs is `UNSUPPORTED`.

## Session-local Annotation Identity

`quantapdf_annotation_ref` is an opaque value capability, not a pointer. Callers may copy it by value but may not interpret `opaque[]`.

Conceptually the private editor maintains a namespace equivalent to:

```text
session nonce
registry slot
generation / liveness
        -> page
        -> private annotation identity
        -> live | tombstone
```

The exact encoding and registry representation are private.

Required identity properties:

```text
same edit + live ref              valid
create/delete shifts indices      unrelated existing live refs remain valid
new create                         returns a new live ref
delete success                     deleted ref becomes tombstone
same-session tombstone use         STATE
wrong-session token                ARGUMENT
malformed/forged token             ARGUMENT
snapshot()                         refs remain valid
drop edit                          entire ref namespace ends
save/reopen output                 no ref continuity promise
```

Registry slots must not be recycled in a way that can make a deleted ref accidentally target a new annotation during the same edit session.

PDF object number/generation or kept PDF objects may be used privately to resolve a registry entry, but none of that becomes public identity.

## Status Semantics

`QUANTAPDF_ERROR_STATE` means the supplied public object was once meaningful in the current editor model but is no longer in a state where the requested operation is valid. The primary V1 case is a deleted/tombstoned annotation ref.

Classification:

```text
null required pointer                 ARGUMENT
bad page/index                        ARGUMENT
wrong-session ref                     ARGUMENT
malformed/forged ref                  ARGUMENT
tombstoned same-session ref           STATE
UNKNOWN mutation                      UNSUPPORTED
unsupported type-specific mutation    UNSUPPORTED
non-PDF/encrypted/signed edit begin   UNSUPPORTED
malformed surviving PDF data          FORMAT
allocation/size overflow              NOMEM
MuPDF exception                       existing status mapper
```

`quantapdf_status_string()` must gain a stable string for `QUANTAPDF_ERROR_STATE`.

## Common Annotation Data

Mutation V1 uses the already-public `quantapdf_annotation_type` and `quantapdf_annotation_info` model:

```text
type
bounds in Fitz page space
flags
Contents
```

No new author/name/date/color/opacity/border/intent/reply/appearance-state model is added in this slice.

### Bounds Input

Public mutation bounds are Fitz page-space rectangles, matching existing page/link/annotation geometry.

Inputs must contain finite floats with `x0 <= x1` and `y0 <= y1`. Zero-width or zero-height rectangles are allowed unless MuPDF rejects the specific annotation type. Non-finite or reversed rectangles are `ARGUMENT`; V1 does not silently normalize mutation inputs.

MuPDF's annotation geometry path performs the private inverse page transform required to store PDF-space geometry. The public caller never supplies PDF-space coordinates.

### Flags

Flags keep the existing raw `uint32_t` PDF `/F` bitmask contract. V1 accepts the full `uint32_t` range so a caller can round-trip an existing raw flag value without signed narrowing.

Pinned MuPDF's convenience flag setter accepts `int`; implementation must therefore not cast a value greater than `INT_MAX` through that setter and emit a negative PDF integer. If the convenience setter cannot preserve the full public value, the implementation must perform an equivalent journalled PDF `/F` write using a non-narrowing integer representation and request the same dirty/resynthesis behavior. This is a private implementation detail; the public `uint32_t` contract does not narrow.

### Contents

Create semantics:

```text
contents_utf8 == NULL && contents_size == 0    absent /Contents
contents_utf8 != NULL && contents_size == 0    present empty /Contents
contents_utf8 != NULL && contents_size > 0     present UTF-8 contents
contents_utf8 == NULL && contents_size > 0     ARGUMENT
```

Update semantics depend on the CONTENTS field bit:

```text
CONTENTS bit absent                             unchanged
bit present + NULL/0                            remove /Contents
bit present + non-NULL/0                        set present empty string
bit present + non-NULL/nonzero                  set UTF-8 contents
bit present + NULL/nonzero                      ARGUMENT
```

Present input is a counted byte range. It does not need a terminating NUL after `contents_size`; QuantaPDF copies the range into temporary storage and appends its own terminator before calling a C-string MuPDF API. The counted range must be valid UTF-8 and must not contain embedded NUL. Invalid UTF-8 or embedded NUL is `ARGUMENT`. Size arithmetic overflow is `NOMEM`.

Live-editor contents access returns an allocated QuantaPDF-owned copy:

```c
char *text = NULL;
size_t size = 0;
quantapdf_pdf_edit_annotation_contents(edit, &ref, &text, &size);
...
quantapdf_free(text);
```

It never returns a borrowed pointer into mutable MuPDF state. Missing Contents returns `OK + NULL + 0`; present-empty returns `OK + non-NULL + 0`.

## Supported Type Matrix

Enumeration capability does not imply creation capability.

V1 create support is limited to types whose geometry and minimum useful representation can be correctly expressed by Rect plus common fields:

| Type | Create | Bounds update | Flags update | Contents update | Delete |
| --- | ---: | ---: | ---: | ---: | ---: |
| TEXT | yes | yes | yes | yes | yes |
| FREE_TEXT | yes | yes | yes | yes | yes |
| SQUARE | yes | yes | yes | yes | yes |
| CIRCLE | yes | yes | yes | yes | yes |
| LINE | no | no | yes | yes | yes |
| POLYGON / POLY_LINE | no | no | yes | yes | yes |
| HIGHLIGHT / UNDERLINE / SQUIGGLY / STRIKE_OUT | no | no | yes | yes | yes |
| REDACT | no | no | yes | yes | yes |
| STAMP / CARET / FILE_ATTACHMENT / SOUND / MOVIE / RICH_MEDIA / SCREEN / PRINTER_MARK / TRAP_NET / WATERMARK / 3D / PROJECTION | no | no | yes | yes | yes |
| INK | no | no | yes | yes | yes |
| UNKNOWN | no | no | no | no | no |

Link, Popup, and Widget are not ordinary mutation targets in this API.

Bounds update is intentionally narrower than generic field update. MuPDF distinguishes annotations whose design rectangle is directly manipulable from annotations whose rectangle is derived from QuadPoints, InkList, vertices, or other subtype geometry.

A future geometry slice may extend existing refs with type-specific APIs without changing the identity model.

## Create Contract

`quantapdf_pdf_edit_annotation_create()` requires non-NULL edit/options/out_ref, a valid page index, and a sufficiently large options `struct_size`.

It then:

- zeroes `out_ref` before later validation/work;
- validates type/bounds/flags/Contents before mutation;
- only permits TEXT/FREE_TEXT/SQUARE/CIRCLE;
- creates the annotation inside one outer journal operation;
- applies bounds, flags, and optional Contents;
- performs required annotation appearance update/resynthesis before successful operation completion;
- publishes/registers the new ref only after the PDF mutation fully succeeds.

Any failure abandons the outer operation, releases temporary MuPDF references, leaves the editor's observable state unchanged, and leaves `out_ref` zeroed.

No partially created annotation or registry entry may survive a failed create.

## Update Contract

`quantapdf_pdf_edit_annotation_update()` requires non-NULL edit/ref/update and a sufficiently large update `struct_size`.

It validates the editor and ref before treating a zero field mask as a successful no-op. Therefore a wrong-session ref still returns `ARGUMENT` and a tombstoned ref still returns `STATE` even when `fields == 0`.

Unknown field bits are `ARGUMENT`. For requested fields, all input validation that does not require mutation is completed before the outer operation begins.

Within one outer journal operation the implementation applies requested fields in a fixed private order. Public semantics do not depend on that order because failure at any point abandons the whole operation.

Bounds update is allowed only for TEXT/FREE_TEXT/SQUARE/CIRCLE. Generic flags/Contents update is allowed for recognized ordinary types. UNKNOWN mutation is `UNSUPPORTED`.

Appearance regeneration/update required by any setter completes before the outer operation ends. If appearance update fails after one or more fields changed, the entire call is abandoned and all fields return to their pre-call state.

## Delete Contract

Delete is allowed for every recognized ordinary annotation type, including types whose geometry V1 cannot create or edit.

Deletion occurs in one outer journal operation using MuPDF annotation deletion semantics. Associated Popup cleanup performed by MuPDF is part of that same operation. Link and Widget remain outside this API.

Only after successful operation completion is the registry entry marked tombstoned. If deletion fails, the ref remains live and the annotation remains observable.

Calling get/update/delete/contents on a same-session tombstoned ref returns `QUANTAPDF_ERROR_STATE`.

## Atomic Mutation Operations

The private PDF enables MuPDF journalling when the editor is constructed.

Each public create/update/delete call establishes one outer operation:

```text
validate request/ref/type
        |
pdf_begin_operation
        |
perform one or more MuPDF annotation mutations
        |
perform required appearance update/resynthesis
        |
        +-- success -> pdf_end_operation -> update registry/publish ref state
        |
        +-- failure -> pdf_abandon_operation -> restore pre-call PDF state
```

Pinned MuPDF annotation setters may start nested annotation operations. Those are intentionally enclosed by the QuantaPDF outer operation so one public API call remains the atomic unit.

Registry changes that would expose a new ref or tombstone an existing ref occur only after the corresponding PDF operation has completed successfully. A failure to allocate registry state needed for create must be handled before publication and must not leave the new PDF annotation behind.

No public undo/redo surface is introduced. Journalling is an implementation mechanism for per-call atomicity only.

## Appearance Semantics

A mutation is not considered successful if the stored annotation data changes but the appearance required for normal rendering cannot be updated consistently.

After setters request synthesis/resynthesis, the target annotation is updated before the outer operation ends. V1 should prefer targeted `pdf_update_annot()` behavior and must not deliberately execute a whole JavaScript/form event pipeline as part of ordinary annotation CRUD.

If pinned MuPDF requires a broader page update for a supported annotation subtype, that behavior must be demonstrated in RED/GREEN tests without enabling JavaScript. The public guarantee remains: successful mutation produces a renderable internally consistent annotation; failed appearance work rolls the API call back.

## Snapshot Contract

`quantapdf_pdf_edit_snapshot()` is deliberately not named commit. It is a non-consuming immutable snapshot operation.

After resetting `*out_output`, it serializes the complete current private PDF using the deterministic full-document writer policy and deep-copies bytes into the existing `quantapdf_output` abstraction. Only complete success publishes the output.

Required properties:

1. Two snapshots from the same edit state with no intervening mutation are byte-identical.
2. Snapshot A remains byte-identical and valid after later mutations to the editor.
3. Snapshot B after later mutation reflects the later state.
4. Snapshot does not consume/finalize the editor.
5. Snapshot does not invalidate existing live refs.
6. Output remains valid after the editor is dropped.
7. Snapshot failure leaves the editor usable in its pre-snapshot observable state and returns NULL output.

The implementation should reuse the existing deterministic PDF serialization policy (`reproducible = 1`, `dont_regenerate_id = 1`) and existing immutable output ownership model.

If `pdf_write_document()` is found to mutate internal editor bookkeeping in a way that violates properties above, the implementation must snapshot through another private serialization clone. Public semantics do not change.

## Encryption Policy

V1 rejects every PDF whose trailer contains `/Encrypt`, even if `quantapdf_open()` successfully authenticated a password.

This avoids accidentally choosing policy for:

- owner/user password preservation,
- algorithm preservation,
- permission preservation,
- metadata encryption,
- re-encryption keys,
- incremental versus full rewrite.

Encrypted mutation belongs to the later rewrite/encryption architecture.

## Signature Policy

V1 permits unsigned signature fields to remain present but rejects a source containing any already-signed signature widget/value.

The editor performs a full-document rewrite and does not define incremental-update signature preservation. Returning a rewritten PDF while implying an existing signature remains valid would be an unsafe API contract.

Signed-PDF mutation therefore belongs to a separate incremental/signature architecture.

## JavaScript Policy

The private PDF editor disables JavaScript before callers can perform mutations.

Annotation CRUD must not execute document OpenAction JavaScript, annotation actions, form validation/calculation scripts, or other PDF JavaScript as an implicit side effect. Editor begin and snapshot likewise must not execute document JavaScript.

JavaScript execution remains out of scope for roadmap #2. A deterministic fixture must prove that a script capable of changing observable document state is not executed by editor begin, mutation, appearance update, or snapshot.

## Forms, Widgets, Links, and Popups

Widgets/forms are reserved for a separate Phase 5 architecture. The editor's ordinary annotation discovery filters Widget exactly as immutable enumeration does.

Links remain owned by the existing link surface and are not represented by annotation refs in Mutation V1.

Popup is not a standalone ordinary annotation target. MuPDF's automatic associated-Popup cleanup during deletion is allowed as internal consistency behavior, but there is no public Popup ref or Popup CRUD API.

## Lifetime and Ownership

`quantapdf_pdf_edit` owns all private MuPDF/PDF state required for mutation. It retains no public dependency on the source after begin succeeds.

`quantapdf_annotation_ref` contains no pointer and requires no drop function. Its namespace exists only while the owning editor exists.

Allocated strings returned by live-editor getters use `quantapdf_free()`.

`quantapdf_output` snapshots use the existing output ownership rules and are independent of both source and editor lifetimes.

`quantapdf_drop_pdf_edit(NULL)` is a no-op.

## Deterministic RED Fixtures

The implementation plan must define checked-in deterministic PDFs sufficient to prove the contracts below without depending on network resources or system fonts beyond the pinned MuPDF test environment.

### Editable ordinary annotations

A PDF with multiple pages and an `/Annots` sequence that includes supported Rect-based ordinary annotations, at least one QuadPoints annotation, at least one Ink/vertex-style unsupported-geometry annotation, Link, Popup, Widget, and UNKNOWN subtype.

This fixture locks discovery filtering/order, strict survivor validation, supported-type matrix, and ref/index separation.

### Source immutability and lifetime

Begin an editor, close the source, continue discovery/mutation/snapshot successfully. Separately retain a source-side annotation snapshot before editing and prove it never changes after editor mutation.

### Ref identity

Acquire refs to at least two annotations, delete an earlier annotation so later discovery indices shift, and prove the surviving later ref still addresses the same logical annotation. Also create a new annotation and prove existing refs remain stable. A ref from edit A used in edit B must be rejected. Delete must tombstone the deleted ref.

### Atomic update failure

Use one multi-field update where an earlier field can succeed but a later requested field deterministically fails. The call must fail and subsequent getters/snapshot must show every field unchanged.

### Create failure

Trigger a deterministic failure after PDF annotation creation has started but before publication. The page count/order and ref registry must remain unchanged and no ref may be published.

### Full uint32 flags

A deterministic case must exercise a raw `/F` value above `INT_MAX`, prove immutable/editor getters preserve the same `uint32_t`, and prove an explicit flags update can round-trip that value without producing a negative PDF integer or failing subsequent strict enumeration.

### Contents ownership and presence

Prove absent, present-empty, and non-empty Contents remain distinct; counted input need not be NUL-terminated outside its declared size; invalid UTF-8 and embedded NUL are rejected; live getter strings survive subsequent editor mutation until caller frees them.

### Snapshot isolation

Generate snapshot A, mutate, generate snapshot B, and prove A bytes do not change, B differs where expected, reparse of A exposes the earlier annotation state, reparse of B exposes the later state, and two snapshots without mutation are byte-identical.

Reparsed public verification should reuse immutable annotation enumeration wherever possible rather than inspect raw PDF objects only.

### Fail-closed inputs

Checked-in encrypted and already-signed PDFs must make edit begin return `UNSUPPORTED + NULL`. An unsigned signature field remains acceptable.

### JavaScript disabled

A fixture containing JavaScript with a deterministic observable mutation must prove no such change occurs through editor begin/CRUD/appearance update/snapshot.

## TDD Boundary

Implementation follows strict RED -> GREEN.

Branch base:

```text
58525ce1b4b691ef53dfafc7c4e4d82753c966ba
  -> feat/pdf-annotation-mutation
```

RED contains deterministic fixtures, the new mutation test target, and test/CMake registration only. It must not add mutation production declarations, `QUANTAPDF_ERROR_STATE`, or implementation behavior before the RED failure is captured.

A valid RED means:

- the existing library and all pre-existing test targets build;
- only the new mutation test target fails because the approved editor/ref/status/update ABI is absent;
- no unrelated runtime/fixture failure masks the missing-ABI boundary.

GREEN then adds the minimal production implementation required by the locked design.

The exact production file split is chosen in the implementation plan, but scope must remain focused on the public header, private PDF editor/mutation implementation, deterministic serialization reuse where needed, CMake registration, and the new tests/fixtures. Unrelated render/text/image/link/outline/metadata/composition refactors are out of scope.

## Cross-platform Acceptance

Before integration, the final exact feature head must pass Linux strict static build + all CTests, Linux ASan/UBSan + all CTests, macOS configure/build/test, and Windows DLL configure/build/test with the new mutation CTest executed through the Windows DLL build.

After merge, the integrated master merge SHA must pass the same push workflow before issue #37 is closed completed and roadmap #2 marks Annotation Mutation V1 integrated.

## Explicit Future Extensions

The following extensions intentionally build on the same editor/ref architecture rather than widening V1 prematurely:

```text
Markup geometry     -> QuadPoints APIs
Line geometry       -> endpoints / line endings
Polygon/PolyLine    -> vertex APIs
Ink                 -> stroke-list APIs
Forms/widgets       -> separate widget/field model
Persistent identity -> separately designed cross-save identity
Encrypted editing   -> rewrite/encryption policy
Signed editing      -> incremental/signature policy
Public undo/redo    -> separate editor-history API if ever required
```

None of these extensions may reinterpret immutable enumeration index as identity or expose raw MuPDF/PDF objects through the C ABI.
