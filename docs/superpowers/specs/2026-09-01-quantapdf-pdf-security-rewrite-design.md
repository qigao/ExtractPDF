# QuantaPDF PDF Security Rewrite V1 Design

**Status:** Revised after committed-spec security review; pending re-review

## Goal

Add three explicit immutable security transforms to the stable C ABI:

```c
QUANTAPDF_API quantapdf_status quantapdf_encrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output);

QUANTAPDF_API quantapdf_status quantapdf_reencrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output);
```

`encrypt` accepts only an unencrypted source, `decrypt` accepts only an
encrypted source that `quantapdf_open()` already authenticated, and
`reencrypt` accepts only an authenticated encrypted source. Each call reparses
the immutable source into a private qpdf graph and publishes a new owning
`quantapdf_output`. No existing save, edit, flatten, sanitize, recompress,
rewrite, or Composer operation changes document security implicitly.

V1 writes only the PDF Standard Security Handler revision 6 with AES-256. It
uses the existing PDFium 154.0.8021.0 + qpdf 12.4.0 backend and adds no MuPDF,
AGPL, TurboNet, TurboUtils, OpenSSL, Monocypher, or other dependency.

## Public ABI

```c
typedef enum quantapdf_encryption_method {
    QUANTAPDF_ENCRYPTION_AES_256 = 1
} quantapdf_encryption_method;

typedef enum quantapdf_pdf_permission {
    QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION = 1u << 0,
    QUANTAPDF_PERMISSION_MODIFY_OTHER = 1u << 1,
    QUANTAPDF_PERMISSION_COPY = 1u << 2,
    QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS = 1u << 3,
    QUANTAPDF_PERMISSION_FILL_FORMS = 1u << 4,
    QUANTAPDF_PERMISSION_ASSEMBLE = 1u << 5,
    QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY = 1u << 6,
    QUANTAPDF_PERMISSION_ALL = (1u << 7) - 1u
} quantapdf_pdf_permission;

typedef struct quantapdf_encryption_options {
    size_t struct_size;
    quantapdf_encryption_method method;
    const char *user_password_utf8;
    const char *owner_password_utf8;
    uint32_t permissions;
    int encrypt_metadata;
} quantapdf_encryption_options;

#define QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE \
    (offsetof(quantapdf_encryption_options, encrypt_metadata) + sizeof(int))
#define QUANTAPDF_ENCRYPTION_OPTIONS_V1_SIZE \
    (sizeof(quantapdf_encryption_options))
```

The options record is size-tagged. V1 requires the complete V1 prefix, accepts
a larger record, and ignores its unknown suffix. `options`, both password
pointers, and `out_output` are required. `encrypt_metadata` accepts only `0` or
`1`. The method must be `QUANTAPDF_ENCRYPTION_AES_256`; unknown methods and
permission bits are `QUANTAPDF_ERROR_ARGUMENT`.

`QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION` grants degraded printing.
`QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY` upgrades printing to full quality
and is invalid unless the low-resolution bit is also set. The other bits map
exactly to qpdf's `allow_modify_other`, `allow_extract`,
`allow_annotate_and_form`, `allow_form_filling`, and `allow_assemble`
parameters. The public names deliberately reflect that PDF bit 4 means
"modifications other than those controlled by bits 6, 9, and 11" and that PDF
bit 6 permits both annotation changes and filling existing form fields.

The combinations are explicit: `FILL_FORMS` alone permits filling existing
fields; `ANNOTATE_AND_FILL_FORMS` permits annotation changes and also filling
existing fields; their union adds no third capability; and combining
`MODIFY_OTHER` with `ANNOTATE_AND_FILL_FORMS` additionally permits creating or
modifying interactive form fields as defined by the Standard Security Handler.
PDF 2.0 requires accessibility extraction to remain available for R6, so V1
does not expose a misleading accessibility toggle and always passes true to
qpdf. PDF permissions are advisory viewer hints, not DRM or an authorization
boundary.

The release becomes 2.6.0. This is an append-only ABI 2 change: no existing
layout or symbol changes, the shared-library major remains 2, and the exact
Windows export baseline grows from 95 to 98 names.

## Password contract

Both public password values are NUL-terminated strings borrowed only for the
duration of the call. Embedded NUL cannot be represented by this V1 ABI. To
make the qpdf 12.4 library boundary standards-compliant without adding ICU,
V1 accepts only the invariant printable ASCII subset of UTF-8: every byte must
be in `0x20..0x7e`, and the sequence must be at most 127 bytes before the
terminator. Printable ASCII is unchanged by the required SASLprep-style
Unicode normalization and bidirectional preparation. QuantaPDF rejects any
non-ASCII, control, DEL, or longer value with `QUANTAPDF_ERROR_ARGUMENT`; it
never truncates. A future size-tagged extension may add prepared Unicode
passwords without weakening this V1 contract.

An empty user password is allowed. This creates an encrypted file that readers
can normally open without prompting while still publishing advisory
permissions. The owner password must be nonempty and byte-distinct from the
user password. Since all accepted characters are preparation-invariant,
byte-distinct also means distinct after password preparation. Requiring a
separate owner credential prevents the insecure R6 policies that qpdf's
low-level library API can technically serialize. No locale or platform-code-
page conversion occurs.

`quantapdf_open(filename, password, ...)` remains the sole authentication
gate. PDFium and qpdf must both accept the supplied credential before a
document is published. Once authenticated, a user password and an owner
password both authorize decrypt or re-encrypt: PDF permission bits are
advisory and QuantaPDF does not treat them as kernel authorization controls.
Wrong or missing credentials fail during open with
`QUANTAPDF_ERROR_PASSWORD`, so an unauthenticated document can never reach a
successful rewrite call.

## Operation state machine

Every operation follows this synchronous single-caller state machine:

```text
validate ABI/policy and clear output
    -> strict fresh qpdf parse with stored authenticated password
    -> force page/object materialization and reject warnings
    -> bounded reachable audit plus all-object signature preflight
    -> reject current-revision signature structures
    -> validate required encrypted/unencrypted source state
    -> configure one QPDFWriter security policy
    -> write to a private memory buffer
    -> copy to a new quantapdf_output allocation
    -> publish exactly once
```

`quantapdf_encrypt_pdf` requires the audit to report no encryption.
`quantapdf_decrypt_pdf` and `quantapdf_reencrypt_pdf` require it to report
encryption. A source-state mismatch returns `QUANTAPDF_ERROR_STATE`, making
the caller's intended operation explicit. Any current-revision signature
structure returns `QUANTAPDF_ERROR_UNSUPPORTED` for all three operations
because a full rewrite invalidates existing signatures. The dedicated
preflight checks catalog `/Perms` (`/DocMDP`, `/UR`, `/UR3`), inherited
AcroForm signature fields, document timestamps, and every object returned by
`QPDF::getAllObjects()` for signature dictionaries, including objects not
reachable from the catalog. Malformed signature fields and dictionaries
return `QUANTAPDF_ERROR_FORMAT`. Signed incremental-update fixtures must be
detected. Bytes from superseded historical revisions that are no longer
represented in the current xref are not treated as an extant signature and
are outside V1's structural contract.

The accepted legacy-source policy is deliberately broad: decrypt and
reencrypt accept any conventional encrypted source that both the existing
PDFium open path and strict qpdf 12.4 parse can authenticate and fully
materialize without warnings. Output from reencrypt is always R6/AES-256;
decrypt never silently upgrades or preserves source encryption.

No JavaScript, action, embedded payload, form event, or annotation event is
executed. Security rewrite is structural serialization only. Active content
is preserved because sanitization remains a separate explicit operation.

## qpdf writer policy

Encryption and re-encryption call
`QPDFWriter::setR6EncryptionParameters()` with the validated passwords,
permission mapping, and metadata flag. This disables preservation of old
encryption parameters and causes qpdf to generate a new R6 key, salts, IVs,
and security material. The writer disables object streams, preserves encoded
stream data, discards unreachable objects, and does not enable deterministic
or static IDs.

Decryption calls `QPDFWriter::setPreserveEncryption(false)`, uses a
deterministic content-derived ID, disables object streams, preserves encoded
stream data, and discards unreachable objects. Its output contains no
`/Encrypt` entry and repeated calls on the same source produce byte-identical
canonical output.

Production encryption is intentionally non-deterministic. Repeated calls with
the same source, passwords, and policy must be semantically equivalent but are
expected to differ in encrypted bytes and security material. Tests inspect the
R6 dictionary and decrypted semantics rather than freezing ciphertext. No
public seed, nonce, IV, entropy provider, static-ID switch, or deterministic
encryption option exists.

## Entropy and provider boundary

QuantaPDF owns one narrow private `secure_random(bytes)` primitive backed by
`BCryptGenRandom` on Windows, `getrandom` with a fail-closed `/dev/urandom`
fallback on supported Unix systems, and `SecRandomCopyBytes` on Apple systems.
It has no deterministic fallback and maps OS failure to a non-success status.
A private qpdf `RandomDataProvider` adapter delegates every request to this
primitive, so qpdf's R6 keys, salts, and IVs do not depend on ambient qpdf
provider state.

qpdf 12.4 exposes its provider as process-global mutable state. A private mutex
therefore serializes the short install/configure/write/restore scope. RAII
restores the exact prior provider on every success or exception, and no other
QuantaPDF path changes it. Supported shared-library builds additionally hide
all dependency symbols: Windows exports only `QUANTAPDF_API`; ELF uses an exact
version script, archive exclusion, hidden default visibility, and local symbol
binding; Mach-O uses an exact exported-symbol list and hidden dependency
symbols. Thus a co-resident qpdf library cannot resolve or mutate QuantaPDF's
private qpdf copy. Static archives are not a dynamic isolation boundary, but a
provider installed before a QuantaPDF call is still overridden and restored;
concurrent direct mutation of dependency-private qpdf globals by a static-link
consumer is outside the public ABI contract.

Tests install a sentinel provider before encryption, prove it cannot determine
the output security material, and prove it is restored afterward. Shared-build
symbol tests assert that the exact QuantaPDF C ABI is the only dynamic export.
Test helpers may use qpdf's static-ID/static-IV facilities only in separate
fixture processes and never while a public QuantaPDF operation is executing.

## Metadata and file identifiers

`encrypt_metadata == 1` uses the R6 default and may omit
`/EncryptMetadata true`; omission has the standards-defined effective value
true. `encrypt_metadata == 0` writes the standards-correct false policy and
leaves metadata outside the encrypted crypt filter. Tests inspect the effective
value, verify metadata remains observable after authentication in both modes,
and inspect the unauthenticated raw metadata stream to prove plaintext is
present only when the effective value is false.

Before encryption, QuantaPDF validates an existing `/ID` as a two-string array
with a nonempty first string. It preserves that permanent first identifier.
When `/ID` is absent, it generates a fresh 16-byte first identifier with
`secure_random`. A temporary, private 32-byte random Info value is installed
only while `setR6EncryptionParameters()` eagerly calls qpdf's `generateID`;
the original Info object is restored before serialization. This makes qpdf's
new 16-byte second/update identifier depend on CSPRNG input without adding or
altering document metadata. The qpdf writer has then cached both IDs and the
R6 key derivation has consumed ID1. Malformed `/ID` is
`QUANTAPDF_ERROR_FORMAT`. Tests cover absent, valid, malformed, and same-second
repeated calls. Decryption preserves a valid permanent ID1, emits a
content-derived deterministic ID2, and never leaves an `/Encrypt` dependency.

## Ownership and memory protocol

The authoritative input is the immutable `source_data/source_size` allocation
owned by `quantapdf_document`. The document owns one password allocation plus
an explicit byte length; the qpdf document borrows this allocation instead of
retaining a duplicate `std::string`. Backend calls borrow it only until the
synchronous call returns. It creates one private QPDF owner and one private
writer buffer. No QPDF pointer, string view, password view, or borrowed source
pointer crosses the C++ bridge.

Document destruction wipes the full recorded password length through a
non-optimizable secure-zero helper before freeing it. New-policy passwords are
borrowed directly after ASCII validation; any QuantaPDF-owned temporary secret
or prepared buffer is wiped on every exit. qpdf receives password pointers only
for its immediate parameter computation, and no QuantaPDF-owned password copy
is retained by the writer after publication.

The backend copies successful writer bytes into a `malloc` allocation. The C
facade owns an unpublished zeroed `quantapdf_output` shell, adopts that byte
allocation only on success, and publishes the shell through `*out_output` as
the final state transition. The caller owns the published immutable output and
releases it with `quantapdf_drop_output()`; it remains valid after the source
document closes.

Every public function resets a non-NULL `*out_output` to NULL before any other
validation. Allocation, qpdf, format, policy, source-state, and post-write
failures free all private allocations and leave NULL output. Size calculations
use existing qpdf/Buffer `size_t` bounds and one checked allocation; no
unbounded auxiliary collection is introduced beyond the existing bounded
audit and qpdf's document graph.

The operation is synchronous and adds no queue, callback, background task,
shutdown phase, or cancellation state. Its one process-wide mutex protects the
unavoidable transient qpdf provider swap and contains no PRNG state. As with
the rest of ABI 2, callers serialize concurrent access to one document.

## Error semantics

- `QUANTAPDF_ERROR_ARGUMENT`: NULL required value; short options record;
  unknown method/permission; invalid boolean; invalid permission combination;
  invalid, overlong, empty-owner, or equal passwords.
- `QUANTAPDF_ERROR_STATE`: encrypt called on encrypted input, or decrypt/
  reencrypt called on unencrypted input.
- `QUANTAPDF_ERROR_PASSWORD`: authentication failed during
  `quantapdf_open()` or the strict private reparse.
- `QUANTAPDF_ERROR_FORMAT`: malformed PDF/security structure or any strict
  parse warning needed to complete the rewrite.
- `QUANTAPDF_ERROR_UNSUPPORTED`: reachable signature structure or a source
  encryption feature qpdf cannot safely rewrite.
- `QUANTAPDF_ERROR_NOMEM`: public shell/copy allocation or qpdf allocation
  failure recognizable as `std::bad_alloc`.
- `QUANTAPDF_ERROR_IO`: system I/O failure reported by qpdf.
- `QUANTAPDF_ERROR_BACKEND`: other qpdf/provider exception or an empty writer
  result.

## Semantic preservation and verification

Apart from the encryption dictionary, encrypted representation, file IDs, PDF
version required by R6, and unreachable-object collection, all source
semantics remain unchanged. Tests cover:

1. compile-time ABI availability, size tags, enum values, and exactly 98 DLL
   exports;
2. NULL/sentinel output, short/extended options, unknown bits/method,
   permission dependencies, and boolean validation;
3. printable ASCII, non-ASCII UTF-8, control/DEL, 127/128-byte boundaries,
   empty user, empty owner, and equal-password policies;
4. unencrypted-to-R6 encryption with PDF 1.7 extension level 8, `V=5`, `R=6`,
   AESV3 crypt filters, the requested permissions, and metadata true/false;
5. wrong-password rejection and correct user/owner password authentication;
6. authenticated decryption with no `/Encrypt` and deterministic repeated
   output;
7. re-encryption with a new password/policy, rejection of old credentials,
   and fresh non-identical security material;
8. production non-determinism for repeated encrypt calls;
9. source-state mismatch and source immutability;
10. output validity after source close and failure-atomic ownership;
11. reachable and orphan `/Sig`, DocMDP, document timestamp, malformed
    signature field, signed incremental input, and malformed/unsupported
    encrypted input mapping;
12. preservation of render pixels, text/search, image observations, links,
    metadata, outline, annotations, AcroForm values/widgets, named
    destinations, page boxes, page order, and ordinary stream data;
13. no regression in audit, sanitize, rewrite, recompress, flatten, edit, or
    Composer behavior;
14. authenticated semantic metadata checks plus unauthenticated raw metadata
    stream checks for both `encrypt_metadata` settings;
15. Release and MSVC ASan builds, release install, exact dynamic export and
    dependency-symbol isolation inspection, and an installed-header C smoke
    consumer.

The dedicated CTest target is `quantapdf.pdf_security_rewrite`. CI runs it in
the existing Linux release + ASan/UBSan, macOS, and Windows DLL matrix using
the exact repository SHA before integration.

## Non-goals

No RC4, DES, R2-R5 output, AES-128 output, certificate/public-key encryption,
signature creation/validation/preservation/removal, direct encrypted editing,
incremental encryption update, caller-provided entropy, deterministic
production encryption, password recovery/cracking, DRM claim, key escrow,
public crypt-filter dictionaries, public raw `/P` integers, or changes to the
security behavior of any existing operation.

## Files

- `include/quantapdf/quantapdf.h`: V1 enums/options, three functions, and
  2.6.0 version.
- `src/pdf_security_rewrite.c`: ABI/policy/password validation and failure-
  atomic output facade.
- `src/backend/qpdf_document.h`: private security rewrite bridge declarations.
- `src/backend/qpdf_document.cpp`: strict state/audit preflight and qpdf writer
  implementation.
- `src/backend/secure_random.h` and `src/backend/secure_random.cpp`: private
  OS CSPRNG and qpdf provider guard.
- `src/internal.h` and `src/document.c`: single password owner, explicit length,
  and secure erasure.
- `cmake/QuantaPDFExports.cmake`: exact shared-library export isolation.
- `tests/test_pdf_security_rewrite.c`: public API, state, ownership, password,
  and semantic tests.
- `tests/pdf_security_rewrite_test_helpers.cpp`: qpdf raw-security inspection
  and fixture helpers.
- `tests/pdf_security_rewrite_fault_hook.c`: test-only facade fault injection.
- `tests/CMakeLists.txt`: `quantapdf.pdf_security_rewrite` registration.
- `abi/quantapdf-v2.exports`: three appended ABI names.
- `README.md`: security rewrite contract and example.
- `docs/releases/v2.6.0.md`: release and verification record.
