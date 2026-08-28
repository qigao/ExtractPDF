# ExtractPDF Immutable Output File Save Design

Date: 2026-08-28  
Status: approved design  
Tracks: #29, umbrella #2  
Stacked base: #27 / PR #28 head `a6d35b6f5d68207cf9c628b2b38c35addb868d82`  
Branch: `feat/output-save-file`

## Goal

Close the remaining Phase 4 PDF Composition `Save/write with explicit ownership and error handling` item without moving filenames into the composition engine.

ExtractPDF already materializes every composition result as an immutable, ExtractPDF-owned `extractpdf_output` byte snapshot. V1 adds one optional terminal adapter that writes those already-materialized bytes to a filesystem path:

```c
EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);
```

The adapter is deliberately downstream of composition:

```text
source document(s)
        |
        v
export / range / reorder / delete / duplicate / merge
        |
        v
immutable extractpdf_output
        |
        +--> extractpdf_output_data(...)      borrowed bytes
        |
        +--> extractpdf_output_save_file(...) optional terminal adapter
        |
        v
extractpdf_drop_output(...)
```

No export, range, delete, duplicate, or merge API gains a filename parameter.

## Architectural decision

V1 chooses a **file-save adapter on `extractpdf_output`** rather than a callback sink or a new composition-to-file path.

Three approaches were considered.

### 1. Output file adapter — selected

```c
extractpdf_output_save_file(output, filename);
```

Benefits:

- composition remains memory-backed and filename-free;
- the adapter consumes the exact immutable bytes already exposed by `extractpdf_output_data(...)`;
- no second PDF writer, MuPDF context, or composition path is introduced;
- C callers and .NET/PInvoke callers receive a simple status-based terminal operation;
- the existing UTF-8 public path contract can be preserved explicitly;
- ownership remains trivial because the output is not consumed.

### 2. Generic write callback — rejected for V1

A callback shape could write to files, sockets, streams, or custom storage, but would create a larger stable ABI contract around:

- callback lifetime;
- short writes;
- callback failures;
- reentrancy;
- thread behavior;
- user-data lifetime;
- .NET delegate pinning/marshalling.

The current output is already fully materialized in memory, so that complexity does not buy a meaningful V1 capability.

### 3. No save API — rejected for roadmap closure

Callers can already use `extractpdf_output_data(...)` and perform their own file I/O. Keeping only that surface would preserve the smallest possible ABI, but the library would still lack a common UTF-8 filesystem/error contract for the Phase 4 `Save/write` roadmap item.

The selected adapter adds that terminal contract without changing composition semantics.

## Public ABI

The only new public declaration is:

```c
EXTRACTPDF_API extractpdf_status extractpdf_output_save_file(
    const extractpdf_output *output,
    const char *filename);
```

It belongs beside the existing `extractpdf_output_data(...)` lifecycle surface.

No public struct, options object, callback type, file handle, MuPDF type, or save-session handle is introduced.

## Terminal-adapter boundary

`extractpdf_output_save_file(...)` is **not** a PDF composition primitive and is **not** a PDF serialization primitive.

By the time it is called, `extractpdf_output` already contains the complete final PDF file image:

```text
struct extractpdf_output   // private
    unsigned char *data
    size_t size
```

The adapter only transfers those bytes to host filesystem storage.

It must not:

- call `pdf_write_document`;
- create an `fz_context`;
- open/reparse the PDF through MuPDF;
- modify PDF contents;
- regenerate document IDs;
- change deterministic serialization options;
- consume or replace the output object;
- retain the filename after return.

Therefore the adapter cannot alter page order, preservation policy, PDF metadata, or byte determinism established by the composition engine.

## Ownership and lifetime

The save operation is **non-consuming** on both success and failure.

A valid call sequence is:

```c
extractpdf_output *output = NULL;

/* produce output through export/merge */

extractpdf_output_save_file(output, "first.pdf");
extractpdf_output_save_file(output, "second.pdf");
extractpdf_output_data(output, &data, &size);
extractpdf_output_save_file(output, "third.pdf");
extractpdf_drop_output(output);
```

The caller still owns the same `extractpdf_output` until `extractpdf_drop_output(...)`.

`extractpdf_output_save_file(...)` never:

- frees `output`;
- frees or replaces `output->data`;
- modifies `output->size`;
- retains a borrowed pointer to the output;
- creates another public handle.

A failed save leaves the immutable output usable for another save, `extractpdf_output_data(...)`, or final drop.

## Argument contract

Return `EXTRACTPDF_ERROR_ARGUMENT` when:

- `output == NULL`;
- `filename == NULL`;
- `filename[0] == '\0'`;
- on Windows, `filename` is not valid UTF-8 under strict conversion.

A non-NULL `extractpdf_output *` is treated according to the existing opaque-handle invariant. V1 does not expose private output fields or add a second public output-validity contract.

## UTF-8 path contract

All public ExtractPDF paths are UTF-8. The save adapter must preserve that contract explicitly on every supported platform.

### Windows

Do not pass the public UTF-8 string to narrow `fopen()` or `_fsopen()` and therefore do not depend on the active ANSI code page.

The intended conversion is:

```text
UTF-8 filename
      |
      v
MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    filename,
    -1,
    NULL,
    0)
      |
      v
allocate wchar_t buffer
      |
      v
MultiByteToWideChar(... actual buffer ...)
      |
      v
UTF-16 filename
      |
      v
_wfopen_s(..., L"wb")
```

The first `MultiByteToWideChar` call determines the required UTF-16 character count including the terminator because the source length is `-1`.

If strict UTF-8 conversion fails, return `EXTRACTPDF_ERROR_ARGUMENT`.

If allocating the UTF-16 path buffer fails, return `EXTRACTPDF_ERROR_NOMEM`.

The UTF-16 buffer is private temporary memory and is freed before return on every path.

### Linux and macOS

Use the public UTF-8 byte path directly with the host binary stdio open path:

```c
fopen(filename, "wb")
```

No locale-dependent conversion layer is introduced by ExtractPDF.

The library does not attempt path normalization or reinterpret UTF-8 bytes beyond the platform rules already implied by the existing public path contract.

## File creation and overwrite semantics

The file is opened for binary write using ordinary truncate/create semantics:

```text
missing target
    -> create

existing regular file
    -> truncate then write
```

V1 does not append.

V1 does not automatically create missing parent directories.

If the parent path does not exist or the target cannot be opened for writing, return `EXTRACTPDF_ERROR_IO`.

## Exact-write requirement

A successful save means **all** `output->size` bytes were accepted by stdio.

The implementation must not assume one `fwrite` call always writes the complete output.

Conceptually:

```text
offset = 0

while offset < output->size:
    written = fwrite(output->data + offset, 1, output->size - offset, file)

    if written == 0:
        fail with IO

    offset += written
```

A positive short write is followed by another write attempt.

A zero-byte write before the requested total is reached is an I/O failure. The implementation may consult `ferror(file)` for diagnostics/control, but the public status remains `EXTRACTPDF_ERROR_IO`.

Because composition outputs are currently required to contain a non-empty complete PDF image, V1 does not add an empty-output special case.

## Flush and close semantics

`EXTRACTPDF_OK` is returned only after all three stages succeed:

```text
1. exact byte write
2. fflush(file) succeeds
3. fclose(file) succeeds
```

If exact writing succeeds but `fflush` fails, return `EXTRACTPDF_ERROR_IO`.

The opened stream must still be closed after a write or flush failure. A save that already has an I/O failure remains `EXTRACTPDF_ERROR_IO` regardless of the close result.

If writing and flushing succeed but `fclose` reports failure, return `EXTRACTPDF_ERROR_IO`.

This makes close-time writeback errors observable rather than silently treating them as success.

## What `EXTRACTPDF_OK` does not mean

The success contract is deliberately limited to stdio completion:

```text
all bytes written
+ fflush succeeded
+ fclose succeeded
```

V1 does **not** promise:

- `fsync`;
- `fdatasync`;
- Windows `FlushFileBuffers`;
- macOS `F_FULLFSYNC`;
- power-loss durability;
- atomic replacement;
- rollback after partial failure;
- preservation of an old target if writing the replacement fails.

Those are separate filesystem/durability policies and would require a different API contract.

## Non-atomic failure semantics

The V1 file adapter is intentionally not transactional.

After an I/O failure, the target path may be:

- absent;
- newly created but empty;
- truncated;
- partially written.

The adapter does not restore a previous target and does not remove a partial output automatically.

This behavior is explicit so callers requiring atomic-replace semantics can implement an application-level temporary-file/rename policy without ExtractPDF pretending to provide guarantees it does not actually enforce.

The input `extractpdf_output`, however, remains unchanged and reusable regardless of filesystem failure.

## Error mapping

The public mapping is:

```text
output == NULL                         -> EXTRACTPDF_ERROR_ARGUMENT
filename == NULL                       -> EXTRACTPDF_ERROR_ARGUMENT
filename == ""                         -> EXTRACTPDF_ERROR_ARGUMENT
invalid UTF-8 path on Windows          -> EXTRACTPDF_ERROR_ARGUMENT
Windows UTF-16 path allocation failure -> EXTRACTPDF_ERROR_NOMEM
file open failure                      -> EXTRACTPDF_ERROR_IO
short/failed write that cannot finish  -> EXTRACTPDF_ERROR_IO
fflush failure                         -> EXTRACTPDF_ERROR_IO
fclose failure                         -> EXTRACTPDF_ERROR_IO
otherwise                              -> EXTRACTPDF_OK
```

No new status enum is introduced.

No OS error code or `errno` value becomes part of the stable ABI in this slice.

## Filesystem policy deliberately left to the host

The adapter does not define special behavior for:

- symlinks;
- hard links;
- ACLs;
- inherited permissions;
- file ownership;
- network filesystems;
- antivirus/file-lock interactions;
- path canonicalization;
- case sensitivity;
- reserved Windows filenames;
- maximum path policy beyond the selected CRT/Windows APIs.

Those conditions either succeed according to the host or return `EXTRACTPDF_ERROR_IO` through the ordinary open/write/flush/close path.

## Production responsibility split

The expected implementation boundary is intentionally narrow:

```text
include/extractpdf/extractpdf.h
    add extractpdf_output_save_file declaration

src/output_file.c
    validate arguments
    perform platform-specific UTF-8 file open
    exact write loop
    flush
    close
    return ExtractPDF status

CMakeLists.txt
    register src/output_file.c
```

The new production file must not include MuPDF headers directly and must not call MuPDF APIs.

No changes are expected in:

```text
src/pdf_export.c
src/pdf_output.c
src/pdf_merge.c
src/pdf_range.c
src/internal.h output layout
```

The private `extractpdf_output` definition remains exactly the current `data + size` snapshot.

## Windows implementation boundary

`src/output_file.c` may use compile-time platform branching:

```c
#ifdef _WIN32
    /* strict UTF-8 -> UTF-16 + _wfopen_s */
#else
    /* fopen(filename, "wb") */
#endif
```

Windows-only includes and helpers remain inside this terminal I/O implementation rather than creating a general platform subsystem before another feature needs one.

No public Windows types appear in `extractpdf.h`.

## Deterministic test strategy

Add one focused test executable:

```text
extractpdf_test_output_file
CTest: extractpdf.output_file
```

Reuse the existing deterministic fixture:

```text
tests/fixtures/composition-three-page.pdf

page 0: PAGE-A, 200 x 200 pt
page 1: PAGE-B, 240 x 180 pt
page 2: PAGE-C, 300 x 150 pt
```

Create the test output through the public composition API:

```c
int indices[] = {2, 0};
extractpdf_export_pages(document, indices, 2, &output);
```

Expected immutable output semantics are:

```text
PAGE-C, 300 x 150
PAGE-A, 200 x 200
```

No new binary PDF fixture is required.

## ASCII exact-byte and overwrite proof

Use a build-directory target such as:

```text
composition-save-output.pdf
```

Before calling the save adapter, the test writes stale content that is longer than the final target representation, for example the current output bytes followed by an extra stale tail.

Then call:

```c
extractpdf_output_save_file(output, ASCII_OUTPUT_PDF);
```

The test reads the ASCII target with ordinary test-side binary stdio and requires:

```text
on-disk size == output size
on-disk bytes == extractpdf_output_data(...) bytes byte-for-byte
```

This proves both:

- existing-file truncation removes stale trailing bytes;
- the save adapter transfers the exact immutable snapshot rather than reserializing or altering it.

Reopen the saved file through `extractpdf_open(...)` and verify:

```text
page_count = 2
page 0 = PAGE-C, 300 x 150
page 1 = PAGE-A, 200 x 200
```

## UTF-8 filename proof

Use a separate build-directory filename containing non-ASCII UTF-8 text, for example:

```text
composition-save-测试.pdf
```

Call:

```c
extractpdf_output_save_file(output, UTF8_OUTPUT_PDF);
```

Do not use narrow Windows test-side `fopen()` to inspect this path, because that would make the test depend on the active ANSI code page.

Instead reopen the result through the existing public UTF-8 path API:

```c
extractpdf_open(UTF8_OUTPUT_PDF, NULL, &saved_document);
```

Then verify the same two pages and geometry.

On MSVC, compile the new test target with `/utf-8`, matching the existing `extractpdf.document` UTF-8 path test precedent.

## Stable I/O failure proof

Do not use file permissions to manufacture an I/O error; elevated CI users and platform permission behavior make such tests unreliable.

Use a deterministic path whose parent directory is intentionally absent, for example:

```text
<build>/extractpdf-save-missing-parent/output.pdf
```

The test does not create that parent directory.

Calling:

```c
extractpdf_output_save_file(output, MISSING_PARENT_PDF);
```

must return:

```text
EXTRACTPDF_ERROR_IO
```

The save adapter must not create the parent directory.

A clean CI build directory makes this failure deterministic across Linux, macOS, and Windows.

## Windows invalid-UTF-8 proof

Under `_WIN32`, add a platform-specific path contract case using an invalid UTF-8 sequence, for example:

```c
const char invalid_utf8[] = { (char)0xC3, (char)0x28, '\0' };
```

Calling:

```c
extractpdf_output_save_file(output, invalid_utf8);
```

must return:

```text
EXTRACTPDF_ERROR_ARGUMENT
```

This directly proves `MB_ERR_INVALID_CHARS` behavior and prevents a regression to ANSI/narrow-path opening.

No corresponding invalid-byte test is required on POSIX/macOS because this API defines the public string encoding as UTF-8 but deliberately passes the byte path through to the host rather than adding a cross-platform Unicode validator.

## Ownership preservation proof

The same output is reused through success and failure:

```text
create output
    |
    +--> save ASCII path -> OK
    |
    +--> save UTF-8 path -> OK
    |
    +--> save missing-parent path -> IO
    |
    +--> Windows invalid UTF-8 -> ARGUMENT
    |
    v
extractpdf_output_data(output, ...)
```

Before the save calls, copy the original output bytes into test-owned memory.

After all success/failure cases, require the current output size and bytes to match that snapshot exactly.

This proves the terminal adapter does not consume or mutate its input on either success or failure.

Finally drop the output exactly once through `extractpdf_drop_output(...)`.

## Argument tests

Required tests include:

```text
extractpdf_output_save_file(NULL, valid_path)
    -> EXTRACTPDF_ERROR_ARGUMENT

extractpdf_output_save_file(output, NULL)
    -> EXTRACTPDF_ERROR_ARGUMENT

extractpdf_output_save_file(output, "")
    -> EXTRACTPDF_ERROR_ARGUMENT
```

No public output slot needs resetting because this function returns no handle or borrowed pointer.

## TDD boundary

This slice introduces real behavior and therefore requires a real RED.

The first RED commit is test-only:

```text
tests/test_output_file.c
tests/CMakeLists.txt
```

It must not add the public declaration or production implementation.

Expected exact RED:

- existing library source builds;
- all pre-existing test executables build;
- only `extractpdf_test_output_file` fails because `extractpdf_output_save_file` is absent;
- no fixture, encoding, or unrelated regression is accepted as the RED boundary.

The minimal GREEN then adds only:

```text
include/extractpdf/extractpdf.h
src/output_file.c
CMakeLists.txt
```

No composition-source refactor is expected.

## CI policy

Development remains Linux-first.

Required sequence:

```text
RED exact head
    -> only missing save-file API boundary

GREEN exact head
    -> Linux strict static build + all normal CTests
    -> Linux ASan/UBSan build + all CTests

same exact GREEN head + full-ci label
    -> Linux static + sanitizers
    -> macOS configure/build/test
    -> Windows DLL configure/build/test
```

The cross-platform checkpoint is mandatory for this slice because:

- `src/output_file.c` contains a dedicated Windows UTF-8 -> UTF-16 production path;
- the new function is a public `EXTRACTPDF_API` DLL export;
- the UTF-8 filename test must execute successfully on Windows, not merely compile.

No success claim may reuse CI evidence from an earlier head.

## Phase 4 closure rule

After the implementation has a true RED, Linux GREEN, and same-head Linux/macOS/Windows full-ci evidence, the umbrella Phase 4 checklist may mark:

```text
[x] Save/write with explicit ownership and error handling
```

At that point every currently defined Phase 4 composition roadmap item is behavior/evidence complete.

This does **not** authorize merging the stacked PR chain. The Save/write PR remains stacked on PR #28 and draft/unmerged until an explicit integration decision.

## Expected stacked strategy

The slice starts from the exact proven Merge head:

```text
master
  |
  +-- feat/pdf-export-pages       PR #20
        |
        +-- feat/pdf-export-range PR #22
              |
              +-- test/pdf-order-contract  PR #24
                    |
                    +-- test/pdf-delete-contract PR #26
                          |
                          +-- feat/pdf-merge-outputs PR #28
                                |
                                +-- feat/output-save-file #29 / future PR
```

The future Save/write PR base is `feat/pdf-merge-outputs` while PR #28 remains unmerged.

## Non-goals

This slice does not add:

- filename parameters to export/merge APIs;
- direct document-to-file composition;
- callback/stream writer ABI;
- output list/batch saving;
- automatic parent-directory creation;
- temporary-file management;
- atomic replacement;
- rollback of partially written files;
- `fsync`/`fdatasync`/`FlushFileBuffers` durability;
- append mode;
- file permission/ACL options;
- symlink policy;
- path normalization/canonicalization API;
- MuPDF-backed file output;
- new PDF serialization options;
- new output ownership semantics;
- Phase 5 interactive preservation;
- new concurrency guarantees.
