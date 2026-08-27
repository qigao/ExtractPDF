# ExtractPDF PDF Page Export + Immutable Output Design

Date: 2026-08-28  
Status: approved design  
Tracks: #19, umbrella #2  
Base: integrated `master` head `1ee52484c7561fb38ecf505d2de0ea83e8b6d9b1`  
MuPDF: pinned `1.28.2` tag commit `fe374accd98a43174a328fa7980d7675e06d5b0d`

## Goal

Add the first Phase 4 PDF Composition primitive: export an ordered selection of pages from one PDF into an immutable ExtractPDF-owned PDF byte buffer.

The primitive must be small enough to remain a stable C ABI building block while being expressive enough to implement subset, split, reorder, and duplicate-page workflows without separate composition engines.

## Architectural decision

V1 uses an **immutable memory-backed `extractpdf_output`** rather than a filename API or a caller-supplied seek/write callback sink.

This is deliberate:

- filenames remain outside the core composition contract;
- no MuPDF `fz_output` type leaks through the public ABI;
- no callback lifetime/error/seekability contract is introduced before it is needed;
- the returned output becomes independent of the source document and MuPDF context;
- later streaming/file helpers can be added without changing the page-selection semantics.

A callback-backed streaming sink remains a possible later optimization for very large outputs. It is not part of this first composition slice.

## Public ABI

```c
typedef struct extractpdf_output extractpdf_output;

EXTRACTPDF_API extractpdf_status extractpdf_export_pages(
    extractpdf_document *document,
    const int *page_indices,
    size_t page_count,
    extractpdf_output **out_output);

EXTRACTPDF_API extractpdf_status extractpdf_output_data(
    const extractpdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

EXTRACTPDF_API void extractpdf_drop_output(
    extractpdf_output *output);
```

No public struct fields are exposed. `extractpdf_output` is opaque and immutable.

## Selection semantics

`page_indices` uses the same zero-based page numbering as `extractpdf_load_page`.

The output page order is exactly the order supplied by the caller. Repeated indices are valid and produce repeated output pages.

Example:

```text
source pages: [A, B, C]
indices:      [2, 0, 2]
output:       [C, A, C]
```

This one primitive therefore covers:

- subset: `[1, 3, 5]`;
- reorder: `[2, 0, 1]`;
- duplicate: `[0, 0, 1]`;
- split helper implementation: one call per desired page/range.

V1 does not sort, deduplicate, normalize, or reinterpret the index list.

## Argument validation

`extractpdf_export_pages` validates the complete request before creating a successful public output.

It returns `EXTRACTPDF_ERROR_ARGUMENT` when:

- `document == NULL`;
- `out_output == NULL`;
- `page_indices == NULL`;
- `page_count == 0`;
- `page_count > INT_MAX`, because the current public page-count model is `int`-bounded;
- any supplied index is negative;
- any supplied index is greater than or equal to the source page count.

When `out_output` is supplied, `*out_output` is set to `NULL` before further validation and remains `NULL` on every failure.

All indices are checked before any page is grafted. There is no partial-success public result.

## PDF-only composition boundary

`extractpdf_open` is format-generic because MuPDF document handlers are registered globally on the document context. Phase 4 composition is intentionally PDF-specific.

The implementation down-casts the source `fz_document *` with MuPDF's `pdf_specifics()` / equivalent non-owning PDF-specific check. If the opened document is not backed by a PDF document, `extractpdf_export_pages` returns `EXTRACTPDF_ERROR_UNSUPPORTED`.

No `pdf_document *` appears in the public header.

## Composition engine

For a validated PDF source, V1 uses MuPDF's native PDF grafting model rather than rendering/reconstructing pages:

```text
source extractpdf_document
        |
        v
borrow source pdf_document
        |
        v
pdf_create_document(ctx)
        |
        v
pdf_new_graft_map(ctx, destination)
        |
        +--> pdf_graft_mapped_page(..., source, page_indices[0])
        +--> pdf_graft_mapped_page(..., source, page_indices[1])
        +--> ...
        |
        v
pdf_write_document(..., memory fz_output, write_options)
        |
        v
copy final bytes to ExtractPDF-owned memory
        |
        v
immutable extractpdf_output
```

One graft map is reused for the whole export call. This preserves sharing of source objects/resources across copied pages where MuPDF can safely reuse a grafted object instead of deep-copying the same dependency repeatedly.

The destination is a new PDF document. The source document is never structurally modified.

## Why native page grafting

Rendering a source page to pixels and creating a new PDF page would destroy vector text, fonts, searchable text, image resources, and original page content structure.

`pdf_graft_mapped_page` copies the PDF page and the resource/object graph needed by that page into a destination PDF. It is therefore the correct Phase 4 primitive for structural page composition.

MuPDF 1.28.2's own `pdfmerge` tool uses a destination `pdf_document`, one `pdf_graft_map` for a copied range, and repeated `pdf_graft_mapped_page` calls. ExtractPDF adopts that underlying primitive but keeps MuPDF types private.

## Output serialization

The destination PDF is serialized to an in-memory MuPDF buffer through:

```text
fz_buffer
   ^
   |
fz_new_output_with_buffer
   ^
   |
pdf_write_document
```

After serialization, ExtractPDF obtains the final byte span from the MuPDF buffer and copies it into normal ExtractPDF-owned memory. The MuPDF output, buffer, graft map, and destination PDF are all dropped before the public `extractpdf_output` is returned.

The public output therefore does not retain:

- `fz_context *`;
- `fz_output *`;
- `fz_buffer *`;
- `pdf_document *`;
- `pdf_graft_map *`;
- source `extractpdf_document *`.

## Output ownership and lifetime

Conceptually:

```c
struct extractpdf_output {
    unsigned char *data;
    size_t size;
};
```

The actual definition remains private.

A successful `extractpdf_export_pages` returns a non-NULL output containing a complete PDF file image. The caller may close the source document immediately after export and continue to read the output bytes.

`extractpdf_output_data` returns a borrowed read-only pointer valid until `extractpdf_drop_output(output)`.

On successful `extractpdf_output_data`:

- `*out_data` is non-NULL;
- `*out_size` is greater than zero;
- the byte range begins with a valid PDF header and contains the complete serialized output.

On argument failure, supplied output slots are reset when possible:

- if `out_data != NULL`, set `*out_data = NULL`;
- if `out_size != NULL`, set `*out_size = 0`;
- then return `EXTRACTPDF_ERROR_ARGUMENT` if `output`, `out_data`, or `out_size` is NULL.

`extractpdf_drop_output(NULL)` is safe.

## Serialization determinism

For identical source PDF bytes, identical selected indices, and the same ExtractPDF/MuPDF build, repeated calls must produce byte-for-byte identical output.

MuPDF 1.28.2 exposes two relevant `pdf_write_options` fields:

- `reproducible = 1`, which asks the writer to avoid build/version-dependent output where supported;
- `dont_regenerate_id = 1`, which prevents the writer from regenerating a document ID during the write.

V1 starts from `pdf_default_write_options`, sets both fields above, and otherwise keeps default non-incremental write behavior.

The deterministic contract is intentionally scoped to repeated exports under the same pinned implementation/build. V1 does **not** promise that byte sequences are identical across different MuPDF versions, compiler/library versions, or operating systems. Cross-platform verification checks semantic validity and the public page/content contract, not byte equality between platforms.

## Preservation boundary

V1 guarantees that each selected page is represented by a native grafted PDF page in the requested order, with the content/resources and page geometry needed to reopen and use that page through ExtractPDF's existing page/render/text APIs.

V1 does not promise document-level or interactive semantic preservation/remapping for:

- document metadata / Info dictionary;
- outlines/bookmarks;
- page labels;
- named destinations;
- internal link destination renumbering;
- forms/widgets and form-level state;
- annotation semantics beyond whatever MuPDF page grafting carries as an implementation detail;
- signatures;
- source encryption or permissions;
- JavaScript;
- optional-content configuration at the document level.

Callers must not rely on any of those features surviving this V1 primitive unless a later public contract explicitly adds them.

This boundary is supported by MuPDF's own `pdfmerge` implementation: after grafting a page it performs separate link handling, and its source explicitly leaves internal-link renumbering as additional work. ExtractPDF therefore does not conflate Phase 4 page composition with Phase 5 interactive-document semantics.

## Encryption and signatures

The output is a newly created PDF using default non-encrypted write options. Source encryption is not propagated to the destination.

Digital signatures from the source are not preserved as a valid signed-document guarantee. Selecting, reordering, or duplicating pages changes document structure, so source signature semantics are outside this primitive.

## Errors and cleanup

All MuPDF calls remain inside the existing C exception boundary.

Implementation uses `fz_try` / `fz_always` / `fz_catch` so partial internal state is released on every MuPDF failure:

- destination PDF;
- graft map;
- memory output;
- memory buffer;
- temporary ExtractPDF allocation.

Caught MuPDF errors are translated with the existing `extractpdf_status_from_mupdf` mapping.

Normal C allocation failure while copying final bytes returns `EXTRACTPDF_ERROR_NOMEM`.

No partially filled public `extractpdf_output` escapes on failure.

## Deterministic fixture

Add `tests/fixtures/composition-three-page.pdf` with three pages and no interactive/document-level features. Each page has distinct searchable text and distinct geometry:

```text
page 0: PAGE-A
page 1: PAGE-B
page 2: PAGE-C
```

The page dimensions are deliberately different so an accidental content-only copy, order error, or duplicate-page aliasing bug cannot pass by checking text alone.

The primary export sequence is:

```text
[2, 0, 2]
```

Expected output:

```text
page_count = 3
page 0 text/geometry == source page 2
page 1 text/geometry == source page 0
page 2 text/geometry == source page 2
```

The fixture contains no outlines, annotations, forms, encryption, or signatures because those are outside the V1 guarantee and must not blur the first composition RED boundary.

## Reopen verification

The public API currently opens documents by UTF-8 filename, not memory. The export implementation must not add an `open_memory` API merely to make its own test convenient.

The composition test therefore:

1. calls `extractpdf_export_pages`;
2. obtains bytes through `extractpdf_output_data`;
3. closes the source document;
4. proves the borrowed output bytes remain readable;
5. writes those bytes from test code to a unique deterministic file path in the CTest binary directory;
6. reopens that file using existing public `extractpdf_open`;
7. verifies page count, page order, text, and geometry through existing public APIs;
8. removes the temporary file.

The temporary filename is test infrastructure only and does not become part of the library ABI.

## TDD boundary

The first RED commit contains only the wished-for public contract test, CTest wiring, and deterministic fixture. It does not add production declarations or implementation.

The RED must prove the new surface is genuinely absent at the current master boundary. Expected failure is compilation/link failure on the new opaque output type/functions, while all pre-existing library/test targets continue to build.

The minimal GREEN then adds:

- public opaque type + three functions;
- private byte-buffer output representation;
- focused `src/pdf_export.c` implementation;
- root CMake source wiring.

No unrelated Page/Render/Text/Image/Link implementation changes are allowed.

## Required test cases

The slice must cover at least:

1. export `[2, 0, 2]` and verify order + duplicate semantics;
2. output remains valid after source document close;
3. repeated identical exports are byte-for-byte equal in the same test build;
4. reopened output has exactly three pages;
5. reopened page text is `PAGE-C`, `PAGE-A`, `PAGE-C` in order;
6. reopened page geometry matches the corresponding source pages;
7. `document == NULL` -> argument error and NULL output;
8. `out_output == NULL` -> argument error;
9. `page_indices == NULL` -> argument error and NULL output;
10. `page_count == 0` -> argument error and NULL output;
11. negative index -> argument error and NULL output;
12. out-of-range index -> argument error and NULL output;
13. mixed-validity list validates atomically and returns no partial output;
14. non-PDF source -> `EXTRACTPDF_ERROR_UNSUPPORTED`;
15. `extractpdf_output_data` success and argument-output reset behavior;
16. `extractpdf_drop_output(NULL)` is safe.

An explicit synthetic `page_count > INT_MAX` test is not required because constructing that many readable indices would itself violate the function's memory-access precondition; the implementation still guards it before iterating.

## CI policy

Development follows the established staged policy:

1. deterministic RED on the new branch;
2. Linux exact-head GREEN with strict warnings, normal CTest, ASan/UBSan build and CTest;
3. retain the feature PR as draft while Phase 4 slices are being stacked unless an earlier integration checkpoint is explicitly chosen;
4. Windows/macOS verification is required at the Phase 4 cross-platform checkpoint, with an earlier `full-ci` run allowed when this first output primitive is judged architecture-critical.

No success claim is made from a previous feature-branch workflow after the implementation head changes.

## Files and responsibility boundaries

Expected production responsibility is intentionally narrow:

```text
include/extractpdf/extractpdf.h
    public opaque output ABI only

src/internal.h
    private immutable output representation only

src/pdf_export.c
    PDF-only validation, page grafting, memory serialization,
    output accessor/drop implementation

CMakeLists.txt
    source registration only
```

Tests remain focused in a new composition test target and fixture. Existing feature implementations must not be refactored as part of this slice.

## Non-goals

This slice does not add:

- direct filename save API;
- caller callback/stream output sinks;
- memory-based document opening;
- multi-document merge;
- metadata copying;
- outline/bookmark copying;
- internal-link remapping;
- page-label remapping;
- forms/widgets preservation guarantees;
- annotation editing;
- encryption options;
- signature preservation;
- PDF optimization/garbage-collection policy knobs;
- incremental save;
- new concurrency guarantees;
- source document mutation.
