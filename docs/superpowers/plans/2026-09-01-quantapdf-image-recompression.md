# QuantaPDF Image Recompression V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an immutable QuantaPDF 2.4.0 transform that deterministically JPEG-recompresses a strict, bounded class of opaque 8-bit DeviceGray and DeviceRGB Image XObjects.

**Architecture:** qpdf owns strict reachable-resource traversal, unique indirect identity, stream decoding/replacement, and serialization; PDFium verifies render and public observation semantics. A thin private adapter configures qpdf `Pl_DCT` over the already pinned libjpeg-turbo 3.2.0, with an all-platform golden-byte gate before the public ABI is changed.

**Tech Stack:** C11 public facade, C++20 qpdf 12.4.0 backend, qpdf `Pl_DCT`, libjpeg-turbo 3.2.0 from pinned vcpkg, PDFium 154.0.8021.0 verification, CMake Presets, CTest, ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-image-recompression-design.md`

## Global Constraints

- Release version is exactly 2.4.0; `QUANTAPDF_ABI_VERSION` and SOVERSION remain 2.
- The public export baseline grows exactly from 87 to 88 by adding only `quantapdf_recompress_images`.
- PDFium 154.0.8021.0 and qpdf 12.4.0 remain the only PDF backends.
- JPEG encode/decode uses qpdf `Pl_DCT` and pinned libjpeg-turbo 3.2.0; no stb, MuPDF, or AGPL dependency is added.
- Public calls remain externally serialized under ABI v2.
- Source documents are immutable; output is owning and failure publication is atomic.
- Signed and encrypted inputs fail with `QUANTAPDF_ERROR_UNSUPPORTED`.
- Eligible raw samples are bounded per image; zero selects the fixed 64 MiB default.
- Inline images, resizing, colorspace conversion, alpha flattening, and generic compression policy remain out of scope.
- A Linux/macOS/Windows golden-byte mismatch stops work before Task 2.

---

## File map

- `src/backend/jpeg_encoder.h`: private fixed-profile encoder declaration and normalized image specification.
- `src/backend/jpeg_encoder.cpp`: qpdf `Pl_DCT` adapter, checked sample-size validation, and fixed libjpeg configuration.
- `tests/test_jpeg_encoder.cpp`: backend-private RGB/gray/repeat/golden-byte characterization.
- `include/quantapdf/quantapdf.h`: append-only options structure, constants, and one public transform declaration.
- `src/pdf_image_recompression.c`: C validation, option normalization, owning output shell, and failure-atomic publication.
- `src/backend/qpdf_document.h`: private recompression bridge declaration.
- `src/backend/qpdf_document.cpp`: fresh-graph preflight, bounded resource traversal, eligibility, provider registration, and serialization.
- `tests/test_pdf_image_recompression.c`: public C ABI, ownership, determinism, policy, and error tests.
- `tests/image_recompression_test_helpers.h`: test-only C bridge declarations.
- `tests/image_recompression_test_helpers.cpp`: qpdf fixture generation and structural inspection without exposing qpdf publicly.
- `tests/fixtures/image-recompression-expected-q90.pdf`: checked complete output bytes for the controlled cross-platform quality-90 case.
- `tests/CMakeLists.txt`: two CTest targets and fixture/output definitions.
- `CMakeLists.txt`: 2.4.0 version, production sources, private JPEG linkage/notice installation.
- `abi/quantapdf-v2.exports`: one appended symbol.
- `tests/test_version.c`: 2.4.0, ABI 2, and shared-library major assertions.
- `README.md`: public usage, lossy boundary, byte cap, ownership, and non-goals.
- `THIRD_PARTY.md`: direct JPEG behavior, libjpeg-turbo 3.2.0 license, and notice path.
- `.github/workflows/ci.yml`: no new platform policy; existing full-ci matrix executes both new CTests.

## Phase A integration gate: qualify the encoder before public ABI work

### Task 1: Fixed-profile JPEG adapter and all-platform golden bytes

**Files:**
- Create: `src/backend/jpeg_encoder.h`
- Create: `src/backend/jpeg_encoder.cpp`
- Create: `tests/test_jpeg_encoder.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `THIRD_PARTY.md`

**Interfaces:**
- Consumes: qpdf `Pl_DCT`, `Pl_Buffer`, and libjpeg configuration types supplied transitively by `qpdf::libqpdf`.
- Produces:

```cpp
namespace quantapdf::detail {
struct jpeg_image_spec {
    size_t width;
    size_t height;
    int components;
    int quality;
};

class jpeg_encoder {
  public:
    ~jpeg_encoder();
    jpeg_encoder(jpeg_encoder const&) = delete;
    jpeg_encoder& operator=(jpeg_encoder const&) = delete;
    static quantapdf_status create(
        jpeg_image_spec const& spec,
        Pipeline* next,
        std::unique_ptr<jpeg_encoder>* out) noexcept;
    Pipeline* pipeline() noexcept;
};
}
```

The wrapper owns both `Pl_DCT::CompressConfig` and `Pl_DCT`, so the callback
configuration outlives the pipeline. It accepts decoded bytes incrementally
and does not expose a raw-sample or encoded-byte vector API.

- [ ] **Step 1: Add a compile-red characterization test**

Create declaration scaffolding in `jpeg_encoder.h` and a `jpeg_encoder.cpp`
factory stub that clears `*out` and returns `QUANTAPDF_ERROR_BACKEND`. Create
`tests/test_jpeg_encoder.cpp` with fixed 8x8 gray and 8x8 RGB arrays. For each
quality 40, 90, and 100, create a `Pl_Buffer` sink and encoder, write the fixed
array to `encoder->pipeline()`, finish once, and collect sink bytes. Repeat
each case and assert JPEG SOI/EOI markers plus equality. Golden arrays are
added after the first successful local encoding in Step 4. This makes the
initial RED a deliberate test failure, not a CMake configure failure caused
by a nonexistent source file.

```cpp
static void require_same(std::vector<unsigned char> const& a,
                         std::vector<unsigned char> const& b)
{
    if (a != b)
        std::abort();
}

static std::vector<unsigned char> encode_once(
    quantapdf::detail::jpeg_image_spec const& spec,
    unsigned char const* samples,
    size_t size)
{
    Pl_Buffer sink("jpeg-golden", nullptr);
    std::unique_ptr<quantapdf::detail::jpeg_encoder> encoder;
    if (quantapdf::detail::jpeg_encoder::create(spec, &sink, &encoder) !=
        QUANTAPDF_OK)
        std::abort();
    encoder->pipeline()->write(samples, size);
    encoder->pipeline()->finish();
    return copy_buffer(sink);
}

static void encode_twice(
    quantapdf::detail::jpeg_image_spec const& spec,
    unsigned char const* samples,
    size_t size,
    std::vector<unsigned char> const& golden)
{
    auto first = encode_once(spec, samples, size);
    auto second = encode_once(spec, samples, size);
    require_same(first, second);
    require_same(first, golden);
}
```

- [ ] **Step 2: Register the private test and verify compile RED**

Add a target that compiles `src/backend/jpeg_encoder.cpp` directly and links
`qpdf::libqpdf` without adding the adapter to the production library yet:

```cmake
add_executable(quantapdf_test_jpeg_encoder
  test_jpeg_encoder.cpp
  ../src/backend/jpeg_encoder.cpp)
target_include_directories(quantapdf_test_jpeg_encoder PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quantapdf_test_jpeg_encoder PRIVATE qpdf::libqpdf)
target_compile_features(quantapdf_test_jpeg_encoder PRIVATE cxx_std_20)
add_test(NAME quantapdf.jpeg_encoder COMMAND quantapdf_test_jpeg_encoder)
```

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target quantapdf_test_jpeg_encoder
ctest --test-dir build/Msvc-Release -R '^quantapdf\.jpeg_encoder$' --output-on-failure
```

Expected: configure and build succeed, then `quantapdf.jpeg_encoder` fails
because the scaffolded `jpeg_encoder::create()` returns BACKEND.

- [ ] **Step 3: Implement the minimal fixed encoder**

In `jpeg_encoder.cpp`, validate `next` and `out` first, clear `*out`, use
checked `width * height * components`, require components 1 or 3 and quality
1..100, and map observable allocation failure to `QUANTAPDF_ERROR_NOMEM`.
Configure `Pl_DCT` exactly:

```cpp
auto config = Pl_DCT::make_compress_config(
    [spec](jpeg_compress_struct* cinfo) {
        jpeg_set_colorspace(
            cinfo, spec.components == 1 ? JCS_GRAYSCALE : JCS_RGB);
        jpeg_set_quality(cinfo, spec.quality, TRUE);
        cinfo->dct_method = JDCT_ISLOW;
        cinfo->optimize_coding = FALSE;
        cinfo->arith_code = FALSE;
        cinfo->smoothing_factor = 0;
        cinfo->restart_interval = 0;
        cinfo->restart_in_rows = 0;
        cinfo->scan_info = nullptr;
        cinfo->num_scans = 0;
        if (spec.components == 1) {
            cinfo->write_JFIF_header = TRUE;
            cinfo->JFIF_major_version = 1;
            cinfo->JFIF_minor_version = 1;
            cinfo->density_unit = 0;
            cinfo->X_density = 1;
            cinfo->Y_density = 1;
            cinfo->write_Adobe_marker = FALSE;
        } else {
            cinfo->write_JFIF_header = FALSE;
            cinfo->write_Adobe_marker = TRUE;
            for (int i = 0; i < 3; ++i) {
                cinfo->comp_info[i].h_samp_factor = 1;
                cinfo->comp_info[i].v_samp_factor = 1;
            }
        }
    });
```

Construct the owned configuration before the owned `Pl_DCT` and return its
pipeline. The test writes fixed samples through it into `Pl_Buffer`; the
production provider instead pipes decoded qpdf bytes directly into it. Catch
`std::bad_alloc`, `QPDFExc`, and other exceptions at this private factory
boundary, with only `std::bad_alloc` mapping to NOMEM and codec/runtime errors
mapping to BACKEND.

- [ ] **Step 4: Capture Windows bytes and make the local test GREEN**

Temporarily print each complete byte vector as a C++ initializer, copy those
six initializers into named `static const std::vector<unsigned char>` golden
values, remove the print path, rebuild, and run:

```powershell
ctest --test-dir build/Msvc-Release -R '^quantapdf\.jpeg_encoder$' --output-on-failure
```

Expected: 1/1 test passes and every repeated result equals its complete golden
byte array.

- [ ] **Step 5: Verify adapter errors and byte relationships**

Add factory cases for null downstream pipeline, null output holder, zero
dimensions, overflowing dimensions, components 2/4, and qualities 0/101.
Require `QUANTAPDF_ERROR_ARGUMENT` for invalid factory shape and
`QUANTAPDF_ERROR_FORMAT` for checked image-size overflow. Exercise short and
long sample writes and require a caught codec/runtime failure rather than
memory over-read or partial publication. Record only the controlled-fixture
facts that quality 40 and 90 bytes differ and quality 100 still emits JPEG.

Run:

```powershell
ctest --preset win-release-user --output-on-failure
```

Expected: 32/32 CTests pass.

- [ ] **Step 6: Document the direct codec boundary and notice**

Add a `libjpeg-turbo` section to `THIRD_PARTY.md` stating version 3.2.0,
pinned-vcpkg provenance, IJG/BSD-style license, that QuantaPDF calls it through
qpdf `Pl_DCT`, and that binary redistribution installs its copyright. Do not
claim cross-platform deterministic bytes until Step 8 passes.

- [ ] **Step 7: Commit the private qualification slice**

```powershell
git add src/backend/jpeg_encoder.h src/backend/jpeg_encoder.cpp tests/test_jpeg_encoder.cpp tests/CMakeLists.txt THIRD_PARTY.md
git commit -m "test: qualify deterministic JPEG encoding"
```

- [ ] **Step 8: Review, push, and run exact-head full CI**

Request task and whole-branch review, push a qualification PR with `full-ci`,
and require the exact commit to pass Linux static, Linux ASan/UBSan, macOS,
and Windows. The hardcoded complete JPEG arrays make any platform difference
fail `quantapdf.jpeg_encoder`.

Expected: all jobs pass on one SHA. If any golden-byte comparison differs,
close the public implementation gate, keep the public header unchanged, and
revise the design with the observed byte diffs.

- [ ] **Step 9: Merge the qualification PR and verify master CI**

Merge only after exact-head full CI, then require the merge commit's Linux,
macOS, and Windows jobs to pass. Start Task 2 from that integrated master
commit; do not stack the public ABI on an unmerged qualification branch.

## Phase B: public transform after the encoder gate is green

### Task 2: Append-only C ABI and failure-atomic facade

**Files:**
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/pdf_image_recompression.c`
- Modify: `src/internal.h`
- Modify: `src/backend/qpdf_document.h`
- Modify: `CMakeLists.txt`
- Modify: `abi/quantapdf-v2.exports`
- Modify: `tests/test_version.c`
- Create: `tests/test_pdf_image_recompression.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the exact public declarations in the design spec and the private
  encoder qualified by Task 1.
- Produces:

```c
quantapdf_status quantapdf_qpdf_recompress_images(
    quantapdf_qpdf_document *document,
    int jpeg_quality,
    size_t max_decoded_bytes_per_image,
    unsigned char **out_data,
    size_t *out_size);
```

- [ ] **Step 1: Write compile-red public contract tests**

Create `test_pdf_image_recompression.c`. Compile references to
`quantapdf_image_recompression_options`, both size macros, the 64 MiB default,
and `quantapdf_recompress_images`. Add runtime assertions that null document,
null options, null output, undersized struct, and qualities 0/101 return
`QUANTAPDF_ERROR_ARGUMENT`; use a non-NULL sentinel and assert every reachable
failure sets output to NULL.

- [ ] **Step 2: Register the public target and verify compile RED**

```cmake
add_executable(quantapdf_test_pdf_image_recompression
  test_pdf_image_recompression.c)
target_link_libraries(quantapdf_test_pdf_image_recompression PRIVATE
  QuantaPDF::QuantaPDF)
add_test(NAME quantapdf.pdf_image_recompression
  COMMAND quantapdf_test_pdf_image_recompression)
set_tests_properties(quantapdf.pdf_image_recompression PROPERTIES TIMEOUT 60)
```

Run the target build. Expected: compilation fails on the missing public type,
macros, and function.

- [ ] **Step 3: Append the header and facade**

Append the exact spec declarations to the sole public header. Implement the C
facade with the established output-shell pattern:

```c
if (out_output == NULL)
    return QUANTAPDF_ERROR_ARGUMENT;
*out_output = NULL;
if (document == NULL || document->qpdf_document == NULL || options == NULL)
    return QUANTAPDF_ERROR_ARGUMENT;
```

Read only fields covered by `struct_size`, normalize a missing/zero cap to
`QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES`, allocate one zeroed
`quantapdf_output`, call the backend, free shell/data on failure, and publish
only on success.

- [ ] **Step 4: Add a temporary backend stub and make contract tests GREEN**

Declare the private bridge and return `QUANTAPDF_ERROR_UNSUPPORTED` from a
temporary backend body after validating/zeroing its raw outputs. Add the C
source and the already qualified encoder source to the production target.
Update the version to 2.4.0, keep ABI/SOVERSION 2, append the one export, and
update exact version/export assertions.

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: 33/33 CTests pass; the new public test passes argument cases and
expects UNSUPPORTED only for valid calls until Task 3 replaces the stub.

- [ ] **Step 5: Commit the ABI slice**

```powershell
git add include/quantapdf/quantapdf.h src/pdf_image_recompression.c src/internal.h src/backend/qpdf_document.h src/backend/qpdf_document.cpp CMakeLists.txt abi/quantapdf-v2.exports tests/test_version.c tests/test_pdf_image_recompression.c tests/CMakeLists.txt
git commit -m "feat: add image recompression ABI"
```

### Task 3: Bounded graph discovery, eligibility, and positive transform

**Files:**
- Modify: `src/backend/qpdf_document.cpp`
- Create: `tests/image_recompression_test_helpers.h`
- Create: `tests/image_recompression_test_helpers.cpp`
- Modify: `tests/test_pdf_image_recompression.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: normalized quality/cap bridge and the private streaming
  `jpeg_encoder`.
- Produces: the complete successful implementation of
  `quantapdf_qpdf_recompress_images()` for eligible images.

- [ ] **Step 1: Generate positive fixtures and write runtime RED tests**

In the C++ helper, generate one PDF containing:

- an opaque 8-bit DeviceRGB image used on two pages;
- an opaque 8-bit DeviceGray image;
- the RGB image referenced through a nested Form;
- eligible images below both annotation and Widget appearances, covering
  `/AP /N`, `/R`, and `/D` as direct streams and as appearance-state
  subdictionaries, with a nested Form below every variant.

Create the helper header/source, then add them to the target with
`target_sources`, link the test privately to `qpdf::libqpdf`, and enable
C++20. Expose C helper functions that count unique image `QPDFObjGen`
identities, return raw Filter/DecodeParms/Width/Height facts, and count
resource occurrences. Add test-only counters keyed by `QPDFObjGen` for
provider registration and invocation. In the C test assert the valid transform
returns OK, output reopens, all eligible unique identities use DCTDecode, the
shared object is still unique, and both of its counters equal one. Expected
before implementation: the stub returns UNSUPPORTED.

- [ ] **Step 2: Implement strict fresh-graph and security preflight**

Mirror the lossless/sanitize strict open sequence: create a new QPDF, suppress
warnings, disable recovery, process the original bytes/password, force pages
and objects, reject warnings, run the existing audit, and reject signature or
encryption findings before discovery.

- [ ] **Step 3: Implement iterative reachable-resource discovery**

Reuse `quantapdf_qpdf_work_budget` and its overflow-checked initializer. Seed
every page effective resource owner plus appearance streams from strict
`/Annots` and `/AP` traversal. Maintain `std::set<QPDFObjGen>` for resource
owners and images. For every `/Resources /XObject` entry, collect Image streams
and queue Form streams. Return FORMAT for consumed wrong container/stream
types, and UNSUPPORTED when the budget is exhausted.

- [ ] **Step 4: Implement exact eligibility and decode preflight**

For each unique image, validate keys in the spec's order, use checked
multiplication for expected bytes, preserve valid ineligible classes, and run
qpdf's null-pipeline filterability probe at `qpdf_dl_all`. Treat a correctly
sized finite numeric non-identity `/Decode` array as valid/ineligible; treat
wrong type/length, nonnumeric, or non-finite entries as FORMAT. A false filter
probe is a preserve decision. For a true probe, use `pipeStreamData` into a
counting pipeline; only plan the image when decode succeeds and the count
equals the expected bytes. Candidate decode failure or a new qpdf warning is
FORMAT.

- [ ] **Step 5: Register providers and rewrite each identity once**

Before replacement, store `image.copyStream()` keyed by `QPDFObjGen`. Register
one retry-capable provider that constructs the fixed encoder over the supplied
writer pipeline and calls `copy.pipeStreamData(encoder->pipeline(), ..., qpdf_dl_all)`;
do not call `getStreamData` or materialize a QuantaPDF raw/encoded vector.
The source pipeline supplies exactly one downstream `finish()`, which finishes
`Pl_DCT`; the provider must not finish the encoder a second time.
Each provider has a first-error `quantapdf_status` latch: `std::bad_alloc`
latches NOMEM, an explicit private status latches itself, and false
`pipeStreamData` or any other provider/codec exception latches BACKEND before
aborting the writer. The outer writer catch returns the latch when non-OK.
Install `/DCTDecode`; install `/ColorTransform 0` only for RGB; call
`setFilterOnWrite(false)`. Preserve all non-filter image dictionary keys.

- [ ] **Step 6: Serialize and verify positive GREEN**

Use the existing deterministic memory writer. After write, reject new warnings
and publish only the complete buffer. Extend tests to render controlled source
and output pages with PDFium, require unchanged dimensions/quads/occurrence
counts, and compare per-channel absolute error against fixture-specific bounds.
Capture before/after snapshots of text, search results, links, annotations,
forms, outline, metadata, destinations, and page boxes. Compare raw bytes for
every page and Form content stream exactly; only eligible image stream bytes
may change.

Run:

```powershell
ctest --test-dir build/Msvc-Release -R '^quantapdf\.(jpeg_encoder|pdf_image_recompression)$' --output-on-failure
```

Expected: 2/2 selected tests pass.

- [ ] **Step 7: Add determinism, lifetime, and quality cases**

Call the transform twice at quality 90 and require byte equality. Close the
source before reading/reopening output. Require quality 40 and 90 image stream
bytes to differ and record controlled fixture size/error ordering. Require
quality 100 to replace the eligible stream rather than copy its original
bytes. Save the reviewed quality-90 controlled output as
`tests/fixtures/image-recompression-expected-q90.pdf`, then compare every test
run's complete output bytes with that file so Linux, macOS, and Windows prove
the same serialization.

- [ ] **Step 8: Commit the successful transform**

```powershell
git add src/backend/qpdf_document.cpp tests/image_recompression_test_helpers.h tests/image_recompression_test_helpers.cpp tests/test_pdf_image_recompression.c tests/CMakeLists.txt
git commit -m "feat: recompress eligible PDF images"
```

### Task 4: Preservation, limits, malformed input, and fault atomicity

**Files:**
- Modify: `src/backend/qpdf_document.cpp`
- Modify: `tests/image_recompression_test_helpers.cpp`
- Modify: `tests/test_pdf_image_recompression.c`
- Create: `tests/pdf_image_recompression_fault_hook.c`
- Create: `tests/pdf_image_recompression_test_api.h`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 discovery/eligibility/provider flow.
- Produces: complete fail-closed and preservation matrix.

- [ ] **Step 1: Write RED preservation and cap-boundary tests**

Generate valid images covering CMYK, ICCBased, Indexed, soft mask, explicit
mask, color-key mask, stencil, 1/16-bpc, non-identity Decode, unsupported
filter, inline content, and an XObject/Form cycle. Snapshot each raw ineligible
stream before and after. For a 2x1 RGB image, require cap 5 to preserve and cap
6 to rewrite. Expected: any missing classifier or cap rule fails.

- [ ] **Step 2: Make the preservation matrix GREEN**

Refine classification so valid unsupported classes return a preserve decision
without decoding or mutation. Ensure the image and owner identity sets stop
cycles and that classification order handles ImageMask before ColorSpace/BPC.

- [ ] **Step 3: Write RED malformed and security tests**

Generate malformed resources, XObject containers, appearance state values,
width/height/BPC/ImageMask/Decode scalars, wrong-length/nonnumeric/non-finite
Decode arrays, overflowing image dimensions/products, and corrupt eligible
stream data. Separately force source-derived traversal-budget overflow and
exhaustion. Reuse the repository's signed and encrypted fixtures. Require
exact FORMAT or UNSUPPORTED outcomes and NULL output.

- [ ] **Step 4: Make malformed/security behavior GREEN**

Map consumed structural violations, image dimension/product overflow, and
candidate decode failure to FORMAT. Map only source-derived traversal-budget
multiplication overflow/exhaustion to UNSUPPORTED; an ordinary image above the
per-image cap remains valid/ineligible and is preserved. Reuse audit
signature/encryption findings. Check `pdf->anyWarnings()` after preflight and
write.

- [ ] **Step 5: Add deterministic fault injection**

Follow existing transform hooks: the test-only C hook exposes integer fault
points immediately before provider registration, during provider encode, and
before writer-buffer publication. Assert the provider's first-status latch is
returned across the QPDFWriter exception boundary. Every injected failure must
leave source observations unchanged and output NULL; only observable C/C++
allocation failures return NOMEM, while simulated codec/provider runtime and
backend invariant failures return BACKEND.

- [ ] **Step 6: Run sanitizers and commit**

```powershell
ctest --preset win-release-user --output-on-failure
```

Then on Linux:

```bash
cmake --fresh --preset linux-dev-user
cmake --build --preset linux-dev-user --parallel 2
ctest --preset linux-dev-user --output-on-failure
```

Expected: 33/33 tests pass in Release and sanitizer configurations, with no
ASan/UBSan report.

```powershell
git add src/backend/qpdf_document.cpp tests/test_pdf_image_recompression.c tests/image_recompression_test_helpers.cpp tests/pdf_image_recompression_fault_hook.c tests/pdf_image_recompression_test_api.h tests/CMakeLists.txt
git commit -m "test: harden image recompression policy"
```

### Task 5: Documentation, install notice, ABI check, and release gate

**Files:**
- Modify: `README.md`
- Modify: `THIRD_PARTY.md`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-09-01-quantapdf-image-recompression-design.md`
- Modify: `docs/superpowers/plans/2026-09-01-quantapdf-image-recompression.md`

**Interfaces:**
- Consumes: the complete API and backend behavior from Tasks 1-4.
- Produces: documented/installable QuantaPDF 2.4.0 candidate with exact
  completion evidence.

- [ ] **Step 1: Document the public call and limits**

Add a README example initializing the full options structure, calling the
transform, saving/dropping output, and explaining explicit loss, strict
eligibility, 64 MiB default cap, quality 100 behavior, immutable ownership,
signed/encrypted failures, and the absence of resizing/inline-image rewriting.

- [ ] **Step 2: Install the libjpeg-turbo notice**

Derive the package share root as the parent of `qpdf_DIR`, require
`${share_root}/libjpeg-turbo/copyright`, and install it under
`${CMAKE_INSTALL_DATADIR}/quantapdf/licenses/libjpeg-turbo`. Keep qpdf and
PDFium notice installation unchanged.

- [ ] **Step 3: Verify the public C and export surfaces**

Keep the public transform test as C, compile its options initializer against
the sole public header, verify the 2.4.0/ABI 2 macros, and extend the existing
ABI checker to require exactly the new symbol. Do not claim a relocatable
CMake package or standalone static installed-consumer test: the project does
not yet ship package configuration for its private static dependencies, and
that packaging work is separate from #57.

- [ ] **Step 4: Run the complete local release gate**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --install build/Msvc-Release --prefix build/install-image-recompression
```

Expected: 33/33 CTests, version 2.4.0, ABI/SOVERSION 2, exactly 88 exports,
the public C target linked successfully, and all three dependency notice trees
present.

- [ ] **Step 5: Update completion evidence and commit**

Mark only locally proven checklist items complete and record exact command
outputs/commit SHAs. Do not claim PR, full CI, merge, or release before those
events exist.

```powershell
git add README.md THIRD_PARTY.md CMakeLists.txt tests/CMakeLists.txt docs/superpowers/specs/2026-09-01-quantapdf-image-recompression-design.md docs/superpowers/plans/2026-09-01-quantapdf-image-recompression.md
git commit -m "docs: document image recompression contract"
```

- [ ] **Step 6: Request task and whole-branch review**

Review the full Phase B range against the spec. Fix all Critical and Important
findings through the original task implementer and re-review each fix. Require
a final whole-branch verdict of ready to merge.

- [ ] **Step 7: Push the feature PR and require full CI**

Push without force, open a PR that links #57, add `full-ci`, and verify the PR
head SHA equals the reviewed local SHA. Require Linux static, Linux ASan/UBSan,
macOS, and Windows to pass, including both new CTests and the 88-export check.

- [ ] **Step 8: Stop before integration**

Report the exact PR URL, reviewed head SHA, local 33/33 evidence, and exact-head
full-ci run. Do not merge the Phase B feature PR until the user explicitly
authorizes integration.
