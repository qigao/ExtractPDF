# QuantaPDF PDF Security Rewrite V1 Design

**Status:** Approved for implementation by the request to finish issue #58

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
    QUANTAPDF_PERMISSION_PRINT = 1u << 0,
    QUANTAPDF_PERMISSION_MODIFY = 1u << 1,
    QUANTAPDF_PERMISSION_COPY = 1u << 2,
    QUANTAPDF_PERMISSION_ANNOTATE = 1u << 3,
    QUANTAPDF_PERMISSION_FILL_FORMS = 1u << 4,
    QUANTAPDF_PERMISSION_ACCESSIBILITY = 1u << 5,
    QUANTAPDF_PERMISSION_ASSEMBLE = 1u << 6,
    QUANTAPDF_PERMISSION_HIGH_QUALITY_PRINT = 1u << 7,
    QUANTAPDF_PERMISSION_ALL = (1u << 8) - 1u
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

`QUANTAPDF_PERMISSION_PRINT` grants low-resolution printing.
`QUANTAPDF_PERMISSION_HIGH_QUALITY_PRINT` upgrades printing to full quality
and is invalid unless `QUANTAPDF_PERMISSION_PRINT` is also set. The other bits
map to qpdf's `allow_modify_other`, `allow_extract`,
`allow_annotate_and_form`, `allow_form_filling`, `allow_accessibility`, and
`allow_assemble` parameters respectively.

PDF 2.0 requires accessibility extraction to remain available for R6 and qpdf
therefore treats the corresponding setting as granted. V1 requires callers to
include `QUANTAPDF_PERMISSION_ACCESSIBILITY`; omitting it is an invalid policy,
not a silently ignored restriction. PDF permissions are advisory viewer hints,
not DRM or an authorization boundary.

The release becomes 2.6.0. This is an append-only ABI 2 change: no existing
layout or symbol changes, the shared-library major remains 2, and the exact
Windows export baseline grows from 95 to 98 names.

## Password contract

Both public password values are NUL-terminated UTF-8 byte strings borrowed
only for the duration of the call. Embedded NUL cannot be represented by this
V1 ABI. Each password must be well-formed UTF-8 and at most 127 bytes before
the terminator, matching the R6 algorithm limit. QuantaPDF rejects an invalid
sequence or longer value with `QUANTAPDF_ERROR_ARGUMENT`; it never truncates.

An empty user password is allowed. This creates an encrypted file that readers
can normally open without prompting while still publishing advisory
permissions. The owner password must be nonempty and byte-distinct from the
user password. Requiring a separate owner credential prevents the insecure
R6 policies that qpdf's low-level library API can technically serialize.

QuantaPDF validates UTF-8 but performs no Unicode normalization, bidirectional
transformation, locale conversion, or platform-code-page conversion. The
caller supplies the exact compliant UTF-8 sequence. This preserves the
project's explicit byte-string contract and avoids hidden ICU behavior.

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
    -> bounded reachable audit
    -> reject signature structures
    -> validate required encrypted/unencrypted source state
    -> configure one QPDFWriter security policy
    -> write to a private memory buffer
    -> copy to a new quantapdf_output allocation
    -> publish exactly once
```

`quantapdf_encrypt_pdf` requires the audit to report no encryption.
`quantapdf_decrypt_pdf` and `quantapdf_reencrypt_pdf` require it to report
encryption. A source-state mismatch returns `QUANTAPDF_ERROR_STATE`, making
the caller's intended operation explicit. Any reachable signature structure
returns `QUANTAPDF_ERROR_UNSUPPORTED` for all three operations because a full
rewrite invalidates existing signatures.

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

The vcpkg qpdf 12.4.0 port builds and requires qpdf's native crypto provider.
Its default configuration keeps OS secure random enabled and insecure random
disabled. qpdf owns AES, SHA-2, R6 derivation, salts, IVs, keys, and random
file/security identifiers. CSPRNG failure throws inside qpdf and maps to a
non-success QuantaPDF status; there is no fallback to `rand()`, timestamps,
addresses, or a mutable QuantaPDF PRNG.

QuantaPDF does not install a qpdf `RandomDataProvider`, because that hook is a
process-global mutable provider and would weaken the library's caller-side
serialization boundary. Tests use production entropy for public-path checks.
Test helpers may use qpdf's documented static-ID/static-IV facilities only for
fixtures they create themselves; those helpers are not linked into the public
library and cannot affect production calls.

## Metadata and file identifiers

`encrypt_metadata == 1` uses the R6 default and may omit
`/EncryptMetadata true`; omission has the standards-defined effective value
true. `encrypt_metadata == 0` writes the standards-correct false policy and
leaves metadata outside the encrypted crypt filter. Tests inspect the effective
value and verify metadata remains observable after authentication.

For a valid source `/ID`, qpdf preserves the permanent first identifier where
appropriate and creates fresh update/security material required by the new
encryption operation. When an identifier is absent, qpdf creates the required
identifier from its secure random provider. Decryption emits a structurally
valid deterministic identifier and never leaves an `/Encrypt` dependency.

## Ownership and memory protocol

The authoritative input is the immutable `source_data/source_size` allocation
owned by `quantapdf_document`. The backend borrows it and the stored password
only until the synchronous call returns. It creates one private QPDF owner and
one private writer buffer. No QPDF pointer, string view, password view, or
borrowed source pointer crosses the C++ bridge.

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

The operation is single-call/single-threaded and adds no queue, callback,
background task, global mutable state, shutdown phase, or cancellation state.
As with the rest of ABI 2, callers serialize concurrent access to one document.

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
3. valid ASCII and multibyte UTF-8, malformed/overlong UTF-8, 127/128-byte
   boundaries, empty user, empty owner, and equal-password policies;
4. unencrypted-to-R6 encryption with `V=5`, `R=6`, AESV3 crypt filters, the
   requested permissions, and metadata true/false;
5. wrong-password rejection and correct user/owner password authentication;
6. authenticated decryption with no `/Encrypt` and deterministic repeated
   output;
7. re-encryption with a new password/policy, rejection of old credentials,
   and fresh non-identical security material;
8. production non-determinism for repeated encrypt calls;
9. source-state mismatch and source immutability;
10. output validity after source close and failure-atomic ownership;
11. signed input rejection and malformed/unsupported encrypted input mapping;
12. preservation of render pixels, text/search, image observations, links,
    metadata, outline, annotations, AcroForm values/widgets, named
    destinations, page boxes, page order, and ordinary stream data;
13. no regression in audit, sanitize, rewrite, recompress, flatten, edit, or
    Composer behavior;
14. Release and MSVC ASan builds, release install, exact export inspection,
    and an installed-header C smoke consumer.

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
- `tests/test_pdf_security_rewrite.c`: public API, state, ownership, password,
  and semantic tests.
- `tests/pdf_security_rewrite_test_helpers.cpp`: qpdf raw-security inspection
  and fixture helpers.
- `tests/pdf_security_rewrite_fault_hook.c`: test-only facade fault injection.
- `tests/CMakeLists.txt`: `quantapdf.pdf_security_rewrite` registration.
- `abi/quantapdf-v2.exports`: three appended ABI names.
- `README.md`: security rewrite contract and example.
- `docs/releases/v2.6.0.md`: release and verification record.
