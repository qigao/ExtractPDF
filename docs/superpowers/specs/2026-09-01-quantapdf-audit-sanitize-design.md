# QuantaPDF Document Audit and Sanitize V1 Design

**Status:** Approved for implementation by the continuing development request

## Goal

Add a structure-level security audit and an immutable sanitizer without exposing
QPDF objects or changing the v2 ABI contract.

```c
QUANTAPDF_API quantapdf_status quantapdf_document_audit(
    quantapdf_document *document,
    quantapdf_audit_result *out_result);

QUANTAPDF_API quantapdf_status quantapdf_sanitize(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output);
```

The audit reports the presence of reachable PDF structures that can execute
actions, leave the document, carry embedded payloads, or invalidate a rewrite.
The sanitizer removes explicitly selected classes and returns a new owning PDF.
Neither operation mutates the source document.

## Public contract

`quantapdf_audit_result` is a size-tagged, single-result output structure:

```c
typedef struct quantapdf_audit_result {
    size_t struct_size;
    uint32_t findings;
} quantapdf_audit_result;
```

V1 findings are independent bits:

- JavaScript action;
- launch action;
- external action (`URI`, `GoToR`, `GoToE`, `SubmitForm`, or `ImportData`);
- other active or unknown action (every action other than internal `GoTo` and
  the preceding named classes);
- embedded file;
- XFA;
- rich-media annotation (`RichMedia`, `3D`, `Movie`, `Sound`, or `Screen`);
- signature structure;
- encryption.

A signature finding means that a populated effective `/FT /Sig` field or a
catalog `/Perms` signature is present. It is not a cryptographic-validity
claim. An encryption finding is returned even when the caller supplied the
correct password to `quantapdf_open()`.

`quantapdf_document_audit` requires a non-NULL document and result. The caller
sets `struct_size`; V1 requires at least `QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE`.
The function clears `findings` before backend work whenever that field is
addressable. A larger structure is accepted and only the known V1 prefix is
written, allowing a future library to extend the structure without overwriting
an older caller.

`quantapdf_sanitize` requires a non-NULL document and output pointer, a nonzero
combination of known flags, and resets `*out_output` to NULL before validation.
Success returns independent owning bytes that remain valid after the source is
closed. Unknown or zero flags return `QUANTAPDF_ERROR_ARGUMENT`.

Adding the two functions is an append-only ABI v2 change. The release becomes
2.3.0, `QUANTAPDF_ABI_VERSION` and shared-library major remain 2, and the exact
Windows export baseline grows from 85 to 87 names.

## Audit model

The backend reparses source bytes into a fresh private QPDF graph with recovery
disabled and warnings captured. It audits only objects reachable from the
catalog root. Unreferenced garbage cannot execute and does not create a
finding. Content stream bytes and ordinary strings are not searched for words
such as `JavaScript`; this prevents false positives and keeps the API a PDF
structure audit rather than a malware-content scanner.

Executable action positions are catalog `/OpenAction`, dictionary `/A`,
dictionary `/AA` entries, action `/Next` chains, and the catalog JavaScript name
tree. Internal `/GoTo` actions and destination arrays are navigation, not
findings. Malformed action containers return `QUANTAPDF_ERROR_FORMAT` instead
of being silently skipped.

The catalog `/Names /JavaScript` value is a strict PDF name tree, not merely a
presence marker. Its iterative postorder walker accepts an empty root and empty
nodes without `/Limits`; otherwise it validates dictionary nodes, mutually
exclusive `/Kids` and `/Names`, dictionary children, even alternating
string/action pairs, and every action dictionary through the shared classifier.
Leaf keys are raw string bytes in strictly increasing order. Child subtree
ranges are strictly increasing and nonoverlapping. `/Limits` is optional for
compatibility, but whenever present it is exactly two strings equal to the
actual first and last key of that node's nonempty subtree; an empty node cannot
have `/Limits`. It follows every action
`/Next` chain, so a JavaScript tree can report launch, external, or other action
bits in addition to the JavaScript-tree presence bit. Every indirect name-tree
node may occur exactly once: a cycle or multi-parent duplicate is malformed.
Actions remain cycle-safe. Every enqueue or inspected name-tree item is charged
before the repeat check against the shared audit budget. Malformed trees return
`QUANTAPDF_ERROR_FORMAT`; exhausted traversal returns
`QUANTAPDF_ERROR_UNSUPPORTED`.

Embedded-file detection covers the catalog `/Names /EmbeddedFiles` tree,
associated-file `/AF` references, file specifications with `/EF`, embedded-file
streams, and file-attachment annotations. XFA is detected at
`/Root /AcroForm /XFA`. Rich media is detected by annotation subtype.

The graph walk is iterative. Indirect objects are de-duplicated by
`QPDFObjGen`; streams contribute their dictionaries but their bytes are never
decoded. Work-list growth is checked and bounded by a conservative limit
derived from source size. Exceeding the completeness limit returns
`QUANTAPDF_ERROR_UNSUPPORTED`; allocation failure returns
`QUANTAPDF_ERROR_NOMEM`.

## Sanitize policy

Sanitize flags correspond exactly to the first seven finding classes:

- JavaScript actions and the JavaScript name tree;
- launch actions;
- external actions;
- other active or unknown actions;
- embedded files;
- XFA;
- rich-media annotations.

`QUANTAPDF_SANITIZE_ALL` combines those seven bits. Signature and encryption
are observations, not removal flags. Every sanitize call rejects a document
with either condition because a full rewrite would invalidate a signature or
silently decrypt the file.

The sanitizer opens a fresh private graph, completes audit/preflight before
mutation, and then removes selected references:

- selected actions from `/OpenAction`, `/A`, `/AA`, and `/Next`;
- `/Names /JavaScript` or `/Names /EmbeddedFiles` as selected; when the
  JavaScript policy is unselected, selected non-JavaScript action heads are
  removed as complete name/value pairs and selected `/Next` continuations are
  still pruned from retained safe or unselected name-tree actions;
- `/AF` and `/EF` references plus file-attachment annotations for embedded-file
  removal;
- `/AcroForm /XFA` for XFA removal;
- selected rich-media annotations from page `/Annots` arrays.

After mutation, the same audit engine must prove that every selected finding
bit is absent before bytes are published. Unselected categories remain
unchanged. The writer uses deterministic IDs, disables object streams,
preserves stream data, and discards objects made unreachable by sanitization.
Repeated identical calls and reopen-then-sanitize are byte-idempotent.

Before mutation, a second bounded role/ownership preflight records every
reachable indirect container reference by semantic role, charging before
role-aware de-duplication. Traversal carries the stable nearest indirect owner
as a mutation anchor. An indirect child becomes its own anchor; editing any
direct descendant targets that nearest indirect anchor. A selected target is
rejected with `QUANTAPDF_ERROR_UNSUPPORTED` if its anchor also has an incoming
custom alias or a different conventional role. This covers direct `/Next`,
`/AA`, `/Annots`, JavaScript name-pair arrays and catalog name descendants,
AcroForm/XFA, `/AF`, `/EF`, and action-owner keys as well as indirect mutable
containers. It prevents one in-place edit from silently changing an unselected
interpretation, such as one array serving both `/Annots` and action `/Next`.
Multiple aliases in the same semantic role remain supported, including shared
`/AA` dictionaries and shared `/Next` arrays. Alias rejection happens after
strict audit but before any mutation or output publication.

## Ownership and failure atomicity

The public document owns source bytes and both backend handles. Audit borrows
them only for the duration of the call and publishes no pointers. Sanitize
creates one private QPDF owner, then copies the final writer buffer into the
heap allocation owned by `quantapdf_output`. No borrowed QPDF memory crosses
the C++ bridge.

The C facade allocates an output shell but publishes it only after backend
success. Every failure frees the shell and any returned buffer, leaving
`*out_output == NULL`. All C++ exceptions stay inside the bridge and map to the
existing status enum.

The implementation is single-call/single-threaded. It adds no global mutable
state, background work, queues, callbacks, or shutdown protocol.

## Verification

Tests prove:

- every finding bit in isolation plus a clean zero-result document;
- safe internal navigation is not reported or removed;
- unreferenced dangerous-looking garbage is ignored;
- malformed action/name/annotation structures fail closed;
- JavaScript name-tree heads and all continuation classes, strictly ordered
  byte keys and child ranges, exact optional limits, repeated-node rejection,
  and charge-before-repeat-check budget exhaustion;
- every sanitize flag is isolated and `ALL` clears every sanitizable finding;
- partial sanitize preserves unselected findings;
- selected mutation of cross-role or conventional/custom indirect aliases,
  including mutations through direct descendants anchored at those objects, is
  unsupported with NULL output, while same-role sharing and unrelated policies
  remain supported;
- source immutability, output lifetime, determinism, and idempotence;
- encrypted and signature-bearing inputs audit correctly and sanitize as
  unsupported;
- NULL, short-structure, zero-flag, unknown-flag, and sentinel-output behavior;
- exact v2 exports, installed header version, and full local Windows tests.

Before merge, the exact reviewed head must also pass Linux release/sanitizer,
macOS, and Windows CI. That cross-platform evidence is a pending branch
integration gate, not evidence supplied by the local Task 4 verification.

## Non-goals

- antivirus heuristics, JavaScript parsing, URL reputation, or content-stream
  keyword scanning;
- cryptographic signature validation or preservation;
- password removal, re-encryption, or encrypted output;
- generic metadata removal, form flattening, annotation flattening, redaction,
  image optimization, or repair;
- a public PDF object model, callback visitor, or policy plugin ABI;
- mutation of the opened source document.

## Files

- `include/quantapdf/quantapdf.h`: findings, flags, result, APIs, and 2.3.0.
- `src/pdf_security.c`: argument checks and output ownership facade.
- `src/backend/qpdf_document.h`: private audit/sanitize bridge declarations.
- `src/backend/qpdf_document.cpp`: bounded graph audit and sanitizer.
- `tests/test_pdf_security.c`: public behavior and ownership tests.
- `tests/pdf_security_test_helpers.cpp`: precise structure fixtures and raw
  postcondition checks.
- `tests/CMakeLists.txt`: `quantapdf.pdf_security` target.
- `abi/quantapdf-v2.exports`: two appended public names.
- `README.md`: documented safety boundary and examples.
