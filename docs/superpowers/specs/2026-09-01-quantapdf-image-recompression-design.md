# QuantaPDF Image Recompression V1 Design

**Date:** 2026-09-01

**Issue:** [#57](https://github.com/qigao/QuantaPDF/issues/57)

**Target release:** 2.4.0

**Public ABI:** additive ABI v2

## Goal

Add one immutable, explicitly lossy whole-document transform that re-encodes a
strict class of opaque 8-bit DeviceGray and DeviceRGB Image XObjects as JPEG.
It preserves image geometry, indirect-object sharing, resource names, content
operators, page structure, interactive structures, and all ineligible images.

The transform is not part of `quantapdf_rewrite_lossless()`. A caller opts into
pixel loss only by calling the new API with an explicit JPEG quality.

## Locked backend and codec decision

The implementation uses the existing backend split:

- qpdf 12.4.0 discovers the reachable resource graph, decodes supported image
  stream filters, preserves indirect identity, replaces stream data, and
  serializes the private graph;
- PDFium 154.0.8021.0 supplies render and public observation semantics used by
  acceptance tests;
- qpdf's `Pl_DCT` adapter supplies JPEG compression through the libjpeg API;
- the pinned vcpkg baseline resolves that API to libjpeg-turbo 3.2.0.

`stb_image` and `stb_image_write` are not added. QuantaPDF already ships the
qpdf/libjpeg-turbo dependency chain, and adding stb would create a second JPEG
implementation, another notice/audit surface, and a second encoding policy.
The selected components remain permissively licensed: qpdf is Apache-2.0 or
Artistic-2.0, and libjpeg-turbo uses IJG plus BSD-style terms. No MuPDF or AGPL
dependency is permitted.

Upstream evidence:

- qpdf 12.4.0's
  [`pdf-invert-images.cc`](https://github.com/qpdf/qpdf/blob/v12.4.0/examples/pdf-invert-images.cc)
  demonstrates cycle-safe image identity handling, `copyStream()`,
  `qpdf_dl_all`, and `replaceStreamData()`;
- qpdf 12.4.0's
  [`Pl_DCT.hh`](https://github.com/qpdf/qpdf/blob/v12.4.0/include/qpdf/Pl_DCT.hh)
  exposes compression with a per-instance libjpeg configuration callback;
- libjpeg-turbo
  [3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0)
  is the exact version resolved by the repository's pinned vcpkg baseline.

This selection is conditional on the deterministic encoder gate below. A
cross-platform byte mismatch stops implementation before the public API is
committed. It does not silently switch codec or weaken the contract.

## Public API

```c
#define QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES \
    ((size_t)64u * (size_t)1024u * (size_t)1024u)

typedef struct quantapdf_image_recompression_options {
    size_t struct_size;
    int jpeg_quality;
    size_t max_decoded_bytes_per_image;
} quantapdf_image_recompression_options;

#define QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_image_recompression_options, jpeg_quality) + \
     sizeof(int))
#define QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_image_recompression_options))

QUANTAPDF_API quantapdf_status quantapdf_recompress_images(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options,
    quantapdf_output **out_output);
```

The options structure is a single append-only structure:

- a size smaller than `QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE`
  returns `QUANTAPDF_ERROR_ARGUMENT`;
- a larger structure is accepted and unknown tail bytes are ignored;
- `jpeg_quality` must be in `[1, 100]`;
- if `max_decoded_bytes_per_image` is covered by `struct_size`, zero selects
  the 64 MiB default and a nonzero value becomes the per-image hard cap;
- when that field is not covered, the 64 MiB default applies.

`document`, `options`, and `out_output` are required. Whenever `out_output` is
non-NULL, `*out_output` is set to NULL before any other validation or work.
Success publishes a new owning `quantapdf_output`; it remains valid after the
source document is closed. The source and all existing observations remain
unchanged.

This adds one export. The v2 export baseline grows from 87 to 88, the release
version becomes 2.4.0, and `QUANTAPDF_ABI_VERSION` plus SOVERSION remain 2.

## Data flow and responsibility boundary

```text
source bytes + password (borrowed for call; never mutated)
    -> fresh strict QPDF graph (private owner)
    -> signed/encrypted audit and structural preflight
    -> bounded reachable resource traversal
    -> unique eligible QPDFObjGen plan
    -> copied original streams + replacement providers
    -> qpdf decode -> fixed Pl_DCT/libjpeg-turbo profile -> writer pipeline
    -> deterministic QPDFWriter memory buffer
    -> owning quantapdf_output

published output -> reopened through QuantaPDF
    -> PDFium render/observation acceptance checks
```

The public page-image snapshot APIs are not an input to this transform. They
are occurrence-oriented PDFium observations and cannot represent complete
document resource identity. No qpdf, PDFium, JPEG, colorspace, or encoder type
enters the public header.

## Reachable resource graph

The private traversal has these roots:

1. every page's effective `/Resources` dictionary, including inherited page
   resources;
2. recursively reachable Form XObjects from each page resource graph;
3. every page annotation or Widget appearance selected through `/AP` keys
   `/N`, `/R`, and `/D`, including state subdictionaries whose values are
   appearance streams;
4. recursively reachable Form XObjects from each valid appearance stream's
   `/Resources` dictionary.

For each resource owner, only `/Resources /XObject` entries are interpreted.
An Image stream is collected; a Form stream is queued; other XObject subtypes
are ignored. Inline images inside page or Form content streams are never
parsed or rewritten.

Traversal is iterative and cycle-safe. Form/resource owners and collected
images are deduplicated by full `QPDFObjGen`, not object number alone. It
reuses the audit/sanitize work budget:

```text
work_limit = max(4096, source_size * 64)
```

The multiplication is overflow-checked. Each root, appearance edge, resource
owner, XObject edge, and planned image consumes one unit. Exhaustion returns
`QUANTAPDF_ERROR_UNSUPPORTED`; it never falls back to partial traversal.

Malformed structures that this traversal consumes return
`QUANTAPDF_ERROR_FORMAT`, including a non-dictionary effective resources
value, non-dictionary `/XObject`, non-stream Form/Image entry, malformed page
`/Annots`, malformed `/AP`, or an appearance state value that is not a stream.
Unrelated custom dictionary edges are outside this reachability claim.

## Strict eligibility policy

Every reachable Image XObject is classified before mutation.

An image is eligible only when all of these statements are true:

- it is an indirect stream with `/Subtype /Image`;
- `/Width` and `/Height` are positive integers representable as both
  libjpeg `JDIMENSION` and `size_t` calculations;
- `/BitsPerComponent` is exactly integer 8;
- `/ColorSpace` resolves to the name `/DeviceGray` or `/DeviceRGB`;
- `/ImageMask` is absent, null, or boolean false;
- `/SMask` and `/Mask` are absent or null;
- `/Decode` is absent/null or is exactly the identity array for the component
  count: `[0 1]` for gray and `[0 1 0 1 0 1]` for RGB;
- checked `width * height * components` does not exceed the normalized
  `max_decoded_bytes_per_image`;
- qpdf's filterability probe reports that the complete filter chain is
  supported at `qpdf_dl_all`;
- actual decoding succeeds and the decoded byte count is exactly
  `width * height * components`.

The byte cap is part of eligibility. A valid image above it is preserved
unchanged. This makes the maximum raw sample buffer for one encoder invocation
explicit and caller-controlled without making normal calls unbounded.

Valid but ineligible images are preserved byte-for-byte at the stream level.
This includes CMYK, ICCBased, CalRGB/CalGray, Lab, Indexed, Separation,
DeviceN, JPX, soft masks, explicit or color-key masks, stencil masks,
non-8-bpc samples, non-identity Decode arrays, unsupported filter chains,
inline images, and images above the per-image byte cap.

A `/Decode` array of the correct length whose entries are finite numbers but
which is not the identity array is valid and ineligible. A `/Decode` value
with the wrong type or length, a nonnumeric entry, or a non-finite entry is a
malformed consumed image dictionary and returns `QUANTAPDF_ERROR_FORMAT`.

Filter support is classified without consuming decoded bytes by the qpdf
filterability probe. A false probe result is an ineligible/preserve decision.
After a true probe result, a failure while actually decoding that otherwise
eligible candidate is malformed input and returns FORMAT.

Malformed required scalar types, non-finite/out-of-range dimensions, checked
arithmetic overflow, or a decode failure after an image otherwise satisfies
the eligible structural class return `QUANTAPDF_ERROR_FORMAT`. The transform
does not publish a partially processed output.

## Encoder profile

The encoder profile is fixed rather than inheriting library defaults:

- libjpeg API sample precision: 8 bits;
- input and encoded colorspace: `JCS_GRAYSCALE` for DeviceGray and `JCS_RGB`
  for DeviceRGB;
- RGB sampling factors: 1x1 for all three components (no chroma subsampling);
- `jpeg_set_quality(cinfo, jpeg_quality, TRUE)` forces baseline-compatible
  quantization tables;
- DCT method: `JDCT_ISLOW`;
- sequential Huffman coding, `optimize_coding = FALSE`, arithmetic coding
  disabled, no progressive scan script, no restart interval, and no smoothing;
- grayscale writes a fixed JFIF 1.01 marker with density unit 0 and 1x1
  density; RGB writes no JFIF marker and writes an Adobe marker with transform
  0;
- quality 100 is still an encode operation and is not a semantic no-op.

The replacement stream keeps `/Width`, `/Height`, `/ColorSpace`,
`/BitsPerComponent`, placement, and object identity. It installs
`/Filter /DCTDecode`. DeviceRGB also installs
`/DecodeParms << /ColorTransform 0 >>`; DeviceGray removes `/DecodeParms`.
An accepted explicit identity `/Decode` is preserved. `/Length` is managed by
qpdf. The stream is marked not to be filtered again on write.

## Determinism gate

Before the public header or ABI baseline changes, a backend-private encoder
characterization test must encode fixed DeviceGray and DeviceRGB sample arrays
at qualities 40, 90, and 100. It compares complete encoded bytes with checked
golden byte arrays and runs on the same exact commit on Linux, macOS, and
Windows. Repeated calls in one process must also match.

The gate is intentionally stronger than a semantic render comparison. If any
platform produces different JPEG bytes, implementation stops at the private
adapter commit and this design is revised. Runtime environment mutation such
as setting `JSIMD_FORCENONE` is not an accepted workaround because ABI v2 uses
external serialization but does not grant permission to mutate process-wide
environment or codec state.

For a fixed source, normalized options, pinned qpdf/libjpeg-turbo versions,
and platform-independent encoder bytes, the complete PDF output must be
byte-identical across repeated calls. Cross-platform CI compares the same
golden JPEG bytes and one complete checked-in golden output PDF, so both the
codec and serialization boundaries are checked on every platform.

## Memory and lifetime protocol

| Item | Contract |
|---|---|
| Data unit | One indirect Image stream plus checked width, height, component count, and normalized options. |
| Fact source | Original stream bytes in a `copyStream()` handle; the private QPDF graph owns replacement providers. |
| Public inputs | `document` and `options` are borrowed for the synchronous call and are never retained after return. |
| Decoded samples | The copied source stream pipes decoded samples directly into `Pl_DCT`. `Pl_DCT` owns at most one complete raw image buffer, bounded by the normalized per-image cap, until `finish()` returns; QuantaPDF does not make a second raw-sample copy. |
| Encoded bytes | `Pl_DCT` writes directly to the QPDF writer's supplied pipeline; providers do not retain a per-image encoded vector or a second PDF-wide encoded-image cache. |
| Output | The C facade copies the completed writer buffer into the new owning `quantapdf_output`. |
| Capacity | `width * height * components` uses checked multiplication and must be at most the normalized per-image cap. The resource traversal uses the source-derived work limit. |
| Concurrency | One producer/consumer call stack, no queue and no worker threads. ABI v2 still requires caller-side serialization of all public calls. |
| Failure | Any validation, decode, encode, allocation, provider, or writer failure destroys the private graph and shell; `*out_output` remains NULL. |
| Shutdown | The synchronous writer finishes every provider before the private graph is destroyed. No borrowed sample pointer escapes the call. |

The cap bounds the one complete raw-sample buffer owned internally by
`Pl_DCT`; it is not a claim that total process memory equals the cap. QPDF may
also retain compressed source data while decoding, and QPDF parsing plus the
existing memory-output writer retain their graph/output storage. Peak memory
and runtime are measured in implementation benchmarks before release, and no
throughput or allocation reduction is promised by this design.

## Security and error semantics

The transform opens a fresh QPDF graph with warning suppression, recovery
disabled, and later warning inspection. It forces page/object loading, runs the
existing document audit, and returns:

- `QUANTAPDF_ERROR_UNSUPPORTED` for encrypted input, even with a valid open
  password;
- `QUANTAPDF_ERROR_UNSUPPORTED` for any recognized signature structure;
- `QUANTAPDF_ERROR_UNSUPPORTED` when the source-derived traversal work-budget
  multiplication overflows or the budget is exhausted;
- `QUANTAPDF_ERROR_FORMAT` for required parser recovery, consumed malformed
  resource/image structures (including image-dimension/product overflow), or
  eligible-stream decode failure;
- `QUANTAPDF_ERROR_NOMEM` only for observable C or C++ allocation failure;
- `QUANTAPDF_ERROR_BACKEND` for a qpdf/libjpeg runtime failure or unexpected
  backend invariant failure after structurally valid preflight.

Each retry-capable replacement provider owns a latched `quantapdf_status`.
Provider setup or execution records its first non-OK status before throwing to
abort `QPDFWriter`: `std::bad_alloc` latches NOMEM, an explicit private status
latches that status, and `pipeStreamData()` failure or any other codec/provider
exception latches BACKEND. The outer writer boundary returns the latched
status when present; otherwise it applies the normal QPDF exception mapping.
This makes the provider's boolean protocol lossless at the public status
boundary. Candidate decode failures are detected during preflight and remain
FORMAT rather than reaching this provider path.

There is no JavaScript or event execution and no page rasterization. The
private graph is fully discovered and structurally preflighted before its
first replacement. Mutations affect only that private graph.

## Observability

V1 adds no report handle or callback. Success means every image in the strict
eligible class and within the caller's byte cap was recompressed; all valid
ineligible images were preserved. A later additive inspection API may expose
candidate, recompressed, preserved, and reason counts without changing this
transform's behavior.

## Acceptance matrix

The implementation must prove all of the following:

1. undersized/oversized option structures and quality bounds follow the ABI
   contract, and every failure nulls `out_output`;
2. one DeviceRGB and one DeviceGray image becomes `/DCTDecode` with the fixed
   encoder profile;
3. page dimensions, image dimensions, occurrence count, placement quads, and
   page geometry remain unchanged;
4. PDFium render comparison stays within a documented per-channel lossy
   tolerance on controlled fixtures;
5. a shared Image XObject used by multiple pages remains one indirect object
   and is encoded once;
6. nested Form resources and both annotation and Widget appearances are
   traversed for `/N`, `/R`, and `/D`, covering direct appearance streams,
   appearance-state subdictionaries, and nested Forms below every variant;
7. resource/Form cycles terminate and preserve unique identity;
8. valid CMYK, ICCBased, Indexed, mask, soft-mask, stencil, non-8-bpc,
   non-identity Decode, inline, unsupported-filter, and over-cap images remain
   unchanged;
9. a cap of 5 bytes preserves a 2x1 RGB image while a cap of 6 bytes permits
   it, proving the boundary without large allocations;
10. malformed consumed resources, malformed Decode arrays, image-dimension or
    sample-product overflow, and eligible stream data return FORMAT, while
    traversal-budget overflow/exhaustion returns UNSUPPORTED;
11. before/after snapshots prove text, search results, links, annotations,
    forms, outline, metadata, destinations, and page boxes unchanged, and raw
    page/Form content stream bytes compare exactly;
12. source observations are unchanged after success and failure;
13. output survives source close and reopens through both backends;
14. repeated same-quality output is byte-identical;
15. qualities 40 and 90 produce different controlled-image bytes, with the
    expected measured size/error ordering recorded as test evidence rather
    than generalized as a universal promise;
16. quality 100 still rewrites the eligible stream;
17. signed and encrypted inputs fail closed;
18. injected allocation/provider faults publish no output and return the
    precisely latched NOMEM or BACKEND status;
19. a test-only counter keyed by `QPDFObjGen` proves each shared eligible image
    has one provider registration and one provider invocation;
20. the public C test compiles against the sole public header and the
    ABI export checker reports exactly 88 public exports;
21. a controlled quality-90 transform equals the complete checked-in golden
    output PDF on Linux, macOS, and Windows;
22. Linux static, Linux ASan/UBSan, macOS, and Windows pass on one exact SHA.

Two CTest targets are added: the private encoder characterization target and
the public transform target. The integrated baseline grows from 31 to 33.

## Non-goals

V1 does not resize or downsample, select target DPI, choose among codecs,
encode PNG/Flate/JPX, convert colorspaces, flatten alpha, rasterize pages,
rewrite inline images, deduplicate different image objects, target a whole-PDF
size, preserve signatures, compose encryption, incrementally save, expose
qpdf/PDFium/libjpeg types, or provide a generic `compress_pdf` API.
Adding a relocatable CMake package configuration or a standalone static-install
consumer harness is also separate packaging work; this feature does not claim
that capability.

The explicit future composition remains:

```text
decrypt -> recompress_images -> rewrite_lossless -> encrypt
```

Issue #58 owns encryption transforms. Neither composition nor encryption is
part of Image Recompression V1.
