# QuantaPDF Document Audit and Sanitize V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stable structure-level document audit and an immutable policy-driven sanitizer.

**Architecture:** A size-tagged C audit result and flag-based sanitize facade delegate to one private QPDF security engine. The engine strictly reparses source bytes, walks only the reachable object graph with bounded storage, classifies executable and embedded structures, mutates a fresh graph for selected policies, verifies postconditions, and publishes deterministic owning bytes.

**Tech Stack:** C11 public ABI, C++20 QPDF 12.4 bridge, PDFium observation backend, CMake Presets, CTest.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-audit-sanitize-design.md`

## Global Constraints

- Public release version becomes 2.3.0; ABI version and shared-library major remain 2.
- Existing declarations, enum values, ownership rules, and 85 exports do not change; exactly two exports are added.
- Audit is structural, reachable-only, read-only, and fail-closed on malformed executable containers.
- Sanitize is immutable, explicit-policy, failure-atomic, deterministic, and refuses signed or encrypted rewrites.
- The graph work list is iterative, overflow-checked, bounded, and owns no PDF bytes.
- Every production behavior is introduced by a witnessed RED test.

---

### Task 1: Publish the append-only API contract

**Files:**
- Create: `tests/test_pdf_security.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_security.c`
- Modify: `CMakeLists.txt`
- Modify: `abi/quantapdf-v2.exports`
- Modify: `tests/test_version.c`

**Interfaces:**
- Produces: `quantapdf_audit_result`, `quantapdf_audit_finding`, `quantapdf_sanitize_flag`, `quantapdf_document_audit`, and `quantapdf_sanitize`.
- Consumes: existing opaque document/output handles and status values.

Use these exact public names and values:

```c
typedef enum quantapdf_audit_finding {
    QUANTAPDF_AUDIT_JAVASCRIPT_ACTION = 1u << 0,
    QUANTAPDF_AUDIT_LAUNCH_ACTION = 1u << 1,
    QUANTAPDF_AUDIT_EXTERNAL_ACTION = 1u << 2,
    QUANTAPDF_AUDIT_OTHER_ACTION = 1u << 3,
    QUANTAPDF_AUDIT_EMBEDDED_FILE = 1u << 4,
    QUANTAPDF_AUDIT_XFA = 1u << 5,
    QUANTAPDF_AUDIT_RICH_MEDIA = 1u << 6,
    QUANTAPDF_AUDIT_SIGNATURE = 1u << 7,
    QUANTAPDF_AUDIT_ENCRYPTION = 1u << 8
} quantapdf_audit_finding;

typedef enum quantapdf_sanitize_flag {
    QUANTAPDF_SANITIZE_JAVASCRIPT_ACTIONS = 1u << 0,
    QUANTAPDF_SANITIZE_LAUNCH_ACTIONS = 1u << 1,
    QUANTAPDF_SANITIZE_EXTERNAL_ACTIONS = 1u << 2,
    QUANTAPDF_SANITIZE_OTHER_ACTIONS = 1u << 3,
    QUANTAPDF_SANITIZE_EMBEDDED_FILES = 1u << 4,
    QUANTAPDF_SANITIZE_XFA = 1u << 5,
    QUANTAPDF_SANITIZE_RICH_MEDIA = 1u << 6,
    QUANTAPDF_SANITIZE_ALL = (1u << 7) - 1u
} quantapdf_sanitize_flag;
```

The exact result is `size_t struct_size` followed by `uint32_t findings`, with
`QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE` ending at `findings` and
`QUANTAPDF_AUDIT_RESULT_V1_SIZE` equal to `sizeof(quantapdf_audit_result)`.

- [x] **Step 1: Write compile and argument RED tests**

Add calls to the wished-for APIs. Assert NULL arguments, a short audit result,
zero/unknown sanitize flags, and sentinel reset:

```c
quantapdf_audit_result audit = {0};
quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
CHECK(quantapdf_document_audit(NULL, &audit) == QUANTAPDF_ERROR_ARGUMENT);
CHECK(quantapdf_sanitize(NULL, QUANTAPDF_SANITIZE_ALL, &output) ==
      QUANTAPDF_ERROR_ARGUMENT);
CHECK(output == NULL);
```

- [x] **Step 2: Build the focused target and witness RED**

Run the public test target. Expected: compilation fails because the new public
types and functions do not exist.

- [x] **Step 3: Add the minimal public shell**

Publish the size-tagged result, finding/flag enums, constants, and function
declarations. Add a C facade that clears addressable outputs, validates all
arguments, and returns `QUANTAPDF_ERROR_UNSUPPORTED` for valid documents until
the bridge exists. Add the source, exact exports, and bump 2.2.0 to 2.3.0.

- [x] **Step 4: Reconfigure and run contract/version/ABI GREEN**

Run the security, version, and ABI tests. The valid-document placeholder may
still be unsupported; all argument contracts and exact 87-export checks pass.

- [x] **Step 5: Commit**

Commit as `feat: publish document security API`.

### Task 2: Implement reachable document audit

**Files:**
- Modify: `tests/test_pdf_security.c`
- Create: `tests/pdf_security_test_helpers.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/pdf_security.c`
- Modify: `src/backend/qpdf_document.h`
- Modify: `src/backend/qpdf_document.cpp`

**Interfaces:**
- Produces: `quantapdf_qpdf_document_audit(quantapdf_qpdf_document *, uint32_t *)`.
- Test helper produces isolated valid and malformed PDF structures.

The classifier uses these exact action classes:

| `/S` name | Finding |
|---|---|
| `/GoTo` | none (safe internal navigation) |
| `/JavaScript` | `QUANTAPDF_AUDIT_JAVASCRIPT_ACTION` |
| `/Launch` | `QUANTAPDF_AUDIT_LAUNCH_ACTION` |
| `/URI`, `/GoToR`, `/GoToE`, `/SubmitForm`, `/ImportData` | `QUANTAPDF_AUDIT_EXTERNAL_ACTION` |
| every other name | `QUANTAPDF_AUDIT_OTHER_ACTION` |

An object reached through `/OpenAction`, `/A`, an `/AA` entry, or action
`/Next` must be a dictionary with a name `/S`; action `/Next` may instead be
an array of such dictionaries. Catalog `/OpenAction` also permits a
destination array, name, or string and those forms are not actions. Scan
`/Next` even for safe `/GoTo` actions.

The ordinary graph walk flags any `/AF` or `/EF` reference, embedded-file
stream, or `/FileAttachment` annotation as embedded content. It flags
`/RichMedia`, `/3D`, `/Movie`, `/Sound`, and `/Screen` annotation subtypes as
rich media. Catalog `/Names` must be a dictionary when present; the presence
of `/JavaScript` or `/EmbeddedFiles` beneath it sets the corresponding bit.
Catalog `/AcroForm` must be a dictionary when present; `/XFA` sets its bit and
the established strict effective-signature traversal supplies the signature
bit. A present page/object `/Annots` entry must be an array of dictionaries.

The shared traversal budget is `max(4096, source_size * 64)` processed or
queued handles. Check the multiplication and every push; an exceeded budget
returns `QUANTAPDF_ERROR_UNSUPPORTED`. De-duplicate indirect graph objects and
indirect actions by `QPDFObjGen`. Direct input containers are acyclic by PDF
syntax. Streams contribute their dictionaries only.

- [x] **Step 1: Add clean, isolated-bit, reachability, and malformed RED tests**

Generate one fixture per finding class. Verify clean and internal-GoTo inputs
return zero, all nine public bits are isolated, dangerous unreferenced garbage
is ignored, and malformed `/A`, `/AA`, name dictionaries, or annotation arrays
return `QUANTAPDF_ERROR_FORMAT` with findings cleared.

Also pass a result struct larger than V1 with a canary suffix: V1 fields change
and the suffix remains byte-identical. Repeat audit on one document to prove
that observation is non-mutating.

- [x] **Step 2: Run the focused test and witness runtime RED**

Expected: valid audits return the shell's unsupported status.

- [x] **Step 3: Implement the bounded read-only engine**

Strictly reparse source bytes, traverse the catalog-reachable graph
iteratively, de-duplicate indirect objects by object-generation identity,
classify conventional action positions and security structures, and map all
exceptions. Enforce the work-item limit before each push.

- [x] **Step 4: Run focused audit tests GREEN**

Build and run only `quantapdf.pdf_security`; confirm every finding and
fail-closed case passes.

- [x] **Step 5: Commit**

Commit as `feat: audit reachable PDF security structures`.

### Task 3: Implement immutable policy sanitization

**Files:**
- Modify: `tests/test_pdf_security.c`
- Modify: `tests/pdf_security_test_helpers.cpp`
- Modify: `src/pdf_security.c`
- Modify: `src/backend/qpdf_document.h`
- Modify: `src/backend/qpdf_document.cpp`

**Interfaces:**
- Produces: `quantapdf_qpdf_sanitize(quantapdf_qpdf_document *, uint32_t, unsigned char **, size_t *)`.
- Consumes: the Task 2 classifier and the established deterministic writer.

Sanitize bits 0-6 map directly to audit bits 0-6. Complete the same strict
audit before mutation. If `QUANTAPDF_AUDIT_SIGNATURE` or
`QUANTAPDF_AUDIT_ENCRYPTION` is present, return
`QUANTAPDF_ERROR_UNSUPPORTED` for every nonzero sanitize policy.

Apply these exact mutations on the fresh private graph:

- `/OpenAction` and `/A`: remove the owner key when the referenced action's
  class is selected; preserve destinations and unselected action heads.
- `/AA`: remove only selected event entries and remove the owner `/AA` key if
  the event dictionary becomes empty.
- action `/Next`: remove a selected single continuation; for arrays, retain
  unselected actions in order and remove `/Next` if none survive. Recursively
  sanitize continuation chains even when their head is safe or unselected.
- catalog `/Names`: remove `/JavaScript` and/or `/EmbeddedFiles` only when its
  matching policy is selected; remove catalog `/Names` if the dictionary then
  has no keys.
- embedded-file policy: remove every dictionary `/AF` and `/EF` key and filter
  `/FileAttachment` annotations from every validated `/Annots` array.
- XFA policy: remove only catalog `/AcroForm /XFA`.
- rich-media policy: filter `/RichMedia`, `/3D`, `/Movie`, `/Sound`, and
  `/Screen` annotations from every validated `/Annots` array.

Preserve array order and all unselected keys/objects. If conventional removal
leaves a selected finding reachable (for example a custom edge directly to an
embedded-file stream), return `QUANTAPDF_ERROR_UNSUPPORTED` without output;
do not relabel or retain payload bytes merely to make the bit disappear.

- [x] **Step 1: Add policy isolation and `ALL` RED tests**

For a combined fixture, sanitize one flag at a time, reopen output, audit it,
and assert only the selected bit disappears. Assert `ALL` clears all seven
sanitizable bits and preserves page/text/render observations.

The combined fixture must place the four action classes in independent
conventional owners so removing one does not make another unreachable. Include
a safe internal `GoTo` action and assert it survives both partial and `ALL`
sanitization. Add a custom direct reference to an embedded-file stream and
assert embedded-file sanitization returns unsupported rather than publishing a
false-clean output.

- [x] **Step 2: Add ownership, security, and canonical RED tests**

Assert source audit remains unchanged, output outlives the source, identical
calls are byte-equal, reopen-then-sanitize is byte-equal, and signed/encrypted
inputs return unsupported with NULL output.

Use the C++ helper to inspect all written indirect objects and prove selected
actions, name-tree entries, file specifications/embedded streams, XFA, and
rich-media annotation objects made unreachable by conventional removal are
absent from the serialized output, not merely hidden from the public scanner.

- [x] **Step 3: Run and witness runtime RED**

Expected: valid sanitize calls still return unsupported from the shell.

- [x] **Step 4: Implement preflight, mutation, post-audit, and publication**

Open a fresh strict graph, audit before mutation, reject signature/encryption,
remove only selected conventional references, re-audit selected postconditions,
and write with deterministic ID, disabled object streams, preserved stream
data, and unreachable-object collection. Publish bytes only on success.

- [x] **Step 5: Run focused sanitization tests GREEN**

Run `quantapdf.pdf_security` plus rewrite/flatten regression tests.

- [x] **Step 6: Commit**

Commit as `feat: sanitize active PDF content`.

### Task 4: Documentation and complete verification

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Add: `docs/superpowers/specs/2026-09-01-quantapdf-audit-sanitize-design.md`
- Modify: `docs/superpowers/plans/2026-09-01-quantapdf-audit-sanitize.md`

**Interfaces:**
- Produces: supported-surface documentation and final release evidence.

- [x] **Step 1: Document examples and exact boundaries**

Show audit-result initialization, finding checks, `SANITIZE_ALL`, output
ownership, signature/encryption rejection, and the structural-not-antivirus
boundary.

- [x] **Step 2: Run fresh Windows verification and install**

Run configure, full build, all CTests, and the release install preset through
`VsDevCmd.bat` with the pinned vcpkg root restored afterward.

- [x] **Step 3: Verify installed ABI and header**

Confirm exactly 87 named exports, version 2.3.0/ABI 2, and compile a consumer
against the installed package.

- [x] **Step 4: Run static review checks**

Inspect the diff, search for implementation placeholders and forbidden legacy
backend/license references, and confirm no generated build/vcpkg files are
tracked.

- [ ] **Step 5: Complete branch workflow**

Use the finishing-development-branch process, push the reviewed branch, open a
PR, and require Linux release/sanitizer, macOS, and Windows success on the exact
head before merge.
