# QuantaPDF Immutable PDF Output Merge Design

Date: 2026-08-28  
Status: approved design  
Tracks: #27, umbrella #2  
Stacked base: #25 / PR #26 head `18a6c0596b68c1ca7b116624a09e27dcf0ac4a7f`  
Branch: `feat/pdf-merge-outputs`

## Goal

Add the first true cross-document PDF composition primitive while preserving QuantaPDF's existing ownership model: every live `quantapdf_document` keeps its own MuPDF context, and no MuPDF object crosses from one document context into another.

Merge therefore consumes immutable `quantapdf_output` values rather than live document handles. It reparses those PDF bytes inside one temporary MuPDF context, appends every input document in caller order, serializes one destination exactly once, and returns a new immutable `quantapdf_output`.

The public API is:

```c
QUANTAPDF_API quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output);
```

This slice closes the Phase 4 `Merge pages/documents` roadmap item without introducing a merge session, a live-document array API, a multi-document page-selection DSL, filenames, or a new output ownership model.

## Architectural problem

The existing single-document composition engine is bound to one live QuantaPDF document:

```text
quantapdf_document
    ├── fz_context *ctx
    └── fz_document *doc
```

`quantapdf_export_pages(...)` obtains the source `pdf_document` from `document->ctx`, creates the destination in the same context, uses one graft map, and serializes the result.

This is correct for one source document but cannot be generalized by simply passing several `quantapdf_document *` handles into one graft loop. Existing document handles may own different `fz_context` instances, and MuPDF objects must not be mixed across those contexts.

Pinned MuPDF 1.28.2's own `pdfmerge` tool confirms the safe model:

- create one `fz_context`;
- create one destination `pdf_document` in that context;
- open every source document in that same context;
- create a fresh `pdf_graft_map` for each source;
- append that source's selected pages;
- drop the source-local graft state before moving to the next source.

QuantaPDF V1 adopts that model, but its sources are immutable output bytes rather than filenames or live document handles.

## Why immutable outputs are the merge boundary

Three API families were considered.

### 1. Immutable output array — selected

```c
quantapdf_merge_outputs(inputs, input_count, out_output);
```

Benefits:

- no live MuPDF object crosses document-context boundaries;
- every input already has clear QuantaPDF ownership/lifetime semantics;
- subset/range/reorder/delete/duplicate compose naturally before merge;
- one destination and one final serialization are sufficient for N inputs;
- no new session lifecycle is required;
- no repeated pairwise serialization is required.

### 2. Live document array — rejected for V1

A shape such as:

```c
quantapdf_merge_documents(documents, count, out_output);
```

looks convenient but would either misuse MuPDF objects from different contexts or secretly serialize/reparse every live document before merge. The latter hides an expensive bridge inside an API whose apparent semantics suggest direct composition.

### 3. Merge session — rejected for V1

A dedicated merge/session object could own one context and reopen all sources itself. That could optimize future workflows, but it adds a new lifecycle, source-open policy, error model, and stateful subsystem before V1 has evidence that such complexity is necessary.

The immutable-output array is therefore the smallest safe boundary that composes with the rest of Phase 4.

## Public semantics

### Input order

Each input contributes its **entire PDF** in exact caller order.

Conceptually:

```text
inputs = [A, B, C]

A pages: A0 A1
B pages: B0
C pages: C0 C1 C2

result:
A0 A1 B0 C0 C1 C2
```

Merge does not sort, deduplicate, normalize, or inspect semantic identity between inputs.

### Single input

`input_count == 1` is valid.

The result is a new, independently owned PDF output created by the same merge engine. V1 promises semantic page equivalence under the Phase 4 preservation contract, but it does **not** promise byte-for-byte identity with the source input.

There is deliberately no one-input memcpy fast path. All valid merge calls follow one parse/graft/serialize engine.

### Duplicate input handles

Repeated input pointers are valid:

```text
inputs = [A, B, A]
```

The result contains all pages from A, then B, then A again.

This is whole-document duplication. It does not require copying or cloning the input `quantapdf_output` object before the call.

### Empty merge

`input_count == 0` is invalid and returns `QUANTAPDF_ERROR_ARGUMENT`.

V1 does not introduce an empty-PDF output special case. This remains consistent with the existing selected-page composition boundary where zero selected pages are invalid.

## Argument validation

When `out_output == NULL`, return `QUANTAPDF_ERROR_ARGUMENT`.

When `out_output != NULL`, set:

```c
*out_output = NULL;
```

before any further validation or fallible work.

Then reject with `QUANTAPDF_ERROR_ARGUMENT`:

- `inputs == NULL`;
- `input_count == 0`;
- any `inputs[i] == NULL`.

The implementation does not expose or validate private byte fields as part of the public ABI. `quantapdf_output` remains opaque; a non-NULL handle produced by QuantaPDF is treated as satisfying the output object's internal invariant.

## Total page-count boundary

The merged PDF page count must fit the same `int` page-index domain used throughout the current public API.

For each source:

1. open the source PDF;
2. obtain its page count;
3. before grafting that source, check the running total without overflow;
4. reject if the new total would exceed `INT_MAX`.

Conceptually:

```c
if (source_page_count > INT_MAX - total_page_count)
    return QUANTAPDF_ERROR_ARGUMENT;
```

The check occurs before grafting pages from the source that would exceed the limit.

It is acceptable that previous sources may already have been grafted into the private destination at that point because the operation is atomic: no destination or partial output is externally visible before final success.

No huge runtime fixture is required to force an `INT_MAX` overflow. The guard is an arithmetic safety requirement verified by code review and ordinary boundary reasoning.

## Temporary-context ownership model

Every merge call creates a fresh temporary MuPDF context that belongs only to that call.

Conceptually:

```text
quantapdf_merge_outputs(...)
        |
        v
fz_new_context(...)
        |
        +--> destination pdf_document
        |
        +--> source A stream/document/graft
        |
        +--> source B stream/document/graft
        |
        +--> ...
        |
        v
serialize destination
        |
        v
drop all MuPDF state + context
```

The merge context is not stored in any public or persistent QuantaPDF object.

The existing `quantapdf_document` structure is unchanged.

No input `quantapdf_output` gains a MuPDF reference, context pointer, or hidden mutable state.

## Opening immutable outputs as PDF sources

Each input is opened from its owned PDF bytes inside the temporary merge context.

The intended MuPDF flow is:

```text
fz_open_memory(ctx, input->data, input->size)
        |
        v
pdf_open_document_with_stream(ctx, stream)
        |
        v
pdf_count_pages(ctx, source)
        |
        v
pdf_new_graft_map(ctx, destination)
        |
        v
pdf_graft_mapped_page(... each source page ...)
```

`fz_open_memory` does not take ownership of the raw memory block. Therefore the input `quantapdf_output` objects must remain alive for the duration of `quantapdf_merge_outputs(...)`.

After the source PDF and its stream have been dropped, the merge engine retains only grafted destination objects. After the merge call returns, the returned `quantapdf_output` is independent of all inputs.

Inputs may be dropped immediately after successful return.

## One graft map per source

The destination document is created once.

A **new `pdf_graft_map` is created for each source input** and reused for all pages copied from that one source.

Conceptually:

```text
destination
  |
  +-- graft map A -> all pages from source A
  |
  +-- graft map B -> all pages from source B
  |
  +-- graft map C -> all pages from source C
```

A graft map is not shared across different source documents.

This matches the source-local resource deduplication model used by MuPDF's own merge tool and prevents source-object-number mappings from one input from leaking into another.

## Strict failure atomicity

Merge is all-or-nothing.

No partial destination is ever returned.

The lifecycle is:

```text
reset out_output
validate arguments
create temporary context
create destination
for every source:
    open stream
    open PDF
    count/preflight pages
    create graft map
    graft all pages
    drop source-local state
serialize destination once
copy serialized bytes into new quantapdf_output
publish *out_output
```

If any step fails:

```text
drop current source-local state
        +
drop partial destination
        +
drop serialization state
        +
drop temporary context
        +
free QuantaPDF-owned temporary/result allocations
        |
        v
return error with *out_output == NULL
```

Earlier successfully grafted inputs do not create a partial-success result.

No input is mutated or consumed on success or failure.

## Error mapping

Merge reuses the existing `quantapdf_status` vocabulary.

No merge-specific status enum is added.

Required mapping:

- public shape/NULL/zero-count/page-total errors -> `QUANTAPDF_ERROR_ARGUMENT`;
- temporary context or QuantaPDF-owned allocation failure -> `QUANTAPDF_ERROR_NOMEM`;
- MuPDF format/syntax parse errors -> existing `QUANTAPDF_ERROR_FORMAT` mapping;
- MuPDF unsupported errors -> `QUANTAPDF_ERROR_UNSUPPORTED`;
- MuPDF system errors -> existing `QUANTAPDF_ERROR_IO` mapping;
- other MuPDF exceptions -> `QUANTAPDF_ERROR_MUPDF`.

The existing `quantapdf_status_from_mupdf(...)` remains authoritative for MuPDF exception mapping.

V1 does not expose which input index failed. A single operation status remains consistent with the rest of the stable C ABI.

## Shared deterministic PDF serializer

The deterministic writer currently embedded in `quantapdf_export_pages(...)` must be extracted into one private helper and reused by merge.

Recommended private interface:

```c
quantapdf_status quantapdf_serialize_pdf(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_output **out_output);
```

This helper is private implementation surface only. It is not declared in the public header.

Its sole responsibility is:

```text
pdf_default_write_options
    reproducible = 1
    dont_regenerate_id = 1
        |
        v
fz_new_buffer
fz_new_output_with_buffer
pdf_write_document
fz_close_output
fz_buffer_storage
        |
        v
allocate quantapdf_output
copy serialized bytes
        |
        v
return immutable output
```

The helper must reset a supplied output slot before its own fallible work and publish the result only after the byte copy is complete.

It owns and cleans up its serialization buffer/output state on both success and failure.

## Required export-path refactor boundary

`quantapdf_export_pages(...)` currently contains both page grafting and deterministic serialization.

This slice may refactor **only the serialization portion** into the shared private helper.

The following existing export behavior must not change:

- PDF-only source check;
- page-count retrieval;
- validation of all selected indices before first graft;
- exact caller order;
- duplicate indices;
- one shared graft map for the single source;
- output reset/error precedence;
- selected-page preservation boundary.

After the refactor the intended split is:

```text
src/pdf_export.c
    validate source/indices
    create destination + single-source graft map
    graft selected pages
    call quantapdf_serialize_pdf(...)

private serializer implementation
    deterministic writer options
    serialize destination to memory
    copy bytes into quantapdf_output

src/pdf_merge.c
    create temporary context + destination
    parse each immutable input in same context
    one graft map per source
    graft all pages
    call quantapdf_serialize_pdf(...)
```

No other Page/Render/Text/Search/Image/Links implementation refactor belongs in this slice.

## Output data/drop lifecycle

`quantapdf_output_data(...)` and `quantapdf_drop_output(...)` remain public lifecycle operations exactly as defined by #19 / PR #20.

They do not need to move files merely because serialization becomes shared.

Avoiding an unnecessary file/lifecycle refactor keeps the merge diff focused on actual shared behavior rather than cosmetic organization.

## Preservation boundary

Merge V1 does **not** expand the Phase 4 selected-page preservation policy.

The page-level state currently copied by pinned MuPDF 1.28.2 `pdf_graft_mapped_page` is the merge contract:

- `Contents`;
- `Resources`;
- `MediaBox`;
- `CropBox`;
- `BleedBox`;
- `TrimBox`;
- `ArtBox`;
- `Rotate`;
- `UserUnit`.

Merge V1 does not promise preservation of:

- annotations or `Annots`;
- external/internal links;
- widgets;
- document metadata / Info;
- outlines / bookmarks;
- page labels;
- named destinations;
- AcroForm state;
- signatures;
- encryption/permissions;
- JavaScript;
- document-level optional-content configuration.

This remains true even though MuPDF's standalone `pdfmerge` utility contains extra logic for some external links/outlines. QuantaPDF merge is composition of Phase 4 immutable outputs, not an implicit upgrade to Phase 5 interactive/document-root preservation.

## Determinism scope

Merge uses the same deterministic writer options as selected-page export:

```text
reproducible = 1
dont_regenerate_id = 1
```

For identical input byte sequences in identical order under the same pinned build, repeated merge calls must return byte-for-byte identical output.

This is a deterministic-build test contract. It does not promise byte identity across different MuPDF versions, compiler/toolchain versions, or platforms unless separately proven and intentionally documented.

A one-input merge does not promise byte identity with that input because the source is reparsed, grafted into a new destination, and serialized again.

## Deterministic fixture strategy

No new binary PDF fixture is required.

Reuse two existing, semantically different fixtures to prove a real cross-source merge.

### Source A

Open:

```text
tests/fixtures/composition-three-page.pdf
```

Export indices:

```text
{2, 0}
```

Expected immutable output A:

```text
page 0: PAGE-C, 300 x 150 pt
page 1: PAGE-A, 200 x 200 pt
```

### Source B

Open:

```text
tests/fixtures/text-one-page.pdf
```

Export index:

```text
{0}
```

Expected immutable output B contains the existing stable text prefix:

```text
Hello Café
```

Close both original source documents **before** calling merge.

This proves that merge consumes only immutable outputs and does not depend on the original document handles or their MuPDF contexts.

## Primary cross-source proof

Call:

```c
const quantapdf_output *inputs[] = {output_a, output_b};
quantapdf_merge_outputs(inputs, 2, &merged);
```

Reopen merged bytes through the existing public test path.

Expected result:

```text
page_count = 3
page 0: PAGE-C, 300 x 150 pt
page 1: PAGE-A, 200 x 200 pt
page 2: text contains "Hello Café"
```

The test must verify exact page order and sufficient content/geometry to distinguish the sources.

## Repeated-merge determinism proof

Call the same merge twice with the same `A,B` inputs.

The two returned outputs must have:

- the same size;
- byte-for-byte identical contents.

This proves the new merge path uses deterministic serialization.

## Single-input proof

Call:

```text
merge({A})
```

Expected result:

```text
PAGE-C
PAGE-A
```

with the same page geometry/content semantics as A.

The test must **not** require:

```text
bytes(merge({A})) == bytes(A)
```

because V1 intentionally defines one-input merge as semantic reserialization, not a copy fast path.

## Duplicate-input proof

Call:

```text
merge({B, A, B})
```

Expected pages:

```text
B page
PAGE-C
PAGE-A
B page
```

This proves duplicate pointers are valid and whole-document multiplicity/order is preserved.

The B pages may be identified by stable text; the A pages retain their known geometry and labels.

## Result lifetime proof

After one successful merge:

1. drop all input outputs A/B;
2. keep merged output alive;
3. obtain/reuse merged bytes and reopen the merged PDF;
4. verify the merged pages again.

This proves the result owns copied bytes independently of all input outputs.

The inverse lifetime is also implicit: successful merge does not consume or alter inputs; callers may continue using A/B until they choose to drop them.

## Failure/argument tests

Required deterministic argument cases:

1. `out_output == NULL` -> `QUANTAPDF_ERROR_ARGUMENT`;
2. `inputs == NULL` with valid output slot -> `QUANTAPDF_ERROR_ARGUMENT`, output reset;
3. `input_count == 0` with a non-NULL input pointer -> `QUANTAPDF_ERROR_ARGUMENT`, output reset;
4. input array containing a NULL element -> `QUANTAPDF_ERROR_ARGUMENT`, output reset.

The output-reset sentinel pattern used by existing tests should be reused.

No artificial malformed `quantapdf_output` object is constructed in tests because the type is opaque and the public API provides no constructor for invalid output handles.

## RED boundary

The first implementation-stage commit is **merge-test-only**.

It adds:

- `tests/test_pdf_merge.c`;
- CTest registration / Windows DLL copy-list wiring;
- no public merge declaration;
- no production merge implementation;
- no shared serializer refactor yet.

The intended RED is that all existing production targets and tests continue to build, while only the new merge test fails because `quantapdf_merge_outputs(...)` is absent.

The RED must not be manufactured through fixture corruption, disabled symbols, or production stubs.

## Minimal GREEN boundary

After the RED is observed, GREEN may add only the production pieces required by this design:

- one public `quantapdf_merge_outputs(...)` declaration;
- focused `src/pdf_merge.c`;
- one private shared deterministic serializer implementation/declaration;
- the minimum `src/pdf_export.c` change needed to call that serializer;
- root CMake source registration;
- internal declarations required to share the serializer.

No new public types are required.

No `quantapdf_document` layout change is required.

No direct-save/filename API is required.

## Expected file responsibilities

Likely production/test surface:

```text
include/quantapdf/quantapdf.h
    add quantapdf_merge_outputs declaration

src/pdf_merge.c
    validate input array
    create temporary context/destination
    open immutable input bytes as source PDFs
    running page-count guard
    one graft map per source
    append all pages in exact input order
    atomic cleanup
    call shared serializer

src/pdf_output.c   (or another focused private name chosen in the plan)
    deterministic pdf_write_document -> immutable quantapdf_output

src/internal.h
    private serializer declaration only if required

src/pdf_export.c
    retain existing selected-page graft behavior
    replace inline writer/copy code with shared serializer call

CMakeLists.txt
    register new focused production source(s)

tests/test_pdf_merge.c
    cross-source order, duplicate/single input, lifetime, determinism, errors

tests/CMakeLists.txt
    register CTest / Windows DLL copy target
```

The implementation plan may choose the exact private serializer filename, but it must preserve these responsibility boundaries.

## Stacked branch / PR strategy

PR #20, #22, #24, and #26 are still stacked/unmerged.

Merge therefore starts from the exact current #26 head:

```text
master
  |
  +-- feat/pdf-export-pages           PR #20
        |
        +-- feat/pdf-export-range     PR #22
              |
              +-- test/pdf-order-contract    PR #24
                    |
                    +-- test/pdf-delete-contract   PR #26
                          |
                          +-- feat/pdf-merge-outputs   #27 / merge PR
```

The future Merge PR base is `test/pdf-delete-contract`, not `master`, until earlier Phase 4 slices are integrated.

At the spec gate, the merge branch must contain only this design document beyond the #26 base.

Retargeting after upstream integration is a later integration task and must preserve the merge feature tree/evidence.

## CI policy

This slice creates a new public ABI, a new temporary MuPDF context, memory-stream PDF parsing, multi-source grafting, and a shared writer refactor. It is therefore an architecture checkpoint.

Required sequence:

1. exact-head RED proves only the merge API is missing;
2. exact-head Linux strict static build + all normal CTests pass;
3. exact-head Linux ASan/UBSan configure/build + all CTests pass;
4. apply/run `full-ci` on the same final feature head;
5. Linux, macOS, and Windows all pass before the Merge slice is considered architecture-complete.

Unlike the recent tests-only reorder/delete closures, macOS/Windows are not deferred here.

No passing workflow from an earlier SHA can be used as completion evidence for a later head.

## Acceptance criteria

The Merge roadmap item can be marked complete only when all of the following are true:

- public ABI is exactly the approved output-array form;
- no live document/MuPDF object is used across contexts;
- one temporary context/destination is used per merge call;
- each source input gets its own graft map;
- inputs are appended in exact whole-document caller order;
- duplicate and single inputs follow the approved semantics;
- total page-count overflow is guarded;
- failure is atomic and output reset semantics are proven;
- result lifetime is independent of inputs;
- preservation does not exceed the existing Phase 4 page-graft boundary;
- export and merge use one private deterministic serializer;
- all preexisting composition tests remain green after the serializer refactor;
- merge deterministic tests pass;
- Linux normal + ASan/UBSan pass;
- final same-head Linux/macOS/Windows full-ci passes;
- scope review finds no merge session, live-document merge API, selection descriptor, interactive preservation, or filename-save expansion.

## Non-goals

This slice does not add:

- `quantapdf_merge_documents(...)` for live documents;
- a merge/session/builder handle;
- incremental append mutation of an existing output;
- pairwise-only merge API;
- per-input range/index descriptors;
- automatic page selection during merge;
- automatic filename generation;
- direct file saving;
- empty-PDF creation;
- mutation of source documents or outputs;
- link/annotation/widget copying;
- metadata/outline/page-label/named-destination merging;
- form/signature/encryption preservation;
- JavaScript or optional-content merging;
- new concurrency guarantees;
- a new error enum or per-input diagnostic object.
