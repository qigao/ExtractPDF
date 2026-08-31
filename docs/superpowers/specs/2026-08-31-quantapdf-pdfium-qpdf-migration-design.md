# QuantaPDF PDFium + qpdf Migration Design

**Date:** 2026-08-31  
**Status:** Approved direction  
**Replaces:** the MuPDF implementation assumptions in the Phase 1-6 specs  
**Decision:** use PDFium and qpdf together; remove MuPDF completely

## 1. Goal

Replace the MuPDF-backed implementation with a permissively licensed two-engine kernel while preserving the public `quantapdf_*` C ABI wherever the existing contract can be reproduced safely.

- PDFium owns rendering, page/text/search observation, image decoding, links, annotations, form-facing behavior, and high-level page-object editing.
- qpdf owns strict PDF object-graph inspection, immutable cloning, page-tree/document-root rewrites, garbage collection, deterministic serialization where possible, and encryption policy.
- QuantaPDF owns public types, status mapping, UTF-8 paths, immutable snapshots/outputs, failure atomicity, validation stricter than either backend, and cross-engine semantic reconciliation.

The final repository and installed package must contain no MuPDF binary, header, package dependency, source identifier, license notice, documentation dependency, or runtime fallback.

## 2. License and supply-chain boundary

Pinned dependencies:

- PDFium `154.0.8021.0`, community binary release `chromium/8021` from `bblanchon/pdfium-binaries`, built with `pdf_enable_v8=false`, `pdf_enable_xfa=false`, `pdf_use_partition_alloc=false`.
- qpdf `12.4.0` from the existing vcpkg baseline `f74a2eade17a628413746557d04db25ccf6e76f9`, with no optional OpenSSL/GnuTLS/Zopfli feature in the first migration stage.

Pinned PDFium SHA-256 values:

| Platform | Artifact | SHA-256 |
|---|---|---|
| Windows x64 | `pdfium-win-x64.tgz` | `adac8ce034015427b5daa81f8eeddfcc8e84bc2a9f036f007890ff18bd4388c4` |
| Linux x64 | `pdfium-linux-x64.tgz` | `685f7930cd184ea22cd77afe707c1cf53b173d18118b6e16cb213c9277d7cdc3` |
| macOS x64 | `pdfium-mac-x64.tgz` | `0e770fda56c6726a08fab84c6306ad91eceb10589020ce3a407fad3ebcbe7bb2` |
| macOS arm64 | `pdfium-mac-arm64.tgz` | `994600fa28974ce09a1c51c35039e808a6bc8ea3839050322c101ab229ad5c96` |

Every download must use TLS and an exact hash. Unsupported CPU/OS combinations fail during configure. The installed QuantaPDF package must carry PDFium's root `LICENSE`, its `licenses/` directory, qpdf's Apache-2.0/MIT notices, and a QuantaPDF third-party manifest. No V8 or XFA binary is permitted.

## 3. Public ABI policy

`include/quantapdf/quantapdf.h` remains backend-neutral. No `FPDF_*`, `QPDF*`, C++ type, backend enum, object number, or raw backend handle enters it.

Existing public calls keep their signatures unless parity is impossible without creating a false preservation guarantee. In that case:

1. retain the signature;
2. return `QUANTAPDF_ERROR_UNSUPPORTED` before mutation/publication;
3. document the narrowed valid-input boundary;
4. add a deterministic regression fixture.

No operation silently changes page order, interactive structures, security, or unrelated document-root state merely because a backend API is lossy.

## 4. Internal architecture

### 4.1 Document ownership

`quantapdf_document` owns:

- the complete source bytes for its lifetime;
- an authenticated PDFium `FPDF_DOCUMENT` over those bytes;
- the password copy needed to open the lazy qpdf graph consistently;
- a lazy opaque qpdf bridge handle;
- document-local snapshot/page ownership bookkeeping.

PDFium memory-load APIs require source bytes to outlive the document. qpdf receives the same immutable byte sequence. Neither backend observes a caller-owned buffer.

### 4.2 PDFium runtime

PDFium requires process-wide initialization and documents that its public API is not thread-safe. QuantaPDF therefore uses one private C++ runtime module with:

- `std::once_flag` for `FPDF_InitLibraryWithConfig()`;
- one process-wide recursive mutex around every PDFium call;
- JavaScript and XFA excluded at build time;
- no public init/shutdown API in migration V1;
- no `FPDF_DestroyLibrary()` while any QuantaPDF code may still execute.

This replaces the old “no mutable process-global backend state” invariant with: no caller-visible mutable global PDF state, and all unavoidable PDFium process state is private and serialized.

### 4.3 qpdf bridge

Only `.cpp` bridge files include qpdf C++ headers. Every exported bridge function is `extern "C"`, accepts C-compatible values, catches `QPDFExc`, `std::bad_alloc`, `std::exception`, and unknown exceptions, and maps them without allowing a C++ exception to cross the C ABI.

The first bridge surface is intentionally small:

```c
typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;

quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document);

quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count);

void quantapdf_qpdf_close(quantapdf_qpdf_document *document);
```

Feature-specific object operations are added only when the corresponding migration phase proves the need. There is no generic public object API and no backend service locator.

### 4.4 Backend boundary

Use focused adapters rather than a runtime-selectable factory:

```text
public quantapdf_* API
        |
        +-- backend/pdfium_runtime.cpp  process lifetime + serialization
        +-- backend/pdfium_document.c   high-level PDFium document/page calls
        +-- backend/qpdf_document.cpp   strict object graph and writer bridge
        +-- feature modules             QuantaPDF semantics and owned snapshots
```

MuPDF and the new backends must never be selectable alternatives in a release build. Coexistence is permitted only on the migration branch until the final removal gate.

## 5. Status mapping

PDFium load failures map from `FPDF_GetLastError()`:

| PDFium error | QuantaPDF status |
|---|---|
| `FPDF_ERR_FILE` | `QUANTAPDF_ERROR_IO` |
| `FPDF_ERR_FORMAT` | `QUANTAPDF_ERROR_FORMAT` |
| `FPDF_ERR_PASSWORD` | `QUANTAPDF_ERROR_PASSWORD` |
| `FPDF_ERR_SECURITY` | `QUANTAPDF_ERROR_UNSUPPORTED` |
| allocation failure observed by QuantaPDF | `QUANTAPDF_ERROR_NOMEM` |
| other backend failure | `QUANTAPDF_ERROR_MUPDF` replacement described below |

The legacy enum member `QUANTAPDF_ERROR_MUPDF` is renamed during Phase A to `QUANTAPDF_ERROR_BACKEND` while retaining numeric value `7`; `quantapdf_status_string()` returns `"backend error"`. This is an intentional source-level breaking cleanup. No new code may expose “MuPDF” in diagnostics.

qpdf syntax/damaged-file exceptions map to `FORMAT`, password exceptions to `PASSWORD`, unsupported contract boundaries to `UNSUPPORTED`, allocation to `NOMEM`, filesystem failures to `IO`, and remaining exceptions to the backend-error value.

## 6. Semantic authority

When both engines expose the same concept:

- qpdf is authoritative for raw structure, page-tree identity, encryption dictionaries, signatures, object reachability, and serialization.
- PDFium is authoritative for rendered geometry, decoded text, search, bitmap output, and high-level annotation/form observations.
- QuantaPDF preflight must reconcile counts and selected identities before a transform. A mismatch fails closed; it is never resolved by guessing.

PDFium private C++ headers under `core/`, `fpdfsdk/`, or `third_party/` are forbidden. Only headers shipped in the binary artifact's `include/` public directory may be included.

## 7. Migration phases

### Phase A: build and backend foundation

Pin and import both engines, package licenses, add runtime/bridge smoke tests, repair the existing compiler-only baseline failures, and remove the MuPDF-specific public error name. MuPDF remains only as a transitional implementation dependency.

### Phase B: immutable read path

Replace document/page lifecycle, rendering, plain/structured text, search, image occurrence/bitmap, and links. Source bytes become the shared backend input. Preserve current public observations or narrow them explicitly.

### Phase C: document and interactive snapshots

Replace metadata, outlines, annotations, AcroForm fields/widgets/values/options, appearance-state classification, and signed-document detection.

### Phase D: output and composition

Replace export/range/reorder/duplicate/delete/merge/output serialization. qpdf owns the cloned graph and writer; PDFium reopens every published output for semantic verification.

### Phase E: mutation

Replace annotation and form-value editors with private qpdf graphs plus PDFium post-write verification. Preserve atomic rollback by discarding the private graph on any failure.

### Phase F: page transforms

Replace CropBox, MediaBox Trim, and Poster Split using qpdf object writes and PDFium coordinate/render verification. Existing fail-closed interactive/navigation tests remain normative.

### Phase G: removal and roadmap reset

Remove every MuPDF source path, include, symbol, CMake target, vcpkg dependency, license reference, and MuPDF-specific test helper. Rewrite #48/#55-#58 around the new backend capabilities. Run the complete three-platform suite on one SHA.

## 8. Determinism and security

qpdf writer options are explicit and pinned. Deterministic operations require repeated-call and reopen/rewrite byte comparisons. Encryption and re-encryption are the sole production-randomness exception and must use qpdf's secure provider without a public deterministic seed.

PDFium is built without V8/XFA. QuantaPDF never calls form action, JavaScript, document action, or event execution APIs. Loading, observation, and render APIs must not activate document behavior.

Every mutation happens on a private graph created from immutable source bytes. Failed operations publish no output and leave the source handle unchanged.

## 9. Testing gates

Every phase must pass:

1. compile RED for the new internal interface;
2. focused runtime RED on existing deterministic fixtures;
3. GREEN on Windows Release;
4. all current CTests relevant to the phase;
5. Linux static plus ASan/UBSan;
6. macOS arm64 and Windows shared-library CI on the exact same SHA;
7. output reopen through both qpdf and PDFium where a PDF is published;
8. no backend exception across the public C ABI;
9. license/artifact hash audit.

Final removal additionally requires:

```text
rg -i "mupdf|libmupdf|unofficial::libmupdf|\bfz_[a-z0-9_]+" .
```

to return no repository content, excluding `.git` and ignored build/worktree directories. Binary dependency inspection must show PDFium and permitted qpdf dependencies only.

## 10. Non-goals

- Depending on PDFium private C++ APIs.
- Shipping a runtime backend selector.
- Keeping MuPDF as an optional fallback.
- Preserving undocumented MuPDF repair behavior.
- Enabling V8, XFA, JavaScript, or PDF form events.
- Expanding the public ABI while migration parity is still unsettled.
- Implementing new #48 features before the existing surface is migrated and MuPDF is removed.
