# QuantaPDF PDF Security Rewrite V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add stable-ABI AES-256 PDF encrypt, decrypt, and re-encrypt transforms that preserve document semantics and fail atomically.

**Architecture:** A size-tagged C options record and three direct C facade functions validate public policy and own output publication. A private qpdf adapter strictly reparses the immutable authenticated source, performs a dedicated all-object signature preflight, installs a guarded OS-CSPRNG provider, and configures one QPDFWriter R6 or decryption policy without exposing backend types.

**Tech Stack:** C11 public facade, C++20 qpdf 12.4 backend, PDFium semantic observations, CMake presets, CTest, MSVC DLL/ASan.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-pdf-security-rewrite-design.md`

## Global Constraints

- Output encryption is PDF Standard Security Handler R6/AES-256 only.
- Keep PDFium 154.0.8021.0 + qpdf 12.4.0 only; add no dependency.
- Keep ABI/SOVERSION 2; bump the feature release from 2.5.0 to 2.6.0.
- Add exactly three exports: `quantapdf_encrypt_pdf`, `quantapdf_decrypt_pdf`, and `quantapdf_reencrypt_pdf`.
- Production encryption uses QuantaPDF's private OS-CSPRNG adapter and is intentionally non-deterministic.
- V1 passwords are preparation-invariant printable ASCII, not arbitrary unprepared Unicode.
- Shared builds export only the exact QuantaPDF C ABI and localize dependency symbols.
- Source documents are immutable; every success returns an independent owning `quantapdf_output`.
- Every failure clears output, releases private memory, and preserves the source.
- Signed inputs fail `QUANTAPDF_ERROR_UNSUPPORTED`; wrong operation state fails `QUANTAPDF_ERROR_STATE`.
- Existing transforms never preserve, add, remove, or replace encryption implicitly.
- Use user presets for every configure/build/test/install command and enter `VsDevCmd.bat` on Windows.

---

### Task 1: Publish the append-only ABI with an unsupported implementation

**Files:**
- Create: `tests/test_pdf_security_rewrite.c`
- Create: `src/pdf_security_rewrite.c`
- Modify: `include/quantapdf/quantapdf.h`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `abi/quantapdf-v2.exports`
- Modify: `tests/test_version.c`

**Interfaces:**
- Consumes: existing opaque `quantapdf_document`, `quantapdf_output`, `quantapdf_status`, and output lifetime functions.
- Produces: `quantapdf_encryption_method`, `quantapdf_pdf_permission`, `quantapdf_encryption_options`, V1 size macros, and the three public functions from the design.

- [ ] **Step 1: Write the compile/link RED test**

Add `test_pdf_security_rewrite.c` with compile-time layout checks and unreachable symbol references so both declaration and export linkage are required:

```c
#include <quantapdf/quantapdf.h>
#include <stddef.h>

_Static_assert(QUANTAPDF_ENCRYPTION_AES_256 == 1, "stable method value");
_Static_assert(QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION == (1u << 0),
               "stable permission");
_Static_assert(QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY == (1u << 6),
               "stable permission");
_Static_assert(QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE ==
               sizeof(quantapdf_encryption_options), "complete V1 prefix");

static void compile_public_surface(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    quantapdf_encryption_options options = {0};
    if (0) {
        (void)quantapdf_encrypt_pdf(document, &options, &output);
        (void)quantapdf_decrypt_pdf(document, &output);
        (void)quantapdf_reencrypt_pdf(document, &options, &output);
    }
}
```

Register one new target/test named `quantapdf.pdf_security_rewrite`, linked to
`QuantaPDF::QuantaPDF` and qpdf. The production change that makes this test
pass is the new public ABI plus exported implementations.

- [ ] **Step 2: Run RED and confirm the missing ABI is the failure**

Run:

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_pdf_security_rewrite
```

Expected: compilation fails on undefined security enums/options/functions,
not on fixture setup or unrelated code.

- [ ] **Step 3: Add the public record and minimal failure-atomic facades**

Append the spec's enums/options/macros and declarations to the public header.
Implement each function in `src/pdf_security_rewrite.c` so it clears a valid
output pointer, rejects NULL required arguments, and otherwise returns
`QUANTAPDF_ERROR_UNSUPPORTED` without allocating:

```c
quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_ERROR_UNSUPPORTED;
}
```

Use the same publication discipline for encrypt and reencrypt. Add the source
to `quantapdf`, append the three export names, update project/header/test
version to 2.6.0, and leave `QUANTAPDF_ABI_VERSION` at 2.

- [ ] **Step 4: Run GREEN for compile, version, and exact exports**

Run:

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_pdf_security_rewrite quantapdf_test_version
ctest --preset win-release-user -R "quantapdf.(pdf_security_rewrite|version|abi_exports|abi_checker_rejects_extra_export)" --output-on-failure
```

Expected: four tests pass and the export checker observes exactly 98 unique
ABI 2 names.

- [ ] **Step 5: Commit the ABI slice**

```bash
git add include/quantapdf/quantapdf.h src/pdf_security_rewrite.c \
  CMakeLists.txt tests/CMakeLists.txt tests/test_pdf_security_rewrite.c \
  tests/test_version.c abi/quantapdf-v2.exports
git commit -m "feat: publish PDF security rewrite ABI"
```

### Task 2: Implement strict encrypt, decrypt, and re-encrypt paths

**Files:**
- Create: `tests/pdf_security_rewrite_test_helpers.cpp`
- Create: `src/backend/secure_random.h`
- Create: `src/backend/secure_random.cpp`
- Create: `cmake/QuantaPDFExports.cmake`
- Modify: `tests/test_pdf_security_rewrite.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `src/pdf_security_rewrite.c`
- Modify: `src/backend/qpdf_document.h`
- Modify: `src/backend/qpdf_document.cpp`
- Modify: `src/internal.h`
- Modify: `src/document.c`

**Interfaces:**
- Consumes: Task 1 public API; stored authenticated `source_data`, `source_size`, and single owned password; existing audit and qpdf error mapping.
- Produces: three private bridge functions that return one malloc-owned byte buffer or a cleared failure, an isolated entropy scope, and exact shared-library symbol boundaries.

- [ ] **Step 1: Write RED tests for the three operation state transitions**

Add helper functions that inspect memory with qpdf and return literal
observations rather than mirroring production policy:

```c
typedef struct security_inspection {
    int encrypted;
    int revision;
    int version;
    int stream_aesv3;
    int string_aesv3;
    int file_aesv3;
    int encrypt_metadata;
} security_inspection;

int security_inspect_pdf(const unsigned char *data, size_t size,
                         const char *password,
                         security_inspection *out);
```

Test these observable transitions:

```c
CHECK(quantapdf_encrypt_pdf(plain, &options, &encrypted) == QUANTAPDF_OK);
CHECK(security_inspect_output(encrypted, "user", &inspection));
CHECK(inspection.encrypted && inspection.revision == 6 &&
      inspection.version == 5 && inspection.stream_aesv3 &&
      inspection.string_aesv3 && inspection.file_aesv3);

CHECK(quantapdf_decrypt_pdf(authenticated, &decrypted) == QUANTAPDF_OK);
CHECK(security_inspect_output(decrypted, NULL, &inspection));
CHECK(!inspection.encrypted);

CHECK(quantapdf_reencrypt_pdf(authenticated, &replacement, &reencrypted) ==
      QUANTAPDF_OK);
CHECK(security_inspect_output(reencrypted, "new-user", &inspection));
CHECK(inspection.encrypted && inspection.revision == 6);
```

Also assert encrypt(encrypted) and decrypt/reencrypt(plain) return
`QUANTAPDF_ERROR_STATE` with NULL output.

- [ ] **Step 2: Run RED and confirm the unsupported stub is reached**

Run:

```bat
cmake --build --preset win-release-user --target quantapdf_test_pdf_security_rewrite
ctest --preset win-release-user -R quantapdf.pdf_security_rewrite --output-on-failure
```

Expected: runtime checks fail because valid calls still return
`QUANTAPDF_ERROR_UNSUPPORTED`.

- [ ] **Step 3: Implement one private strict rewrite template**

Declare three private bridge entry points in `qpdf_document.h`. In
`qpdf_document.cpp`, implement an internal operation enum and one function that:

```cpp
auto pdf = QPDF::create();
pdf->setSuppressWarnings(true);
pdf->setAttemptRecovery(false);
pdf->processMemoryFile(description,
    reinterpret_cast<char const*>(document->source_data),
    document->source_size, document->password);
(void)pdf->getAllPages();
(void)pdf->getAllObjects();
if (pdf->anyWarnings()) return QUANTAPDF_ERROR_FORMAT;

uint32_t findings = 0;
status = quantapdf_qpdf_audit_document(
    *pdf, document->source_size, &findings);
if (status != QUANTAPDF_OK) return status;
status = quantapdf_qpdf_security_signature_preflight(*pdf);
if (status != QUANTAPDF_OK) return status;
```

Require encryption absent/present according to the operation. Configure a
memory writer with object streams disabled, stream bytes preserved, and
unreferenced objects discarded. Decrypt sets preserve-encryption false and a
deterministic ID. The signature preflight examines catalog permissions,
inherited signature fields, `/Type /Sig`, `/Type /DocTimeStamp`, and every
current-xref object, returning FORMAT for malformed signature structures and
UNSUPPORTED for valid ones. Encrypt/reencrypt validate or create ID1, replace
`/Info` temporarily with a dictionary holding one NUL-free ASCII-hex CSPRNG
seed while qpdf eagerly computes ID2, restore the exact original Info handle,
and never set deterministic/static IDs. Install the private provider under an
RAII mutex guard before configuration and retain the guard through
`QPDFWriter::write()` so stream IV requests remain isolated; restore the prior
provider only after writing or during exception unwinding. Copy `Buffer` into
malloc storage only after a successful write. Catch `QPDFExc`,
`std::bad_alloc`, `std::exception`, and unknown exceptions exactly like existing
backend functions.

Implement `secure_random` with BCrypt, SecRandom, and getrandom/urandom
fail-closed branches. Add exact ELF/Mach-O allowlists and archive localization
for shared builds; extend the public macro with default visibility. Replace the
backend's retained password `std::string` with a borrow of the C document's one
owned allocation, record its length, and wipe exactly that length on close.

- [ ] **Step 4: Replace stubs with owning C facade publication**

Factor a private C helper that allocates a zeroed output shell, calls the
selected bridge, frees buffer/shell on failure, and publishes on success:

```c
output = (quantapdf_output *)calloc(1, sizeof(*output));
if (output == NULL)
    return QUANTAPDF_ERROR_NOMEM;
status = bridge(document->qpdf_document, options,
                &output->data, &output->size);
if (status != QUANTAPDF_OK) {
    free(output->data);
    free(output);
    return status;
}
*out_output = output;
return QUANTAPDF_OK;
```

- [ ] **Step 5: Run GREEN plus security regressions**

Run:

```bat
cmake --build --preset win-release-user --target quantapdf_test_pdf_security_rewrite quantapdf_test_pdf_security quantapdf_test_pdf_rewrite_lossless
ctest --preset win-release-user -R "quantapdf.(pdf_security_rewrite|pdf_security|pdf_rewrite_lossless)" --output-on-failure
```

Expected: all three focused tests pass.

- [ ] **Step 6: Commit the core backend**

```bash
git add CMakeLists.txt cmake/QuantaPDFExports.cmake src/document.c \
  src/internal.h src/pdf_security_rewrite.c src/backend/secure_random.h \
  src/backend/secure_random.cpp src/backend/qpdf_document.h \
  src/backend/qpdf_document.cpp tests/test_pdf_security_rewrite.c \
  tests/pdf_security_rewrite_test_helpers.cpp tests/CMakeLists.txt
git commit -m "feat: rewrite PDF security with qpdf R6"
```

### Task 3: Lock password, permissions, metadata, and authentication policy

**Files:**
- Modify: `src/pdf_security_rewrite.c`
- Modify: `tests/test_pdf_security_rewrite.c`
- Modify: `tests/pdf_security_rewrite_test_helpers.cpp`

**Interfaces:**
- Consumes: Task 2 working operations and qpdf inspection helper.
- Produces: complete V1 input validation and exact semantic permission mapping.

- [ ] **Step 1: Write table-driven validation RED tests**

Cover NULL options/passwords, `struct_size` one byte short, extended structure
suffix preservation, unknown method, unknown permission, metadata `-1`/`2`,
high-quality print without low-resolution print, empty owner, equal passwords,
ASCII controls, DEL, non-ASCII UTF-8, 127-byte success, and 128-byte rejection.
Every failure starts with a sentinel output and asserts it becomes NULL.

Use literal malformed sequences:

```c
static const char ascii_control[] = {'a', '\n', 'b', 0};
static const char ascii_del[] = {'a', 0x7f, 0};
static const char non_ascii_utf8[] = "m\xC3\xB6t-de-passe";
```

The production mutations these tests catch are silent truncation, byte-mode
fallback, acceptance of insecure owner credentials, and ignored policy bits.

- [ ] **Step 2: Run RED and confirm invalid policies reach the backend**

Run the dedicated CTest and confirm at least one invalid case returns success,
state, or backend error instead of `QUANTAPDF_ERROR_ARGUMENT`.

- [ ] **Step 3: Implement bounded C11 password validation**

Scan at most 128 bytes for the terminator and reject a missing terminator.
Accept only preparation-invariant printable ASCII bytes `0x20..0x7e`; reject
controls, DEL, and all non-ASCII UTF-8 before entering qpdf. Validate both
passwords before allocation. Require nonempty/distinct owner and permit empty
user.

- [ ] **Step 4: Write permission/metadata RED observations**

For each public permission bit, encrypt from a fresh plain document and inspect
qpdf's effective `allow*` getters and `/P`-derived behavior. Test no optional
permission, all permissions, low print, full print, metadata effective true,
and explicit false. Test the exact truth table for `MODIFY_OTHER`,
`ANNOTATE_AND_FILL_FORMS`, and `FILL_FORMS`. Accessibility is always effective
and is not a public flag.

- [ ] **Step 5: Map policy to qpdf and run GREEN**

Map print flags to `qpdf_r3p_none`, `qpdf_r3p_low`, or `qpdf_r3p_full`; pass
true for accessibility and the five exposed permission booleans plus normalized
metadata boolean to `setR6EncryptionParameters`. Run the dedicated test until the full table is
green, then run audit/sanitize regression.

- [ ] **Step 6: Commit policy validation**

```bash
git add src/pdf_security_rewrite.c tests/test_pdf_security_rewrite.c \
  tests/pdf_security_rewrite_test_helpers.cpp
git commit -m "feat: validate PDF encryption policy"
```

### Task 4: Prove security material, lifetime, failures, and semantic preservation

**Files:**
- Create: `tests/test_pdf_security_rewrite_semantics.c`
- Create: `tests/pdf_security_rewrite_fault_hook.c`
- Create: `tests/pdf_security_rewrite_test_api.h`
- Modify: `tests/test_pdf_security_rewrite.c`
- Modify: `tests/pdf_security_rewrite_test_helpers.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/internal.h`
- Modify: `src/pdf_security_rewrite.c`

**Interfaces:**
- Consumes: completed public operations and existing public observation APIs.
- Produces: adversarial/security/lifetime proof with test-only facade faults.

- [ ] **Step 1: Write authentication and randomness RED/GREEN tests**

Save encrypted output and assert public open rejects a wrong password, accepts
the correct user password, and accepts the owner password. Encrypt the same
source/policy twice and assert nonzero, valid outputs with different bytes and
different R6 security material. Reencrypt and assert old credentials fail,
new user/owner credentials succeed, permissions change, and security material
is fresh. Under `QUANTAPDF_TESTING`, use private non-exported counters and
one-shot faults to prove provider entry, random requests during both configure
and write phases, restoration on every exit, and explicit entropy failure. A
separate static-link helper may verify sentinel-provider override/restore, but
is not shared-copy isolation evidence. Cover absent, valid, malformed, and repeated same-second `/ID`
cases, including fresh ID2 and a CSPRNG-created ID1 when missing. Assert the
encrypted header/catalog declares PDF 1.7 extension level 8.

- [ ] **Step 2: Write deterministic decrypt and source-lifetime tests**

Decrypt one authenticated encrypted document twice, compare exact output
bytes, close the source before reading/saving the first output, and reopen the
saved plaintext with no password. Audit source before and after every transform
and assert its encryption finding is unchanged.

- [ ] **Step 3: Write signed, legacy, malformed, and failure-atomic tests**

Assert all three operations reject the existing signed fixture plus generated
reachable/orphan `/Sig`, DocMDP, document timestamp, malformed signature-field,
and signed incremental fixtures. Decrypt and reencrypt the existing legacy
encrypted fixture after successful open. Use the helper to create malformed
encryption dictionaries and assert open/transform fails as password, format,
or unsupported without output. Add a test-only
document fault enum with two one-shot stages:

```c
typedef enum quantapdf_test_security_fault {
    QUANTAPDF_TEST_SECURITY_FAULT_NONE = 0,
    QUANTAPDF_TEST_SECURITY_FAULT_OUTPUT_NOMEM = 1,
    QUANTAPDF_TEST_SECURITY_FAULT_BEFORE_PUBLICATION = 2
} quantapdf_test_security_fault;
```

The hook source includes `src/internal.h` and is linked only into the dedicated
test. Production facades consume the field only under `QUANTAPDF_TESTING`.
Both faults must leave sentinel output NULL; the next call must succeed.

- [ ] **Step 4: Adapt the existing public semantic comparator**

Build `test_pdf_security_rewrite_semantics.c` from the established
image-recompression semantic comparison pattern. For each text, links, image,
AcroForm, outline, metadata, and annotation fixture:

```text
open plain -> encrypt -> save -> open(user) -> decrypt -> save -> open plain
           -> compare source and final through public APIs
```

Compare page count/order, MediaBox/CropBox, extracted text/search, rendered
pixels, image observations/pixels, links/destinations, annotations/contents,
form fields/options/values/widgets, outline topology/titles/URIs, and metadata.
Require at least one observed image, link, annotation, form field, outline
node, and nonempty text result so empty fixtures cannot make the test vacuous.
For metadata true and false, perform authenticated semantic comparison and an
unauthenticated raw-stream scan that finds the plaintext marker only when
metadata encryption is effectively false.

- [ ] **Step 5: Run focused GREEN and full Release regression**

Run:

```bat
cmake --build --preset win-release-user --target quantapdf_test_pdf_security_rewrite
ctest --preset win-release-user -R quantapdf.pdf_security_rewrite --output-on-failure
ctest --preset win-release-user --output-on-failure
```

Expected: the dedicated test passes and the suite grows from 35 to 36 tests
with zero failures.

- [ ] **Step 6: Commit the hardening matrix**

```bash
git add src/internal.h src/pdf_security_rewrite.c \
  tests/test_pdf_security_rewrite.c \
  tests/test_pdf_security_rewrite_semantics.c \
  tests/pdf_security_rewrite_test_helpers.cpp \
  tests/pdf_security_rewrite_fault_hook.c \
  tests/pdf_security_rewrite_test_api.h tests/CMakeLists.txt
git commit -m "test: harden PDF security rewrite boundary"
```

### Task 5: Document the stable security kernel surface

**Files:**
- Modify: `README.md`
- Create: `docs/releases/v2.6.0.md`
- Modify: `docs/releases/v2.3.0.md`
- Modify: `docs/superpowers/specs/2026-09-01-quantapdf-pdf-security-rewrite-design.md`
- Modify: `docs/superpowers/plans/2026-09-01-quantapdf-pdf-security-rewrite.md`

**Interfaces:**
- Consumes: verified final ABI and behavior.
- Produces: public usage, safety limits, dependency/license statement, release record, and checked plan.

- [ ] **Step 1: Add a complete README example and contract**

Show options initialized with `QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE`, an empty
or explicit user password, a nonempty distinct owner password, exact
print/copy/form flags, metadata choice, output save/drop, and the explicit
decrypt-transform-encrypt workflow. State that permissions are advisory, R6
V1 passwords are printable ASCII up to 127 bytes, encryption is randomized, and
signed inputs are unsupported.

- [ ] **Step 2: Create the 2.6.0 release record**

Record three transforms, AES-256-only output, private OS CSPRNG/provider guard,
98 exports, ABI/SOVERSION 2, 36 CTests, dependency/license and symbol-isolation
boundaries, intentional non-determinism, semantic preservation, and the
verification commands/results.
Remove #58 from the 2.3.0 list of currently open limitations by marking it
delivered in 2.6.0 rather than rewriting the historical 2.3.0 contents.

- [ ] **Step 3: Reconcile spec/plan with implementation facts**

Update any file list, exact error outcome, qpdf behavior, or verification count
that changed during RED/GREEN work. Mark completed plan checkboxes only when
the corresponding command evidence exists. Run:

```bash
rg -n "ExtractPDF|EXTRACTPDF|MuPDF|AGPL|TBD|TODO" README.md \
  docs/releases/v2.6.0.md \
  docs/superpowers/specs/2026-09-01-quantapdf-pdf-security-rewrite-design.md \
  docs/superpowers/plans/2026-09-01-quantapdf-pdf-security-rewrite.md
git diff --check
```

Expected: only intentional no-MuPDF/no-AGPL statements match; no stale API or
placeholder appears and diff whitespace is clean.

- [ ] **Step 4: Commit documentation**

```bash
git add README.md docs/releases/v2.3.0.md docs/releases/v2.6.0.md \
  docs/superpowers/specs/2026-09-01-quantapdf-pdf-security-rewrite-design.md \
  docs/superpowers/plans/2026-09-01-quantapdf-pdf-security-rewrite.md
git commit -m "docs: publish PDF security rewrite contract"
```

### Task 6: Verify release, sanitizer, install, ABI, and review gates

**Files:**
- Modify only if verification or review exposes a defect; every fix starts with a reproducing test.

**Interfaces:**
- Consumes: Tasks 1-5 completed feature branch.
- Produces: fresh completion evidence and reviewed integration candidate.

- [ ] **Step 1: Run clean Windows Release verification**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user -j 2
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user
```

Expected: configure/build/install succeed and 36/36 CTests pass.

- [ ] **Step 2: Run clean MSVC ASan verification**

First verify `where clang_rt.asan_dynamic-x86_64.dll` succeeds inside the same
VS environment, then run:

```bat
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user -j 2
ctest --preset win-dev-user --output-on-failure
```

Expected: 36/36 tests pass under ASan with no sanitizer diagnostic.

- [ ] **Step 3: Verify installed C ABI independently**

Compile a standalone C11 consumer against the installed header/library. It
must initialize the V1 record, reference all three functions, print version
2.6.0/ABI 2, link without repository-private headers, and run successfully.
Use `dumpbin /exports` plus the repository checker to prove exactly the 98
expected unique names and no test hooks. On a shared ELF/Mach-O verification
host, inspect dynamic symbols and prove no qpdf/PDFium/dependency symbol is
exported.

- [ ] **Step 4: Request an independent exact-range code/security review**

Obtain base and head SHAs and dispatch the `requesting-code-review` template
with this spec/plan. Resolve every Critical and Important finding with a RED
test, minimal fix, focused GREEN, and renewed full verification. Record any
accepted Minor item in the release note.

- [ ] **Step 5: Verify final tree and commit review fixes**

Run `git diff --check`, `git status --short`, the focused security CTest, and
the full Release suite again at the exact final head. Commit only reviewed
source/test/doc changes; keep ignored build products out of Git.

- [ ] **Step 6: Finish the development branch**

Use `superpowers:finishing-a-development-branch`. Re-run the full suite,
confirm base branch `master`, and present exactly the three integration options
without pushing or merging before the user chooses.
