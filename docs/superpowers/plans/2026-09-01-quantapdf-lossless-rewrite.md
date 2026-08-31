# QuantaPDF Lossless Rewrite / GC V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic, immutable, strict lossless PDF rewrite that removes unreachable objects and preserves reachable semantics.

**Architecture:** A small C facade publishes an owning `quantapdf_output`; a private QPDF bridge reparses source bytes with recovery disabled, performs security/signature preflight, and writes with one fixed canonical policy. Public and raw-graph tests prove GC, semantic preservation, determinism, idempotence, ownership, and failure atomicity.

**Tech Stack:** C11 public ABI, C++20 QPDF 12.4 bridge, PDFium observation/rendering backend, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-lossless-rewrite-design.md`

## Global Constraints

- Public release version becomes 2.1.0; ABI version and shared-library major remain 2.
- The public ABI adds only `quantapdf_rewrite_lossless`; existing declarations and numeric values do not change.
- The rewrite never performs lossy recompression, appearance generation, flattening, encryption changes, repair, or object deduplication.
- Encrypted and signed documents fail with `QUANTAPDF_ERROR_UNSUPPORTED`.
- Output publication is failure-atomic and output bytes outlive the source document.
- Every production behavior is introduced by a witnessed RED test.

---

### Task 1: Public contract and append-only ABI

**Files:**
- Create: `tests/test_pdf_rewrite_lossless.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_rewrite.c`
- Modify: `CMakeLists.txt`
- Modify: `abi/quantapdf-v2.exports`
- Modify: `tests/test_version.c`

**Interfaces:**
- Consumes: `quantapdf_document`, `quantapdf_output`, `quantapdf_status`.
- Produces: `quantapdf_status quantapdf_rewrite_lossless(quantapdf_document *, quantapdf_output **)`.

- [ ] **Step 1: Write the compile and argument-contract test**

Add a test that calls the wished-for public API and asserts:

```c
quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
CHECK(quantapdf_rewrite_lossless(NULL, &output) == QUANTAPDF_ERROR_ARGUMENT);
CHECK(output == NULL);
CHECK(quantapdf_rewrite_lossless(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
```

Register `quantapdf.pdf_rewrite_lossless` and compile the target against
`QuantaPDF::QuantaPDF`.

- [ ] **Step 2: Run the target and verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target quantapdf_test_pdf_rewrite_lossless
```

Expected: compilation fails because `quantapdf_rewrite_lossless` is undeclared.

- [ ] **Step 3: Add the minimal public shell**

Append the declaration to the transform section of the public header. Add
`src/pdf_rewrite.c` with strict argument handling and NULL output reset. Until
the backend is introduced, a valid document returns
`QUANTAPDF_ERROR_UNSUPPORTED`; invalid calls pass the contract test.

Add the source to `quantapdf`, append the sorted symbol to the v2 export
baseline, change CMake project version and public macros to 2.1.0, and update
the version test to expect minor version 1.

- [ ] **Step 4: Build and run the focused tests**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_pdf_rewrite_lossless quantapdf_test_version
ctest --preset win-release-user -R "quantapdf.(version|abi_exports|pdf_rewrite_lossless)" --output-on-failure
```

Expected: version, exact export, and argument tests pass.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt abi/quantapdf-v2.exports include/quantapdf/quantapdf.h src/pdf_rewrite.c tests/CMakeLists.txt tests/test_pdf_rewrite_lossless.c tests/test_version.c
git commit -m "feat: publish the lossless rewrite contract"
```

### Task 2: Strict canonical writer and garbage collection

**Files:**
- Modify: `tests/test_pdf_rewrite_lossless.c`
- Create: `tests/rewrite_test_helpers.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/pdf_rewrite.c`
- Modify: `src/backend/qpdf_document.h`
- Modify: `src/backend/qpdf_document.cpp`

**Interfaces:**
- Consumes: the Task 1 public function and source bytes owned by `quantapdf_document`.
- Produces: `quantapdf_qpdf_rewrite_lossless(quantapdf_qpdf_document *, unsigned char **, size_t *)` plus test-only raw fixture helpers.

- [ ] **Step 1: Add a real GC fixture and runtime RED**

The C++ helper creates a PDF containing:

```cpp
auto garbage = pdf->makeIndirectObject(
    QPDFObjectHandle::parse("<< /QuantaPDFGarbage true >>"));
auto reachable = pdf->makeIndirectObject(
    QPDFObjectHandle::parse("<< /QuantaPDFReachable true >>"));
pdf->getRoot().replaceKey("/QuantaPDFReachable", reachable);
writer.setPreserveUnreferencedObjects(true);
```

The public test opens that file, calls `quantapdf_rewrite_lossless`, and asserts
success, source immutability, two repeated byte-identical outputs, removal of
the garbage marker, and preservation of the reachable marker.

- [ ] **Step 2: Run and verify runtime RED**

Run the focused CTest. Expected: the valid-document call returns
`QUANTAPDF_ERROR_UNSUPPORTED` from the Task 1 shell.

- [ ] **Step 3: Implement strict parse, preflight, and writer**

The private bridge must:

```cpp
auto pdf = QPDF::create();
pdf->setSuppressWarnings(true);
pdf->setAttemptRecovery(false);
pdf->processMemoryFile("quantapdf-lossless-rewrite", source, size, password);
(void)pdf->getAllPages();
(void)pdf->getAllObjects();
if (pdf->anyWarnings())
    return QUANTAPDF_ERROR_FORMAT;
```

Reject encryption and signed field/catalog-permissions state, then use the
existing fixed writer settings: deterministic ID, disabled object streams,
preserved stream data, and discarded unreferenced objects. The C facade owns
allocation and publishes only successful bytes.

- [ ] **Step 4: Run focused tests GREEN**

Build and run `quantapdf.pdf_rewrite_lossless` and `quantapdf.abi_exports`.
Expected: GC, deterministic repeated calls, ownership, and export checks pass.

- [ ] **Step 5: Commit**

```powershell
git add src/pdf_rewrite.c src/backend/qpdf_document.h src/backend/qpdf_document.cpp tests/CMakeLists.txt tests/test_pdf_rewrite_lossless.c tests/rewrite_test_helpers.cpp
git commit -m "feat: add deterministic lossless PDF rewrite"
```

### Task 3: Idempotence, semantics, and fail-closed matrix

**Files:**
- Modify: `tests/test_pdf_rewrite_lossless.c`
- Modify: `tests/rewrite_test_helpers.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/backend/qpdf_document.cpp`

**Interfaces:**
- Consumes: the canonical writer from Task 2.
- Produces: reopen-and-rewrite idempotence and explicit security/format boundaries.

- [ ] **Step 1: Add idempotence and semantic RED assertions**

Save the first output, close the source, reopen the output, rewrite it, and
assert exact byte equality. Compare representative source/output page count,
bounds, rendered bitmap bytes, text, metadata, outline, links, annotations,
and form observations using literal fixtures and existing public APIs.

- [ ] **Step 2: Add security and strict-format RED assertions**

Create a catalog `/Perms /DocMDP` signature fixture, open the encrypted fixture
with its known password, and open the repairable malformed fixture. Assert:

```c
expect_rewrite_error(encrypted, QUANTAPDF_ERROR_UNSUPPORTED);
expect_rewrite_error(signed_pdf, QUANTAPDF_ERROR_UNSUPPORTED);
expect_rewrite_error(repairable_bad, QUANTAPDF_ERROR_FORMAT);
```

Every failure must leave the sentinel output pointer NULL.

- [ ] **Step 3: Run and classify RED**

Run only `quantapdf.pdf_rewrite_lossless`. Expected failures must identify an
unimplemented preflight branch or a non-idempotent writer policy, never fixture
setup or an unrelated API failure.

- [ ] **Step 4: Complete the minimal preflight/policy**

Recognize effective signed fields and catalog permissions signatures without
rejecting an unsigned empty signature field. Map malformed containers to
`FORMAT`, signed/encrypted state to `UNSUPPORTED`, and adjust only the fixed
writer policy if reopen idempotence proves false.

- [ ] **Step 5: Run focused and full tests GREEN**

Run the focused test, then all CTests. Expected: 29/29 pass.

- [ ] **Step 6: Commit**

```powershell
git add src/backend/qpdf_document.cpp tests/CMakeLists.txt tests/test_pdf_rewrite_lossless.c tests/rewrite_test_helpers.cpp
git commit -m "test: prove lossless rewrite semantics"
```

### Task 4: Documentation and release verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: the completed 2.1.0 API.
- Produces: user-facing transform contract and final verification evidence.

- [ ] **Step 1: Document the supported transform**

Add Lossless Rewrite/GC to the supported-surface list and document that it
preserves stream encodings, discards unreachable objects, is deterministic and
idempotent, and rejects encrypted/signed/recovery-requiring inputs.

- [ ] **Step 2: Run fresh verification**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user
```

Expected: configure/build/install succeed and all 29 CTests pass.

- [ ] **Step 3: Audit the installed ABI**

Run the installed DLL through `cmake/CheckQuantaPDFExports.cmake`. Expected:
exactly 84 named exports, including `quantapdf_rewrite_lossless`, with no
unexpected or ordinal-only exports. Verify installed macros report 2.1.0 and
ABI 2.

- [ ] **Step 4: Commit**

```powershell
git add README.md
git commit -m "docs: describe lossless PDF rewrite"
```

- [ ] **Step 5: Push and open the PR**

Push `feat/lossless-rewrite`, create a PR closing #56, add `full-ci`, and require
Linux release/sanitizer, macOS, and Windows success on the exact final SHA.

