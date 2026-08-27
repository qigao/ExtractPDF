# ExtractPDF Structured Text Snapshot Design

Date: 2026-08-27  
Status: approved design, implementation not started  
Tracks: #9, umbrella #2  
Stacked base: plain-text head `2eef49e06c744071897cf44265a47863aa7720cd`

## Goal

Add a stable, MuPDF-independent structured-text ABI for blocks, lines, and style spans. The snapshot must reuse the Page+Render coordinate contract, survive page/document destruction, and become the shared source for later search geometry without exposing MuPDF's in-development `fz_stext_*` layouts.

## Why an ExtractPDF-owned snapshot

MuPDF 1.28.2 explicitly describes the structured-text data structures as in development and subject to change. Returning or mirroring `fz_stext_page`, `fz_stext_block`, `fz_stext_line`, or `fz_stext_char` would therefore couple the public ABI to an unstable upstream implementation.

The V1 API instead copies the relevant content into one opaque immutable `extractpdf_text_page` snapshot. Public traversal is index-based, which is straightforward for C and .NET/PInvoke and leaves storage/layout free to change internally.

## Public types

```c
typedef struct extractpdf_text_page extractpdf_text_page;

typedef struct extractpdf_text_block_info {
    size_t struct_size;
    extractpdf_rect bounds;
} extractpdf_text_block_info;

typedef struct extractpdf_text_line_info {
    size_t struct_size;
    extractpdf_rect bounds;
    float direction_x;
    float direction_y;
    int writing_mode;
} extractpdf_text_line_info;

typedef struct extractpdf_text_span_info {
    size_t struct_size;
    extractpdf_rect bounds;
    float font_size;
    uint32_t argb;
    uint32_t bidi_level;
} extractpdf_text_span_info;
```

`extractpdf.h` therefore adds `<stdint.h>` but no MuPDF header or type.

### C identifier rule

C typedef names and function names share the ordinary identifier namespace. Therefore the accessor functions deliberately use `get_*_info` names rather than colliding with the `extractpdf_text_*_info` typedefs.

### Versioned output structs

Each `*_info` call treats `struct_size` as caller input. V1 requires at least the V1 size (`offsetof(last_field) + sizeof(last_field)`). On success the library fills only fields known to its version and preserves `struct_size`. Future fields may be appended; future libraries must continue accepting the V1 size and only write fields present in the caller-provided size.

On an argument/index failure, known output fields are reset to zero while preserving the caller's `struct_size` when a non-NULL info pointer was supplied.

## Public API

```c
EXTRACTPDF_API extractpdf_status extractpdf_extract_structured_text(
    extractpdf_page *page,
    extractpdf_text_page **out_text);

EXTRACTPDF_API extractpdf_status extractpdf_text_block_count(
    const extractpdf_text_page *text,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_block_info(
    const extractpdf_text_page *text,
    size_t block_index,
    extractpdf_text_block_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_line_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_line_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    extractpdf_text_line_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_span_count(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t *out_count);

EXTRACTPDF_API extractpdf_status extractpdf_text_get_span_info(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    extractpdf_text_span_info *out_info);

EXTRACTPDF_API extractpdf_status extractpdf_text_span_text(
    const extractpdf_text_page *text,
    size_t block_index,
    size_t line_index,
    size_t span_index,
    const char **out_utf8,
    size_t *out_size);

EXTRACTPDF_API void extractpdf_drop_text_page(
    extractpdf_text_page *text);
```

## Lifetime and ownership

- `extractpdf_extract_structured_text` sets `*out_text = NULL` on every failure.
- A successful snapshot owns all arrays and strings it exposes and no longer depends on `extractpdf_page`, `extractpdf_document`, `fz_context`, or any MuPDF object.
- The source page and document may be dropped immediately after extraction.
- `extractpdf_text_span_text` returns a borrowed NUL-terminated UTF-8 pointer owned by the snapshot. `out_size` excludes the trailing NUL.
- Borrowed span pointers remain valid until `extractpdf_drop_text_page`.
- `extractpdf_drop_text_page(NULL)` is safe.
- V1 does not broaden the existing single-thread contract.

## Content semantics

### Scope

V1 structured text contains only MuPDF `FZ_STEXT_BLOCK_TEXT` blocks. Image, vector, grid, and structure-tree blocks are deliberately not projected into this API; page-observed image extraction remains its own Phase 3 feature.

Block order and line order follow MuPDF's extracted structured-text order. ExtractPDF does not claim to reconstruct a higher-level semantic reading order beyond what MuPDF returns.

### Extraction options

V1 calls MuPDF structured-text extraction with default options (`flags = 0`):

- ligatures are expanded;
- horizontal whitespace is normalized by MuPDF;
- images are ignored;
- no dehyphenation is requested;
- no structure/vector/table collection is requested.

A future options-bearing API can be added separately without changing the V1 function.

### Coordinates

Every rectangle uses the Phase 2 Fitz page-space contract:

- CropBox top-left is the displayed-page origin;
- coordinates are points (72 points/inch);
- Y increases downward;
- geometry is independent of render DPI/zoom.

Block and line bounds copy MuPDF's structured-text bounds. Span bounds are the axis-aligned union of the quads of the characters contained in that span.

### Lines

`direction_x` / `direction_y` copy MuPDF's normalized line baseline direction. `writing_mode` is `0` for horizontal and `1` for vertical, matching the semantic meaning of MuPDF's line mode without exporting a MuPDF enum.

### Spans

ExtractPDF forms a new span whenever consecutive characters differ in style identity. V1 style identity consists of:

- MuPDF font identity;
- font size;
- sRGB ARGB color;
- bidi level;
- MuPDF character flags (used internally only).

The public span exposes font size, ARGB (`0xAARRGGBB`), and bidi level. Font names and MuPDF character flags are intentionally not part of V1; they can be added later through append-only info fields or separate accessors if a real caller requires them.

Span text is UTF-8 generated from the Unicode code points in the structured-text characters. It does not include an artificial line break.

## Internal snapshot model

`src/structured_text.c` owns the projection from MuPDF into ExtractPDF storage. The opaque snapshot contains compact arrays conceptually equivalent to:

```text
extractpdf_text_page
  blocks[]  -> first_line + line_count + bounds
  lines[]   -> first_span + span_count + bounds + direction + writing_mode
  spans[]   -> first_char + char_count + text offset/size + bounds + style
  chars[]   -> Unicode code point + Fitz quad + span/text byte metadata
  strings   -> NUL-terminated UTF-8 span strings
```

The internal `chars[]` array is intentionally retained even though V1 has no character accessor. Later search can operate on the same snapshot and return exact character/search quads without re-running MuPDF extraction.

No internal pointer in the snapshot points back into MuPDF memory after `extractpdf_extract_structured_text` returns.

## Error handling

- NULL handles/output pointers, undersized `struct_size`, and out-of-range indices return `EXTRACTPDF_ERROR_ARGUMENT`.
- Count outputs are reset to `0` on failure when supplied.
- Span text outputs are reset to `NULL` / `0` on failure when supplied.
- Allocation/overflow failures return `EXTRACTPDF_ERROR_NOMEM` and unwind all partially built arrays/strings.
- MuPDF exceptions are caught before the C ABI boundary and translated through the existing status mapping.
- No global/TLS last-error state is added.

## Deterministic TDD fixture

Add an ASCII PDF fixture whose single text line contains two adjacent style runs:

1. `Hello ` — Helvetica, 18 pt, opaque black;
2. `Caf\351` — Helvetica, 12 pt, opaque red, where WinAnsi `\351` must surface as UTF-8 `é` (`C3 A9`).

The fixture is authored with exact stream length/xref offsets rather than relying on MuPDF repair mode.

The contract tests lock:

- successful immutable snapshot creation;
- one text block / one line / two spans for the fixture;
- block/line/span geometry is finite, ordered, and contained consistently;
- line direction and writing mode;
- exact span font sizes and ARGB colors;
- UTF-8 span text and byte lengths;
- LTR bidi level for the fixture;
- invalid/NULL arguments and out-of-range indices;
- `struct_size` validation/output reset;
- empty page produces a valid snapshot with zero text blocks;
- snapshot data remains valid after page/document destruction;
- `drop_text_page(NULL)` safety.

## CI policy

Follow the repository's Linux-first development policy:

- each structured-text slice runs Linux build + CTest + ASan/UBSan;
- Windows/macOS stay skipped during small RED/GREEN iterations;
- Phase 3 gets one exact-head Linux/macOS/Windows `full-ci` checkpoint after the related Content features are assembled.

## Non-goals

This slice does not add:

- public character-level accessors;
- font-name/font-file APIs;
- search APIs;
- image/vector/structure-tree blocks;
- reading-order reconstruction beyond MuPDF's order;
- structured-text extraction options;
- concurrency guarantees.
