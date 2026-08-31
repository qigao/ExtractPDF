# QuantaPDF PDF Outline / Bookmarks V1 Design

## Status

Approved architectural design for Phase 5 Interactive PDF child issue #33 under roadmap #2.

Base: integrated `master` exact SHA `cb0f50b0734bcae00fedd529e4f13701c0852866`.

This slice defines a PDF-only, read-only document-outline snapshot. Outline editing/mutation remains a separate future architecture.

## Goals

- Reuse the existing opaque `quantapdf_document`.
- Expose no MuPDF types, `fz_location`, PDF object pointers, or PDF destination syntax.
- Return an QuantaPDF-owned immutable outline snapshot independent of document lifetime.
- Preserve hierarchy through flat preorder indices, not a public pointer graph.
- Map internal destinations to the existing zero-based flat PDF page index plus Fitz page-space x/y.
- Distinguish no destination, internal destination, and external URI destination.
- Refuse malformed/cyclic/repairable PDF outline structures instead of silently repairing the live PDF.
- Keep QuantaPDF-owned preflight and flattening iterative.
- Bound MuPDF 1.28.2's unavoidable recursive PDF-outline validation before invoking it.

## Non-goals

V1 does not provide outline mutation, persistent bookmark IDs, non-PDF outline semantics, chapter/layout locations, raw PDF destination/action objects, style decoration, JavaScript execution, remote-document loading, or a second public PDF-document handle.

## PDF-only Boundary

MuPDF can expose outlines for multiple formats, but non-PDF handlers may use chapter/page locations and reflow/layout semantics. QuantaPDF currently exposes a stable flat page model only.

```text
PDF document      -> outline snapshot API
non-PDF document  -> QUANTAPDF_ERROR_UNSUPPORTED
```

## Public ABI

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

`quantapdf_link_kind` is not reused because outline nodes have a legal no-destination state and may evolve independently from page links.

## Hierarchy Representation

Nodes are stored in global preorder traversal order.

```text
0 Chapter 1
1   Section 1.1
2   Section 1.2
3     Section 1.2.1
4 Chapter 2
```

Each node exposes `parent_index`, `first_child_index`, and `next_sibling_index`.

```text
SIZE_MAX = no parent / no first child / no next sibling
```

No extra public `QUANTAPDF_INDEX_NONE` macro is added.

Indices are snapshot-local coordinates only. They are not PDF object numbers, persistent bookmark IDs, cross-snapshot identities, or valid mutation handles.

## `struct_size` Compatibility

Callers initialize:

```c
quantapdf_outline_info info = {0};
info.struct_size = sizeof(info);
```

V1 requires the supplied struct to cover through `is_open`. Larger structs are accepted; V1 writes only fields it knows.

After `struct_size` validation and before later handle/index validation, known fields are reset to:

```text
parent_index       = SIZE_MAX
first_child_index  = SIZE_MAX
next_sibling_index = SIZE_MAX
destination_kind   = NONE
target_page        = -1
target             = {0,0}
is_open            = 0
```

Future style fields can be appended ABI-safely.

## Destination Contract

### NONE

```text
destination_kind = NONE
target_page      = -1
target            = {0,0}
outline_uri()     = unavailable
```

### INTERNAL

```text
destination_kind = INTERNAL
target_page      = zero-based flat PDF page index
target            = Fitz page-space x/y
outline_uri()     = unavailable
```

The public ABI never exposes `fz_location` or raw PDF destination syntax. A non-null internal destination that cannot resolve to a valid PDF page causes the entire extraction to fail with `QUANTAPDF_ERROR_FORMAT`; V1 never publishes `INTERNAL + target_page = -1`.

### URI

```text
destination_kind = URI
target_page      = -1
target            = {0,0}
outline_uri()     = snapshot-owned borrowed UTF-8 URI
```

QuantaPDF never executes or fetches the URI.

## Title and URI Ownership

Strings are copied once into snapshot-owned storage.

`quantapdf_outline_title()`:

```text
absent title        -> QUANTAPDF_OK, NULL, 0
present empty title -> QUANTAPDF_OK, borrowed "", 0
present title       -> QUANTAPDF_OK, borrowed UTF-8, byte size excluding NUL
```

`quantapdf_outline_uri()`:

```text
URI node             -> QUANTAPDF_OK + borrowed URI
NONE / INTERNAL      -> QUANTAPDF_ERROR_ARGUMENT + NULL + 0
invalid handle/index -> QUANTAPDF_ERROR_ARGUMENT + NULL + 0
```

Pointers remain valid until `quantapdf_drop_outline()`.

## Display State

V1 exposes only `is_open`. Bold, italic, RGB color, and other presentation decoration are deferred.

## Empty Outline

No `/Outlines`, or an outline root with no first item, is a valid empty collection:

```text
quantapdf_document_outline() -> QUANTAPDF_OK
*out_outline                  -> non-NULL snapshot
quantapdf_outline_count()    -> 0
```

Success always returns a valid snapshot handle; failure leaves `*out_outline == NULL`.

## Snapshot Lifetime

The completed snapshot must retain no MuPDF/document state. Conceptually:

```text
quantapdf_outline
  ├── nodes[]
  └── strings[]
```

Internal records contain relation indices, destination data, state, and string offsets/sizes. The snapshot retains no `fz_outline*`, iterator, `pdf_obj*`, `fz_document*`, or `quantapdf_document*`.

Therefore the source document may be closed immediately after extraction and all snapshot accessors remain valid.

`quantapdf_drop_outline(NULL)` is a safe no-op.

## Read-only PDF Structural Preflight

MuPDF 1.28.2's PDF outline iterator constructor validates the outline tree and may repair inconsistent `Parent`, `Prev`, and parent `Last` relationships. It rejects cycles. QuantaPDF's read API must not trigger that silent repair behavior.

Before invoking the MuPDF iterator, QuantaPDF performs an iterative, read-only walk of `/Root/Outlines` and verifies at minimum:

- every traversed outline item is an indirect dictionary;
- expected `Parent` is correct;
- expected `Prev` is correct;
- each parent's `Last` matches the final entry in its child/sibling chain;
- every node is visited at most once;
- cycles/repeated nodes are rejected;
- sibling/child traversal terminates;
- node/depth/allocation accounting cannot overflow.

Any inconsistency MuPDF would otherwise repair returns `QUANTAPDF_ERROR_FORMAT`. The read API never intentionally changes PDF outline objects.

QuantaPDF-owned preflight and preorder flattening use explicit heap-backed stack/worklist state, not recursion driven by attacker-controlled outline depth.

## MuPDF 1.28.2 Recursive Validation Safety Bound

Pinned MuPDF 1.28.2 still performs a recursive validation pass inside its PDF outline iterator constructor. QuantaPDF calculates maximum depth during iterative preflight and refuses excessive valid trees before invoking that constructor.

```text
maximum accepted outline depth = 256 levels

malformed / cyclic / inconsistent tree -> QUANTAPDF_ERROR_FORMAT
valid depth <= 256                      -> continue
valid depth > 256                       -> QUANTAPDF_ERROR_UNSUPPORTED
```

The numeric bound is a private V1 implementation capability, not a public struct field or persistent ABI guarantee. The public semantic distinction is stable: `FORMAT` means malformed; `UNSUPPORTED` means structurally valid but outside this V1 safety capability.

Pinned MuPDF 1.28.2's public `pdf_load_page_tree()` entry point is currently a no-op, so it introduces no additional page-tree repair requirement for this slice.

## MuPDF Integration Boundary

After PDF-only gating, successful iterative preflight, and depth validation:

```text
quantapdf_document
    ↓
pdf_specifics()
    ↓
iterative read-only outline preflight
    ↓
depth <= 256
    ↓
MuPDF outline iterator/item decoding
    ↓
resolve destination
    ↓
iterative preorder flatten + deep copy
    ↓
quantapdf_outline snapshot
```

MuPDF destination/action parsing is reused internally rather than duplicated. No MuPDF pointer survives in the published snapshot.

## Error Atomicity

`quantapdf_document_outline()` resets `*out_outline = NULL` before validation/fallible work.

```text
out_outline == NULL                  -> ARGUMENT
document == NULL                     -> ARGUMENT
non-PDF                              -> UNSUPPORTED
valid empty                          -> OK + empty snapshot
repairable structure inconsistency  -> FORMAT
cycle / repeated node                -> FORMAT
malformed outline data               -> existing MuPDF format mapping
unresolvable internal destination    -> FORMAT
valid depth > 256                    -> UNSUPPORTED
allocation / size overflow           -> NOMEM
other MuPDF exception                -> existing status mapper
```

No partial snapshot is ever published. Temporary MuPDF state and partial QuantaPDF allocations are released on failure.

## Implementation Boundary

Production scope:

```text
Modify: include/quantapdf/quantapdf.h
Create: src/pdf_outline.c
Modify: CMakeLists.txt
```

Do not modify for this slice:

```text
src/document.c
src/internal.h
src/links.c
src/pdf_metadata.c
Phase 4 composition/output implementation
existing Page/Render/Text/Search/Image/Links behavior
```

`src/pdf_outline.c` privately owns all outline snapshot records, structural preflight, flattening, string arena, and cleanup logic. No generic PDF-root helper is introduced solely for this feature.

## Deterministic Test Fixtures

### `outline-tree.pdf`

A three-page PDF with deterministic preorder:

```text
0 Chapter 1
1   Section 1.1
2   Website
3   No target
4 Chapter 2
```

It must prove exact preorder indices, hierarchy relations, `SIZE_MAX`, Unicode and ordinary titles, one absent title, one present-empty title, internal page+x/y, external URI, no-destination, open/closed nodes, and post-document-close snapshot lifetime.

### `outline-repairable-bad.pdf`

Contains a deterministic `Parent`, `Prev`, or parent `Last` inconsistency that MuPDF would normally attempt to repair. Expected: `QUANTAPDF_ERROR_FORMAT` and NULL output.

### `outline-cycle.pdf`

Contains a deterministic `Next` and/or child cycle. Expected: `FORMAT`, NULL output, deterministic termination.

### Existing no-outline PDF

Reuse an existing valid PDF to prove `OK + non-NULL snapshot + count 0`.

### `outline-depth-257.pdf`

A deterministic valid PDF with exactly 257 nested outline levels. This checked-in fixture is required, not optional. Expected: `QUANTAPDF_ERROR_UNSUPPORTED` and NULL output.

## Strict TDD Boundary

```text
master cb0f50b0734bcae00fedd529e4f13701c0852866
  ↓
feat/document-outline
```

RED contains only:

```text
tests/fixtures/outline-tree.pdf
tests/fixtures/outline-repairable-bad.pdf
tests/fixtures/outline-cycle.pdf
tests/fixtures/outline-depth-257.pdf
tests/test_pdf_outline.c
tests/CMakeLists.txt
```

No public outline declaration or production implementation may exist at RED.

Acceptable RED: library and every pre-existing test target build/link; only the new outline target fails because the outline type/enum/API are absent. Fixture parse errors, unrelated regressions, crashes, or timeouts are invalid RED.

GREEN adds only:

```text
include/quantapdf/quantapdf.h
src/pdf_outline.c
CMakeLists.txt
```

Exact GREEN head must pass:

```text
Linux strict static + all CTests       ✅
Linux ASan/UBSan + all CTests          ✅
same-head macOS configure/build/test   ✅
same-head Windows DLL build/test       ✅
```

Windows must execute the outline CTest through the shared-library build so the ABI is proven exported, linkable, and runnable.

## Future Mutation Isolation

MuPDF has editable outline iterators, but V1 exposes no mutation path and snapshot indices are never mutation identities. Future insert/rename/move/delete/destination/style operations require a separate design covering mutable document state, transaction/rollback behavior, save/rewrite integration, stable selector identity, and interaction with pre-existing snapshots.

## Architecture Summary

```text
PDF-only
+ existing quantapdf_document
+ immutable quantapdf_outline snapshot
+ preorder flat node table
+ parent / first-child / next-sibling indices
+ SIZE_MAX relation sentinel
+ NONE / INTERNAL / URI destinations
+ flat PDF page + Fitz x/y internal targets
+ snapshot-owned title / external URI
+ is_open only
+ valid empty snapshot
+ iterative read-only structural preflight
+ no silent outline repair
+ cycle/repeated-node rejection
+ private 256-level safety bound
+ document-independent snapshot lifetime
+ snapshot-local indices only
+ mutation deferred to a separate architecture
```
