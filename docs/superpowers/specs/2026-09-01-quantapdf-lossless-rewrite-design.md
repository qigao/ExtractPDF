# QuantaPDF Lossless Rewrite / GC V1 Design

**Status:** Approved for implementation by the continuing development request

**Issue:** #56

## Goal

Add one immutable whole-document transform that emits a canonical full PDF,
removes objects unreachable from the trailer graph, and preserves all reachable
document semantics without lossy recompression or interactive-content changes.

```c
QUANTAPDF_API quantapdf_status quantapdf_rewrite_lossless(
    quantapdf_document *document,
    quantapdf_output **out_output);
```

This is a rewrite and garbage-collection primitive, not a generic optimizer.

## Public contract

- `document == NULL` or `out_output == NULL` returns
  `QUANTAPDF_ERROR_ARGUMENT`.
- Whenever `out_output` is non-NULL, `*out_output` is set to NULL before any
  validation or backend work.
- Success returns a new owning `quantapdf_output`. The output remains valid
  after the source document is closed.
- The source document and all of its public observations remain unchanged.
- Encrypted input returns `QUANTAPDF_ERROR_UNSUPPORTED`, even when the caller
  supplied a valid password to `quantapdf_open()`.
- A document with an active signature field or catalog permissions signature
  returns `QUANTAPDF_ERROR_UNSUPPORTED` because a full rewrite invalidates the
  signature.
- Structurally damaged input that requires backend recovery returns
  `QUANTAPDF_ERROR_FORMAT`; this transform does not silently repair it.

Adding this function is an append-only ABI v2 change. The release becomes
2.1.0, `QUANTAPDF_ABI_VERSION` remains 2, and the shared-library major remains
2. The v2 export baseline grows from 83 to 84 functions.

## Rewrite policy

The backend opens a fresh private QPDF graph from the source bytes and password.
It disables parser recovery and suppresses warning output while retaining the
ability to detect warnings. It forces the page and object graphs to load before
writing. Any parse warning or exception is a format failure.

The writer policy is fixed:

- deterministic document ID;
- conventional objects with object streams disabled;
- source stream bytes and filter state preserved;
- unreferenced objects discarded;
- no content normalization;
- no linearization;
- no object deduplication;
- no encryption preservation or creation.

Disabling object streams is part of the canonical V1 representation. Preserving
stream data avoids hidden image loss, filter changes, appearance regeneration,
or platform-dependent recompression.

## Reachability and garbage collection

QPDF's writer traversal starts at the trailer and discards every indirect object
not visited through that graph. Reachable sharing is preserved by indirect
object identity. Distinct reachable objects are never merged merely because
their serialized bytes match.

V1 rejects a file that needs structural recovery even when the damaged object
would otherwise be garbage. This intentionally favors a strict, reproducible
kernel boundary over forensic repair behavior.

## Signature preflight

The preflight rejects:

- an encrypted QPDF graph;
- a reachable AcroForm field whose effective field type is `/Sig` and whose
  value is non-null;
- catalog `/Perms` entries `/DocMDP`, `/UR`, or `/UR3` when present.

Malformed `/AcroForm`, `/Fields`, `/Kids`, or `/Perms` structures encountered
by this preflight are format failures. Unsigned empty signature fields remain
valid and are preserved.

## Determinism and idempotence

For identical source bytes, repeated calls must return byte-identical outputs.
Reopening the first output and rewriting it again must also return the same
bytes. This is enforced by the fixed writer policy and deterministic ID.

If the pinned writer cannot satisfy reopen-and-rewrite idempotence, the feature
must not ship with a weaker undocumented promise; implementation stops until
the policy is corrected.

## Semantic preservation

Tests observe both raw graph behavior and public QuantaPDF behavior:

- unreachable ordinary and stream objects disappear;
- a reachable shared object remains reachable and shared;
- page count, bounds, rendered pixels, extracted text, metadata, outline,
  annotations, links, and AcroForm values remain unchanged on representative
  fixtures;
- the output reopens successfully after the source document is closed;
- repeated and reopen-and-rewrite bytes are identical.

## Failure atomicity

The C facade allocates the output handle before calling the backend, but it does
not publish it until the backend returns success. Every failure frees the shell
and any backend buffer, leaving `*out_output == NULL`.

All QPDF exceptions remain inside the C++ bridge and map to the established
`quantapdf_status` values.

## Non-goals

- lossy image recompression or downsampling;
- stream recompression or filter normalization;
- annotation or Widget flattening;
- page/content editing;
- object deduplication;
- linearization or incremental-save preservation;
- encryption, decryption, or re-encryption;
- signature preservation or removal;
- generic damaged-file repair;
- a public low-level PDF object API or writer options structure.

## Files

- `include/quantapdf/quantapdf.h`: append the public function and publish 2.1.0.
- `src/pdf_rewrite.c`: owning C facade and failure-atomic output publication.
- `src/backend/qpdf_document.h`: private rewrite bridge declaration.
- `src/backend/qpdf_document.cpp`: strict preflight and fixed writer policy.
- `tests/test_pdf_rewrite_lossless.c`: public contract, semantics, lifetime,
  determinism, idempotence, and fail-closed tests.
- `tests/rewrite_test_helpers.cpp`: fixture construction and raw GC assertions.
- `tests/CMakeLists.txt`: one new `quantapdf.pdf_rewrite_lossless` test target.
- `abi/quantapdf-v2.exports`: append the 84th v2 export.
- `README.md`: document the transform and its strict boundaries.

