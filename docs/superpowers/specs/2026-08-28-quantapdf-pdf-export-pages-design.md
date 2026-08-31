# QuantaPDF PDF Page Export + Immutable Output Design

Date: 2026-08-28  
Status: approved design  
Tracks: #19, umbrella #2  
Base: integrated `master` head `1ee52484c7561fb38ecf505d2de0ea83e8b6d9b1`  
MuPDF: pinned `1.28.2` tag commit `fe374accd98a43174a328fa7980d7675e06d5b0d`

## Goal

Add the first Phase 4 PDF Composition primitive: export an ordered selection of pages from one PDF into an immutable QuantaPDF-owned PDF byte buffer.

The primitive must be small enough to remain a stable C ABI building block while being expressive enough to implement subset, split, reorder, and duplicate-page workflows without separate composition engines.

## Architectural decision

V1 uses an **immutable memory-backed `quantapdf_output`** rather than a filename API or a caller-supplied seek/write callback sink.

This is deliberate:

- filenames remain outside the core composition contract;
- no MuPDF `fz_output` type leaks through the public ABI;
- no callback lifetime/error/seekability contract is introduced before it is needed;
- the returned output becomes independent of the source document and MuPDF context;
- later streaming/file helpers can be added without changing the page-selection semantics.

A callback-backed streaming sink remains a possible later optimization for very large outputs. It is not part of this first composition slice.

## Public ABI

```c
typedef struct quantapdf_output quantapdf_output;

QUANTAPDF_API quantapdf_status quantapdf_export_pages(
    quantapdf_document *document,
    const int *page_indices,
    size_t page_count,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_output_data(
    const quantapdf_output *output,
    const unsigned char **out_data,
    size_t *out_size);

QUANTAPDF_API void quantapdf_drop_output(
    quantapdf_output *output);
```

No public struct fields are exposed. `quantapdf_output` is opaque and immutable.

## Selection semantics

`page_indices` uses the same zero-based page numbering as `quantapdf_load_page`.

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

MuPDF 1.28.2's graft implementation creates a fresh destination page dictionary for every `pdf_graft_mapped_page` call while allowing the shared graft map to reuse copied dependent objects. Calling the primitive twice with the same source page therefore yields two distinct destination pages that may safely share grafted resources.

## Argument validation

`quantapdf_export_pages` validates the complete request before creating a successful public output.

It returns `QUANTAPDF_ERROR_ARGUMENT` when:

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

`quantapdf_open` is format-generic because MuPDF document handlers are registered on the document context. Phase 4 composition is intentionally PDF-specific.

The implementation checks the source `fz_document *` with MuPDF's non-owning `pdf_specifics()`. If it returns `NULL`, `quantapdf_export_pages` returns `QUANTAPDF_ERROR_UNSUPPORTED`.

No `pdf_document *` appears in the public header.

The project build includes MuPDF's text document handler, so the unsupported-format contract is exercised with a tiny deterministic plain-text fixture that must first open successfully through `quantapdf_open` and then fail composition with `QUANTAPDF_ERROR_UNSUPPORTED`.

## Composition engine

For a validated PDF source, V1 uses MuPDF's native PDF grafting model rather than rendering/reconstructing pages:

```text
source quantapdf_document
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
copy final bytes to QuantaPDF-owned memory
        |
        v
immutable quantapdf_output
```

One graft map is reused for the whole export call. This preserves sharing of source objects/resources across copied pages where MuPDF can safely reuse a grafted object instead of deep-copying the same dependency repeatedly.

The destination is a new PDF document. The source document is never structurally modified.

## Exact page-graft preservation surface

The MuPDF 1.28.2 implementation of `pdf_graft_mapped_page` creates a new destination page dictionary and copies only the following inheritable page keys:

```text
Contents
Resources
MediaBox
CropBox
BleedBox
TrimBox
ArtBox
Rotate
UserUnit
```

That list is the exact V1 structural preservation basis for this primitive.

Notably, `Annots` is not in the graft copy list. Therefore links, annotations, widgets, and other annotation-array content are **not preserved by V1 page export**. This is an explicit contract boundary, not an accidental omission.

Document-root structures such as metadata, outlines, names, page labels, AcroForm, JavaScript, optional-content configuration, encryption, and signatures are also not copied into the newly created destination document by this primitive.

## Why native page grafting

Rendering a source page to pixels and creating a new PDF page would destroy vector text, fonts, searchable text, image resources, and original page content structure.

`pdf_graft_mapped_page` copies the page content/resource graph and page geometry keys above into a destination PDF. It is therefore the correct Phase 4 primitive for structural page composition.

MuPDF 1.28.2's own `pdfmerge` tool uses a destination `pdf_document`, one `pdf_graft_map` for a copied range, and repeated `pdf_graft_mapped_page` calls. QuantaPDF adopts that underlying primitive but keeps MuPDF types private.

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

After serialization, QuantaPDF obtains the final byte span from the MuPDF buffer and copies it into normal QuantaPDF-owned memory. The MuPDF output, buffer, graft map, and destination PDF are all dropped before the public `quantapdf_output` is returned.

The public output therefore does not retain:

- `fz_context *`;
- `fz_output *`;
- `fz_buffer *`;
- `pdf_document *`;
- `pdf_graft_map *`;
- source `quantapdf_document *`.

## Output ownership and lifetime

Conceptually:

```c
struct quantapdf_output {
    unsigned char *data;
    size_t size;
};
```

The actual definition remains private.

A successful `quantapdf_export_pages` returns a non-NULL output containing a complete PDF file image. The caller may close the source document immediately after export and continue to read the output bytes.

`quantapdf_output_data` returns a borrowed read-only pointer valid until `quantapdf_drop_output(output)`.

On successful `quantapdf_output_data`:

- `*out_data` is non-NULL;
- `*out_size` is greater than zero;
- the byte range begins with a valid PDF header and contains the complete serialized output.

On argument failure, supplied output slots are reset when possible:

- if `out_data != NULL`, set `*out_data = NULL`;
- if `out_size != NULL`, set `*out_size = 0`;
- then return `QUANTAPDF_ERROR_ARGUMENT` if `output`, `out_data`, or `out_size` is NULL.

`quantapdf_drop_output(NULL)` is safe.

## Serialization determinism

For identical source PDF bytes, identical selected indices, and the same QuantaPDF/MuPDF build, repeated calls must produce byte-for-byte identical output.

MuPDF 1.28.2 exposes two relevant `pdf_write_options` fields:

- `reproducible = 1`, which asks the writer to avoid build/version-dependent output where supported;
- `dont_regenerate_id = 1`, which prevents the writer from regenerating a document ID during the write.

V1 starts from `pdf_default_write_options`, sets both fields above, and otherwise keeps default non-incremental write behavior.

The deterministic contract is intentionally scoped to repeated exports under the same pinned implementation/build. V1 does **not** promise that byte sequences are identical across different MuPDF versions, compiler/library versions, or operating systems. Cross-platform verification checks semantic validity and the public page/content contract, not byte equality between platforms.

## Preservation boundary

V1 guarantees that each selected output page contains the source page's grafted `Contents`, `Resources`, supported page boxes, `Rotate`, and `UserUnit` in the requested order. Those are sufficient for the current QuantaPDF page/render/text surfaces to reopen and consume the composed result while preserving native PDF text/vector/resource structure.

V1 explicitly drops or does not propagate:

- page `Annots`, therefore links, annotations, and widgets;
- document metadata / Info dictionary;
- outlines/bookmarks;
- page labels;
- named destinations;
- AcroForm/form-level state;
- signatures;
- source encryption and permissions;
- JavaScript;
- document-level optional-content configuration.

Internal link remapping is consequently not performed because link annotations themselves are outside this V1 graft surface.

A later Phase 5 interactive-document feature may deliberately layer link/annotation/form preservation and destination remapping on top of this composition engine without changing the page selection contract.

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
- temporary QuantaPDF allocation.

Caught MuPDF errors are translated with the existing `quantapdf_status_from_mupdf` mapping.

Normal C allocation failure while copying final bytes returns `QUANTAPDF_ERROR_NOMEM`.

No partially filled public `quantapdf_output` escapes on failure.

## Deterministic fixtures

Add `tests/fixtures/composition-three-page.pdf` with three pages and no interactive/document-level features. Each page has distinct searchable text and exact MediaBox/CropBox geometry:

```text
page 0: PAGE-A, 200 x 200 pt
page 1: PAGE-B, 240 x 180 pt
page 2: PAGE-C, 300 x 150 pt
```

Each CropBox equals its MediaBox and each page uses rotation 0. The differing dimensions ensure an accidental content-only copy, order error, or duplicate-page bug cannot pass by checking text alone.

The primary export sequence is:

```text
[2, 0, 2]
```

Expected output:

```text
page_count = 3
page 0: PAGE-C, 300 x 150 pt
page 1: PAGE-A, 200 x 200 pt
page 2: PAGE-C, 300 x 150 pt
```

Also add `tests/fixtures/composition-non-pdf.txt` containing a short ASCII line. The test first requires `quantapdf_open` to succeed on that fixture and then requires `quantapdf_export_pages` to return `QUANTAPDF_ERROR_UNSUPPORTED`.

The PDF fixture contains no outlines, annotations, forms, encryption, or signatures because those are outside the V1 guarantee and must not blur the first composition RED boundary.

## Reopen verification

The public API currently opens documents by UTF-8 filename, not memory. The export implementation must not add an `open_memory` API merely to make its own test convenient.

The composition test therefore:

1. calls `quantapdf_export_pages`;
2. obtains bytes through `quantapdf_output_data`;
3. closes the source document;
4. proves the borrowed output bytes remain readable;
5. writes those bytes from test code to a unique deterministic file path in the CTest binary directory;
6. reopens that file using existing public `quantapdf_open`;
7. verifies page count, page order, text, and geometry through existing public APIs;
8. removes the temporary file.

The temporary filename is test infrastructure only and does not become part of the library ABI.

## TDD boundary

The first RED commit contains only the wished-for public contract test, CTest wiring, and deterministic fixtures. It does not add production declarations or implementation.

The RED must prove the new surface is genuinely absent at the current master boundary. Expected failure is compilation/link failure on the new opaque output type/functions, while all pre-existing library/test targets continue to build.

The minimal GREEN then adds:

- public opaque type + three functions;
- private byte-buffer output representation;
- focused `src/pdf_export.c` implementation with private `<mupdf/pdf.h>` use;
- root CMake source wiring.

No unrelated Page/Render/Text/Image/Link implementation changes are allowed.

## Required test cases

The slice must cover at least:

1. export `[2, 0, 2]` and verify order + duplicate semantics;
2. output remains valid after source document close;
3. repeated identical exports are byte-for-byte equal in the same test build;
4. reopened output has exactly three pages;
5. reopened page text is `PAGE-C`, `PAGE-A`, `PAGE-C` in order;
6. reopened page geometry is `300x150`, `200x200`, `300x150` points in order;
7. `document == NULL` -> argument error and NULL output;
8. `out_output == NULL` -> argument error;
9. `page_indices == NULL` -> argument error and NULL output;
10. `page_count == 0` -> argument error and NULL output;
11. negative index -> argument error and NULL output;
12. out-of-range index -> argument error and NULL output;
13. mixed-validity list validates atomically and returns no partial output;
14. successfully opened plain-text source -> `QUANTAPDF_ERROR_UNSUPPORTED`;
15. `quantapdf_output_data` success and argument-output reset behavior;
16. `quantapdf_drop_output(NULL)` is safe.

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
include/quantapdf/quantapdf.h
    public opaque output ABI only

src/internal.h
    private immutable output representation only

src/pdf_export.c
    PDF-only validation, private MuPDF PDF API use, page grafting,
    memory serialization, output accessor/drop implementation

CMakeLists.txt
    source registration only
```

Tests remain focused in a new composition test target and fixtures. Existing feature implementations must not be refactored as part of this slice.

## Non-goals

This slice does not add:

- direct filename save API;
- caller callback/stream output sinks;
- memory-based document opening;
- multi-document merge;
- metadata copying;
- outline/bookmark copying;
- link/annotation/widget preservation;
- internal-link remapping;
- page-label remapping;
- forms preservation guarantees;
- annotation editing;
- encryption options;
- signature preservation;
- PDF optimization/garbage-collection policy knobs;
- incremental save;
- new concurrency guarantees;
- source document mutation.
