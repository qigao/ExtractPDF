# ExtractPDF AcroForm Snapshot V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a strict, immutable, document-independent AcroForm snapshot that exposes logical field groups, typed values/options, and reconciled page Widget instances without mutating the source PDF or executing PDF behavior.

**Architecture:** Parse raw AcroForm objects into a private deep-owned semantic model in `pdf_form_common.c`, after a strict Parent/Kids/group preflight and page-Widget reconciliation. `pdf_form.c` owns only public snapshot publication/accessors. Reuse the existing Fitz page-space geometry and raw full-range uint32 conventions, but do not use MuPDF form getters that can repair or write back source state.

**Tech Stack:** C11, MuPDF 1.28.2 through pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, CMake 3.20+, CTest, Linux `-Wall -Wextra -Wpedantic -Werror`, Linux ASan/UBSan, macOS, Windows shared-library build with MSVC `/W4 /WX`.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-snapshot-design.md`

## Global Constraints

- Base integrated master is exactly `0e4b769753215725797a557c4f18c4654e444e30`; design head before implementation planning is `d586aceb3837ba8169af67470fd81ffb4a75a867`.
- Issue scope is #43 only. Do not create or implement Form Value Mutation V1, field refs, setters, field/Widget create/delete/rename, XFA, signing, JavaScript/events, NeedAppearances rewriting, or flattening.
- Public logical objects are **field groups**. `/T` creates a logical-name boundary; unnamed descendants remain in the nearest logical group.
- Public `field_index`, `widget_index`, and field-local `option_index` are snapshot-local coordinates only.
- A valid PDF with no `/AcroForm`, no `/Fields`, or empty `/Fields` returns `OK` plus a non-NULL empty snapshot.
- Non-PDF input returns `EXTRACTPDF_ERROR_UNSUPPORTED` and resets `*out_form = NULL`.
- Publication is atomic. Any late structural/value/Widget failure returns no snapshot and exposes no valid prefix.
- Snapshot extraction must not repair Parent/Kids, normalize/write `/V`, dirty fields, synthesize/update appearances, calculate forms, or execute JavaScript/actions/events.
- Do not define immutable semantics with `pdf_field_value()` or repairing inheritable-parent walkers. Parse raw objects against the validated private Parent graph.
- Widget page placement is authoritative from page `/Annots`: page order ascending, then raw `/Annots` relative order. Missing/non-array `/Annots` is empty; non-dictionary entries are ignored.
- Every published Widget is reachable from exactly one terminal logical field group and appears exactly once on exactly one page. Direct Widgets cannot provide cross-structure identity and are `FORMAT`.
- Widget `/Rect` uses the existing PDF-user-space -> Fitz page-space transform. Widget `/F` and field `/Ff` preserve the complete non-negative `uint32_t` range without signed narrowing.
- All public strings are deep-owned by `extractpdf_form`; accessors return borrowed pointers until `extractpdf_drop_form()`.
- String tri-state is exact: missing `NULL + 0`; present-empty non-NULL + `0`; present-value non-NULL + byte count.
- Public structs use the existing minimum-known-`struct_size` rule: too small `ARGUMENT`; larger accepted; only known fields are reset/written; trailing caller bytes remain untouched.
- Target suite progression is exactly 19 -> 20 CTests.
- Final feature proof requires exact-head Linux static 20/20, Linux ASan/UBSan 20/20, then same-SHA `full-ci` macOS 20/20 and Windows DLL 20/20 before explicit integration authorization.
- Do not merge or close #43 before explicit integration authorization and integrated-master push proof.

---

## File Structure

**Create**

- `src/pdf_object_common.h` — shared raw PDF dictionary lookup, strict rectangle-to-Fitz conversion, and strict optional uint32 reader used by annotations and Forms.
- `src/pdf_object_common.c` — implementation of those source-observation helpers only; no form semantics.
- `src/pdf_form_common.h` — private deep-owned form semantic-model types and parser/drop entry points.
- `src/pdf_form_common.c` — AcroForm root validation, Parent/Kids graph, `/T` field grouping, validated inheritance, choice/button/value normalization, page Widget reconciliation, and deep model materialization.
- `src/pdf_form.c` — `extractpdf_form` wrapper/public publication and all public accessors/drop.
- `tests/test_pdf_form.c` — the single twentieth CTest, with a `--case <name>` selector so each implementation task can prove only the contract it just made GREEN while the full no-argument invocation stays RED until the final task.
- Deterministic checked-in fixtures listed in Task 1 under `tests/fixtures/acroform-*.pdf`.

**Modify**

- `include/extractpdf/extractpdf.h` — approved public `extractpdf_form` ABI only.
- `src/pdf_annotation_common.c` / `src/pdf_annotation_common.h` — use the shared raw Rect/F helpers without changing annotation semantics.
- `CMakeLists.txt` — compile the two shared/form production units after their tasks introduce them.
- `tests/CMakeLists.txt` — register fixture macros, `extractpdf_test_pdf_form`, CTest name `extractpdf.pdf_form`, timeout, and Windows DLL post-build copy.

**Do not modify**

- `.github/workflows/ci.yml` — current workflow already provides Linux static+sanitizer on PR and macOS/Windows on `full-ci` label/push.
- annotation/editor public semantics.
- `src/pdf_edit*` — read-only snapshot slice only.

---

### Task 1: Define the complete strict RED surface

**Files:**
- Create: `tests/test_pdf_form.c`
- Create: deterministic `tests/fixtures/acroform-*.pdf` files below
- Modify: `tests/CMakeLists.txt`
- Do **not** modify: `include/extractpdf/extractpdf.h`, `src/*.c`, top-level `CMakeLists.txt`

**Interfaces:**
- Consumes: current public API at base master; none of the approved Forms API exists yet.
- Produces: one wished-for C test target containing the entire approved public ABI usage and deterministic fixtures. It must fail at compile time because `extractpdf_form` and the Forms API are absent.

- [ ] **Step 1: Generate the deterministic fixture family outside the runtime test path**

Use a one-shot local Python helper (for example `/tmp/gen_acroform_fixtures.py`) to write classic xref-table PDFs. Do not commit the generator and do not make Python a test/runtime dependency. The helper must calculate byte offsets rather than hand-writing xref offsets:

```python
from pathlib import Path


def pdf_bytes(objects, root=1, version="1.7"):
    out = bytearray(f"%PDF-{version}\n% deterministic-acroform\n".encode("ascii"))
    offsets = [0] * (max(objects) + 1)
    for number in sorted(objects):
        offsets[number] = len(out)
        out += f"{number} 0 obj\n".encode("ascii")
        body = objects[number]
        if isinstance(body, str):
            body = body.encode("latin-1")
        out += body
        if not body.endswith(b"\n"):
            out += b"\n"
        out += b"endobj\n"
    xref = len(out)
    out += f"xref\n0 {len(offsets)}\n".encode("ascii")
    out += b"0000000000 65535 f \n"
    for number in range(1, len(offsets)):
        if offsets[number]:
            out += f"{offsets[number]:010d} 00000 n \n".encode("ascii")
        else:
            out += b"0000000000 00000 f \n"
    out += (
        f"trailer\n<< /Size {len(offsets)} /Root {root} 0 R >>\n"
        f"startxref\n{xref}\n%%EOF\n"
    ).encode("ascii")
    return bytes(out)


def write(name, objects):
    Path("tests/fixtures", name).write_bytes(pdf_bytes(objects))
```

All valid fixture pages use `/MediaBox [0 0 200 200]` and no rotation so a Widget Rect `[x0 y0 x1 y1]` has predictable Fitz bounds `[x0, 200-y1, x1, 200-y0]` after the existing page transform.

Generate these **valid** fixtures:

1. `acroform-main.pdf` — three pages, seventeen public terminal logical field groups in this exact DFS order:

```text
0  profile.nickname   TEXT          inherits /FT /Ff /V /TU from named parent `profile`; value "Ada"
1  repeat             TEXT          one named group containing unnamed appearance-only field children; value "same"; 2 Widgets
2  <missing name>     TEXT          anonymous top-level group; value "anon"
3  <present-empty>    TEXT          local /T (); local /V ()
4  textMissing        TEXT          no effective /V
5  textEmpty          TEXT          /V ()
6  textValue          TEXT          /V (hello); /TU () present-empty; 1 Widget
7  agree              CHECKBOX      /V /Yes; two repeated Widgets, each /AP/N has exactly /Off and /Yes
8  payment            RADIO_BUTTON  /Ff 32768; /V /Mastercard; Widgets Visa, Mastercard, Visa
9  country            COMBO_BOX     /Ff 131072; /Opt [[(US)(United States)] [(JP)(Japan)]]; /V (JP); /I [1]
10 city               COMBO_BOX     /Ff 393216 (Combo|Edit); /Opt [(Tokyo) (Osaka)]; custom /V (Kyoto)
11 size               LIST_BOX      /Opt [(S) (M) (L)]; /V (M); /I [1]
12 colors             LIST_BOX      /Ff 2097152 (MultiSelect); /Opt [[(r)(Red)] [(g)(Green)] [(b)(Blue)]]; /V [(r)(b)]; /I [0 2]
13 submit             PUSH_BUTTON   /Ff 65536; ordinary value NOT_APPLICABLE
14 sigUnsigned        SIGNATURE     no /V; is_signed=0
15 sigSigned          SIGNATURE     /V << /Type /Sig >>; is_signed=1
16 future             UNKNOWN       /FT /Future; ordinary value NOT_APPLICABLE
```

Use a parent group `/T (profile) /FT /Tx /Ff 0 /V (Ada) /TU (Profile label)` with named child `/T (nickname)` and no Widgets; only `profile.nickname` is public. For `repeat`, use `/T (repeat) /FT /Tx /V (same)` with two child field dictionaries that have no `/T`; each unnamed child owns one indirect Widget through `/Kids`. This proves unnamed child nodes stay in one logical field group.

Use these Widget occurrences and raw page `/Annots` order; include at least one scalar and one non-Widget annotation in page arrays to prove they are ignored:

```text
page 0:
  repeat-A        Rect [10 170 40 190]   -> Fitz [10 10 40 30]
  agree-A         Rect [50 170 70 190]   -> Fitz [50 10 70 30]
  payment-Visa-A  Rect [80 170 100 190]  -> Fitz [80 10 100 30]
  country         Rect [10 130 80 150]   -> Fitz [10 50 80 70]
  submit          Rect [90 130 150 150]  -> Fitz [90 50 150 70]

page 1:
  repeat-B        Rect [10 170 40 190]
  textValue       Rect [50 170 100 190]
  agree-B         Rect [110 170 130 190], /F 2147483649
  payment-Master  Rect [140 170 160 190]
  city            Rect [10 130 80 150]
  sigUnsigned     Rect [90 130 150 150]

page 2:
  payment-Visa-B  Rect [10 170 30 190]
  size            Rect [40 170 80 190]
  colors          Rect [90 170 140 190]
  sigSigned       Rect [10 130 70 150]
  future          Rect [80 130 140 150]
```

Checkbox/radio normal appearances are state dictionaries containing an `/Off` stream reference and exactly one non-Off state reference per Widget. Repeated `agree` Widgets both use `/Yes`, so field option_count is 1. Payment option discovery in global Widget order is Visa then Mastercard; the later Visa Widget deduplicates to option 0. Therefore `payment` value is OPTION(1).

2. `acroform-no-fields.pdf` — valid AcroForm dictionary without `/Fields`.
3. `acroform-empty-fields.pdf` — `/Fields []`.
4. `acroform-annots-nonarray.pdf` — one valid terminal Text field with zero Widgets and a page whose `/Annots` is scalar `17`; snapshot succeeds with one field and zero Widgets.
5. `acroform-js.pdf` — Text fields `marker`=`SAFE` and `dependent`=`UNCHANGED`, plus document OpenAction JavaScript and field `/AA` Keystroke/Validate/Calculate/Format JavaScript that would change those strings if executed. Snapshot must report original values on repeated extraction; no event execution.
6. `acroform-stream-value.pdf` — Text `/V` is an indirect stream. Two consecutive extraction attempts must both return FORMAT; if a convenience getter normalized/wrote the value, the second attempt would incorrectly change behavior.

Generate these **strict failure** fixtures; each contains the smallest structure needed to isolate one boundary:

```text
acroform-bad-root.pdf           Root/AcroForm is integer 17                         -> FORMAT
acroform-bad-fields.pdf         AcroForm /Fields is integer 17                      -> FORMAT
acroform-bad-kid.pdf            field /Kids contains scalar 17                      -> FORMAT
acroform-cycle.pdf              A /Kids [B], B /Kids [A] with matching Parents      -> FORMAT
acroform-repeated-node.pdf      one field object is reached twice                    -> FORMAT
acroform-parent-mismatch.pdf    A /Kids [B], but B /Parent references C              -> FORMAT
acroform-root-parent.pdf        top-level field has non-null external /Parent         -> FORMAT
acroform-depth-257.pdf          otherwise-valid 257-node Parent/Kids chain            -> UNSUPPORTED
acroform-mixed-group.pdf        one logical group owns Widget(s) and named child group-> FORMAT
acroform-group-conflict.pdf     unnamed same-group child overrides effective /V       -> FORMAT
acroform-duplicate-name.pdf     two distinct public fields resolve to same name        -> FORMAT
acroform-period-name.pdf        partial /T contains literal '.'                       -> FORMAT
acroform-bad-ft.pdf             terminal effective /FT is a string                    -> FORMAT
acroform-bad-ff.pdf             effective /Ff is 4294967296                           -> FORMAT
acroform-bad-value.pdf          Text /V is integer 17                                 -> FORMAT
acroform-bad-opt.pdf            Choice /Opt contains non-string/non-pair entry         -> FORMAT
acroform-bad-i.pdf              /I [1] contradicts /V selecting option 0              -> FORMAT
acroform-bad-button-ap.pdf      button Widget /AP/N has /Off plus two non-Off states  -> FORMAT
acroform-orphan-widget.pdf      page /Annots Widget is not reachable from Fields       -> FORMAT
acroform-missing-widget.pdf     field-tree Widget never occurs in page /Annots         -> FORMAT
acroform-duplicate-widget.pdf   same Widget ref occurs twice in one /Annots            -> FORMAT
acroform-p-mismatch.pdf         Widget occurs on page 0 but /P references page 1       -> FORMAT
acroform-direct-widget.pdf      cross-reconciled Widget is a direct dictionary         -> FORMAT
acroform-bad-widget-rect.pdf    Widget /Rect has three values                          -> FORMAT
acroform-bad-widget-flags.pdf   Widget /F is -1 or >UINT32_MAX                         -> FORMAT
acroform-bad-signature.pdf      Signature /V dict has /Type /NotSig                    -> FORMAT
acroform-late-malformed.pdf     first field valid Text; later field has malformed /Ff   -> FORMAT, no prefix
```

For malformed fixtures that need pages/Widgets, keep the unrelated objects valid so the named defect is the first semantic failure.

- [ ] **Step 2: Write the wished-for test executable against the approved ABI**

`tests/test_pdf_form.c` must include `<extractpdf/extractpdf.h>` only for public API access and define the same `CHECK` style as existing tests. Add a case dispatcher:

```c
static void run_case(const char *name)
{
    if (strcmp(name, "api") == 0) test_api_contract();
    else if (strcmp(name, "empty") == 0) test_empty_and_non_pdf();
    else if (strcmp(name, "structure") == 0) test_field_groups_and_structure();
    else if (strcmp(name, "widgets") == 0) test_widgets();
    else if (strcmp(name, "scalar-values") == 0) test_text_signature_unknown();
    else if (strcmp(name, "choice-values") == 0) test_choice_values();
    else if (strcmp(name, "button-values") == 0) test_button_values();
    else if (strcmp(name, "lifetime") == 0) test_lifetime_atomicity_no_execution();
    else CHECK(0);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        run_case(argv[2]);
        return EXIT_SUCCESS;
    }
    CHECK(argc == 1);
    test_api_contract();
    test_empty_and_non_pdf();
    test_field_groups_and_structure();
    test_widgets();
    test_text_signature_unknown();
    test_choice_values();
    test_button_values();
    test_lifetime_atomicity_no_execution();
    return EXIT_SUCCESS;
}
```

The file must compile-reference **every** approved enum, info struct, accessor, `extractpdf_document_form()`, and `extractpdf_drop_form()` from the spec. Include helpers that assert missing vs present-empty strings by checking both pointer and size.

`test_api_contract()` must lock output reset, index errors, minimum/larger `struct_size`, and accessor-kind errors. For larger structs use a wrapper with a trailing canary and pass `info.struct_size = sizeof(wrapper)`; assert the canary is unchanged. For too-small structs use `offsetof(type, last_known_field)` so the failure is exactly one field short.

- [ ] **Step 3: Register the twentieth target without production changes**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(extractpdf_test_pdf_form test_pdf_form.c)
target_link_libraries(extractpdf_test_pdf_form PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_form PRIVATE
  ACROFORM_MAIN_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-main.pdf"
  ACROFORM_NO_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-no-fields.pdf"
  ACROFORM_EMPTY_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-empty-fields.pdf"
  ACROFORM_ANNOTS_NONARRAY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-annots-nonarray.pdf"
  ACROFORM_JS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-js.pdf"
  ACROFORM_STREAM_VALUE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-stream-value.pdf"
  ACROFORM_BAD_ROOT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-root.pdf"
  ACROFORM_BAD_FIELDS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-fields.pdf"
  ACROFORM_BAD_KID_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-kid.pdf"
  ACROFORM_CYCLE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-cycle.pdf"
  ACROFORM_REPEATED_NODE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-repeated-node.pdf"
  ACROFORM_PARENT_MISMATCH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-parent-mismatch.pdf"
  ACROFORM_ROOT_PARENT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-root-parent.pdf"
  ACROFORM_DEPTH_257_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-depth-257.pdf"
  ACROFORM_MIXED_GROUP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-mixed-group.pdf"
  ACROFORM_GROUP_CONFLICT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-group-conflict.pdf"
  ACROFORM_DUPLICATE_NAME_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-duplicate-name.pdf"
  ACROFORM_PERIOD_NAME_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-period-name.pdf"
  ACROFORM_BAD_FT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-ft.pdf"
  ACROFORM_BAD_FF_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-ff.pdf"
  ACROFORM_BAD_VALUE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-value.pdf"
  ACROFORM_BAD_OPT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-opt.pdf"
  ACROFORM_BAD_I_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-i.pdf"
  ACROFORM_BAD_BUTTON_AP_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-button-ap.pdf"
  ACROFORM_ORPHAN_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-orphan-widget.pdf"
  ACROFORM_MISSING_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-missing-widget.pdf"
  ACROFORM_DUPLICATE_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-duplicate-widget.pdf"
  ACROFORM_P_MISMATCH_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-p-mismatch.pdf"
  ACROFORM_DIRECT_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-direct-widget.pdf"
  ACROFORM_BAD_WIDGET_RECT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-widget-rect.pdf"
  ACROFORM_BAD_WIDGET_FLAGS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-widget-flags.pdf"
  ACROFORM_BAD_SIGNATURE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-bad-signature.pdf"
  ACROFORM_LATE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/acroform-late-malformed.pdf"
  NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  NO_ACROFORM_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf")
add_test(NAME extractpdf.pdf_form COMMAND extractpdf_test_pdf_form)
set_tests_properties(extractpdf.pdf_form PROPERTIES TIMEOUT 60)
```

Add `extractpdf_test_pdf_form` to the existing Windows shared-library `foreach(test_target ...)` list.

- [ ] **Step 4: Prove the RED is the approved missing-ABI boundary**

Configure with the exact Linux CI shape:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
```

First build the existing 19 targets explicitly; every command must exit 0:

```bash
cmake --build build --parallel 2 --target \
  extractpdf_test_status extractpdf_test_document extractpdf_test_render \
  extractpdf_test_text extractpdf_test_structured_text extractpdf_test_text_search \
  extractpdf_test_images extractpdf_test_image_bitmap extractpdf_test_links \
  extractpdf_test_pdf_export extractpdf_test_pdf_range extractpdf_test_pdf_order \
  extractpdf_test_pdf_delete extractpdf_test_pdf_merge extractpdf_test_output_file \
  extractpdf_test_pdf_metadata extractpdf_test_pdf_outline \
  extractpdf_test_pdf_annotations extractpdf_test_pdf_annotation_mutation
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: `100% tests passed, 0 tests failed out of 19`.

Then build only the new target:

```bash
cmake --build build --target extractpdf_test_pdf_form --parallel 2
```

Expected: compile failure naming absent approved Forms declarations such as `extractpdf_form`, `extractpdf_document_form`, or `EXTRACTPDF_FORM_FIELD_TEXT`. A fixture parser failure, link failure against an accidentally partial ABI, or unrelated old-target failure is not a valid RED.

- [ ] **Step 5: Commit only the RED surface**

```bash
git add tests/test_pdf_form.c tests/CMakeLists.txt tests/fixtures/acroform-*.pdf
git commit -m "test: define AcroForm snapshot red"
```

Confirm base-to-head production diff remains zero.

- [ ] **Step 6: Open a draft PR and capture remote RED evidence**

Open a draft PR from `feat/acroform-snapshot` to `master`, title `feat: add immutable AcroForm field/widget snapshot`, referencing #43 and the committed design/plan. Record the exact RED SHA in the PR and #43. The ordinary PR workflow should fail in Linux compilation at the new Forms target because the ABI is absent; do not label `full-ci` while intentionally RED.

---

### Task 2: Extract shared raw PDF observation primitives without changing behavior

**Files:**
- Create: `src/pdf_object_common.h`
- Create: `src/pdf_object_common.c`
- Modify: `src/pdf_annotation_common.c`
- Modify: `src/pdf_annotation_common.h` only if include wiring requires it
- Modify: top-level `CMakeLists.txt`
- Test: existing 19 CTests only; the Forms target must remain compile RED because public ABI is still absent

**Interfaces:**
- Produces:

```c
int extractpdf_pdf_dict_find(
    fz_context *ctx, pdf_obj *dictionary, pdf_obj *key, pdf_obj **out_value);

extractpdf_status extractpdf_pdf_read_rect(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    fz_matrix page_ctm,
    extractpdf_rect *out_rect);

extractpdf_status extractpdf_pdf_read_optional_uint32(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    uint32_t missing_value,
    uint32_t *out_value);
```

- `extractpdf_pdf_read_rect` exactly preserves current annotation Rect semantics: key required, exactly four finite numbers, normalize raw endpoints, apply supplied matrix, require finite transformed endpoints, normalize transformed endpoints.
- `extractpdf_pdf_read_optional_uint32` requires an integer when present and range `[0, UINT32_MAX]`; missing uses `missing_value`.

- [ ] **Step 1: Add the shared helper declarations/implementation**

Move the existing dictionary scan, Rect parsing, and `/F` uint32 logic out of `pdf_annotation_common.c` without semantic edits. Keep form-specific logic out of this file.

- [ ] **Step 2: Convert annotation common to the helper**

Replace its private `dict_find`, bounds, and flags bodies with calls to the shared functions. Contents and annotation subtype classification stay in `pdf_annotation_common.c`.

- [ ] **Step 3: Add `src/pdf_object_common.c` to the library**

Place it before annotation/form consumers in `add_library(extractpdf ...)`.

- [ ] **Step 4: Run the old regression gate**

Because the Forms target is still intentionally compile RED, do not build the default all-target graph. Build the library plus the 19 old targets and run CTest excluding Forms using the commands from Task 1 Step 4.

Expected: 19/19 pass. Also rebuild `extractpdf_test_pdf_form` and confirm it still fails only on absent Forms ABI, not on the helper refactor.

- [ ] **Step 5: Commit**

```bash
git add src/pdf_object_common.h src/pdf_object_common.c \
  src/pdf_annotation_common.c src/pdf_annotation_common.h CMakeLists.txt
git commit -m "refactor: share strict PDF object readers"
```

---

### Task 3: Add the public ABI and empty/non-PDF snapshot shell

**Files:**
- Create: `src/pdf_form_common.h`
- Create: `src/pdf_form_common.c`
- Create: `src/pdf_form.c`
- Modify: `include/extractpdf/extractpdf.h`
- Modify: `CMakeLists.txt`
- Test: `tests/test_pdf_form.c --case api`, `--case empty`, plus existing 19

**Interfaces:**
- Public ABI is copied verbatim from the approved spec: `extractpdf_form`, field/value/option enums, four info structs, `extractpdf_document_form`, field/name/label/value/option/widget accessors, and `extractpdf_drop_form`.
- Private semantic model entry point:

```c
typedef struct extractpdf_pdf_form_model extractpdf_pdf_form_model;

extractpdf_status extractpdf_pdf_form_parse(
    fz_context *ctx,
    pdf_document *document,
    extractpdf_pdf_form_model **out_model);

void extractpdf_pdf_form_drop_model(extractpdf_pdf_form_model *model);
```

- `extractpdf_pdf_form_model` is deep-owned C heap data and contains no borrowed `pdf_obj *`, `pdf_page *`, or MuPDF string pointers after parse returns.

- [ ] **Step 1: Add the exact public declarations**

Add the opaque handle near other public opaque handles and the enums/info structs after annotation types. Add API declarations near other document-root snapshots. Do not add mutation refs or setters.

Use these exact approved structs:

```c
typedef struct extractpdf_form extractpdf_form;

typedef enum extractpdf_form_field_type {
    EXTRACTPDF_FORM_FIELD_UNKNOWN = 0,
    EXTRACTPDF_FORM_FIELD_PUSH_BUTTON = 1,
    EXTRACTPDF_FORM_FIELD_CHECKBOX = 2,
    EXTRACTPDF_FORM_FIELD_RADIO_BUTTON = 3,
    EXTRACTPDF_FORM_FIELD_TEXT = 4,
    EXTRACTPDF_FORM_FIELD_COMBO_BOX = 5,
    EXTRACTPDF_FORM_FIELD_LIST_BOX = 6,
    EXTRACTPDF_FORM_FIELD_SIGNATURE = 7
} extractpdf_form_field_type;

typedef enum extractpdf_form_value_presence {
    EXTRACTPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    EXTRACTPDF_FORM_VALUE_MISSING = 1,
    EXTRACTPDF_FORM_VALUE_PRESENT = 2
} extractpdf_form_value_presence;

typedef enum extractpdf_form_value_kind {
    EXTRACTPDF_FORM_VALUE_UTF8 = 1,
    EXTRACTPDF_FORM_VALUE_OPTION = 2
} extractpdf_form_value_kind;

typedef struct extractpdf_form_value_info {
    size_t struct_size;
    extractpdf_form_value_kind kind;
    size_t option_index;
} extractpdf_form_value_info;

typedef enum extractpdf_form_option_kind {
    EXTRACTPDF_FORM_OPTION_BUTTON_STATE = 1,
    EXTRACTPDF_FORM_OPTION_CHOICE = 2
} extractpdf_form_option_kind;

typedef struct extractpdf_form_option_info {
    size_t struct_size;
    extractpdf_form_option_kind kind;
} extractpdf_form_option_info;

typedef struct extractpdf_form_field_info {
    size_t struct_size;
    extractpdf_form_field_type type;
    uint32_t flags;
    extractpdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} extractpdf_form_field_info;

typedef struct extractpdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_form_widget_info;
```

- [ ] **Step 2: Create the private deep model layout**

In `pdf_form_common.h`, define private records with offsets into one growable string arena; do not expose MuPDF pointers:

```c
typedef struct extractpdf_pdf_form_string {
    size_t offset;
    size_t size;
    int present;
} extractpdf_pdf_form_string;

typedef struct extractpdf_pdf_form_value {
    extractpdf_form_value_kind kind;
    size_t option_index;
    extractpdf_pdf_form_string utf8;
} extractpdf_pdf_form_value;

typedef struct extractpdf_pdf_form_option {
    extractpdf_form_option_kind kind;
    extractpdf_pdf_form_string export_text;
    extractpdf_pdf_form_string display_text;
    extractpdf_pdf_form_string private_button_state;
} extractpdf_pdf_form_option;

typedef struct extractpdf_pdf_form_field {
    extractpdf_form_field_type type;
    uint32_t flags;
    extractpdf_form_value_presence value_presence;
    size_t value_first, value_count;
    size_t option_first, option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
    extractpdf_pdf_form_string name;
    extractpdf_pdf_form_string label;
} extractpdf_pdf_form_field;

typedef struct extractpdf_pdf_form_widget {
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_pdf_form_widget;
```

`extractpdf_pdf_form_model` owns arrays for those records plus `char *strings` and counts/capacities. Add overflow-checked append/grow/drop helpers in `pdf_form_common.c`.

- [ ] **Step 3: Implement only empty/non-PDF parsing first**

`extractpdf_document_form()`:

1. require/reset `out_form`;
2. validate `extractpdf_document` internals;
3. obtain `pdf_document_from_fz_document`; non-PDF -> UNSUPPORTED;
4. call `extractpdf_pdf_form_parse`;
5. allocate public `extractpdf_form` wrapper owning the returned model only after full success.

For this task, `extractpdf_pdf_form_parse` fully handles only:

```text
no AcroForm -> empty model
AcroForm non-dict -> FORMAT
AcroForm dict, no Fields -> empty model
Fields non-array -> FORMAT
Fields [] -> empty model
non-empty Fields -> UNSUPPORTED temporary task boundary
```

The temporary `UNSUPPORTED` for non-empty Fields is private implementation staging only and disappears in Task 4.

- [ ] **Step 4: Implement all accessor reset/argument mechanics over the model**

Even before populated forms exist, every public symbol must link. Count functions reset count first; string functions reset pointer/size first; info functions validate minimum `struct_size`, reset every known field including `SIZE_MAX` sentinels, then validate snapshot/index. Kind-specific string accessors return UNSUPPORTED after reset when the value/option kind does not carry public text.

- [ ] **Step 5: Build and run the first GREEN cases**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case api
./build/tests/extractpdf_test_pdf_form --case empty
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: `api` and `empty` pass; old 19 pass. The no-argument Forms CTest is allowed to remain RED because populated-field cases are not implemented yet.

- [ ] **Step 6: Commit**

```bash
git add include/extractpdf/extractpdf.h src/pdf_form_common.h \
  src/pdf_form_common.c src/pdf_form.c CMakeLists.txt
git commit -m "feat: add AcroForm snapshot ABI shell"
```

---

### Task 4: Implement strict field graph, field groups, inheritance, names, and labels

**Files:**
- Modify: `src/pdf_form_common.c`
- Modify: `src/pdf_form_common.h` only for private parser metadata
- Test: `tests/test_pdf_form.c --case structure`

**Interfaces:**
- Produces a populated field array in deterministic terminal-group DFS order before Widget/value normalization.
- Private transient graph records may contain borrowed PDF objects only while `extractpdf_pdf_form_parse` is running; nothing borrowed survives return.

- [ ] **Step 1: Build a transient field-node graph with stable private identity**

Use a private node record conceptually equivalent to:

```c
typedef struct extractpdf_pdf_form_node {
    pdf_obj *raw_object;      /* borrowed only during parse */
    pdf_obj *resolved_dict;   /* borrowed only during parse */
    size_t parent_node;
    size_t group_index;
    size_t depth;
    int has_local_t;
    int is_widget;
} extractpdf_pdf_form_node;
```

Define `same_object_identity` as indirect object number+generation equality when both references are indirect; otherwise require the same resolved direct dictionary pointer. Do not deep-compare two distinct direct dictionaries and call them the same object.

Traversal order:

1. check repeated/cycle identity before depth limit;
2. if a new otherwise-valid node has depth >256 -> UNSUPPORTED;
3. validate `/Kids` is array when present;
4. validate child dictionaries;
5. validate each child `/Parent` exactly names the traversed parent identity;
6. reject top-level external `/Parent`.

- [ ] **Step 2: Assign logical field groups using `/T` boundaries**

Rules encoded directly in traversal:

```text
top-level node -> starts a group whether /T exists or not
child with local /T present -> starts a new child group
child with no local /T -> remains in parent group
```

A present-empty `/T` still starts a boundary. Validate every observed `/T` is a PDF string; reject partial names containing `.`. Decode text without calling `pdf_load_field_name()` as public authority.

Mark parent group as containing a named child group whenever a child begins a new `/T` boundary. A group with both any Widget instance and any named child group -> FORMAT. Publish only groups with no named child group.

- [ ] **Step 3: Resolve inheritance only over the validated parent graph**

Implement a helper that searches the already-built node chain for a key without calling `pdf_dict_get_inheritable`:

```c
typedef struct extractpdf_pdf_form_effective {
    pdf_obj *value;
    size_t owner_node;
    int present;
} extractpdf_pdf_form_effective;
```

Use it for `/FT`, `/Ff`, `/V`, `/TU` and retain `owner_node` transiently. Same-group nodes that participate in semantics/Widgets must normalize effective `/FT`, `/Ff`, and `/V` consistently; conflicting overrides -> FORMAT.

- [ ] **Step 4: Materialize field type, raw `/Ff`, fully-qualified name, and label**

- `/FT` required on public terminal group; present non-name -> FORMAT; unknown name -> UNKNOWN.
- `/Ff`: missing 0; present strict integer `[0, UINT32_MAX]`; contradictory subtype flags -> FORMAT.
- fully-qualified name is joined only from `/T` boundary components; anonymous top-level -> missing; one empty component -> present-empty.
- `/TU` label is effective `/TU` only and never falls back to `/T`.
- distinct public fields with same non-empty final name -> FORMAT.

For this task, initialize value semantics conservatively: known Text with missing value may be MISSING, Push/Signature/Unknown N/A, while choice/button payloads may remain a temporary UNSUPPORTED parse boundary until later tasks. The `structure` fixtures are chosen so they do not require those later value kinds.

- [ ] **Step 5: Make all structural failure fixtures deterministic**

`test_field_groups_and_structure()` must assert exact status for bad root/fields/kid/cycle/repeated/parent/root-parent/depth/mixed-group/group-conflict/duplicate-name/period-name/bad-FT/bad-FF. Call late structural failures twice where useful to prove repeatability.

- [ ] **Step 6: Run task GREEN**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case structure
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: structure case passes and existing 19/19 pass.

- [ ] **Step 7: Commit**

```bash
git add src/pdf_form_common.c src/pdf_form_common.h tests/test_pdf_form.c
git commit -m "feat: parse strict AcroForm field groups"
```

---

### Task 5: Reconcile page Widgets and normalize Widget geometry/button options

**Files:**
- Modify: `src/pdf_form_common.c`
- Test: `tests/test_pdf_form.c --case widgets`

**Interfaces:**
- Consumes: validated terminal logical field groups from Task 4.
- Produces: `model->widgets[]` in global page/Annots order, per-field `widget_count`, strict Fitz bounds/raw flags, and BUTTON_STATE options/button_option_index for checkbox/radio fields.

- [ ] **Step 1: Discover all field-tree Widgets during graph traversal**

A field-tree dictionary with `/Subtype /Widget` is a Widget occurrence owned by its current logical group. To participate in page reconciliation it must be an indirect object. Record its indirect num/gen identity and group index in a transient table; do not publish it yet.

If a Widget `/AP/N` is absent, record no usable button state. For checkbox/radio only, if `/AP/N` is present it must be a dictionary with zero or one non-Off key. More than one non-Off key -> FORMAT. The non-Off PDF Name is private bytes/string data only.

- [ ] **Step 2: Scan every PDF page in ascending index order**

Load each page with MuPDF inside `fz_try/fz_always` and drop it before moving on. Read raw page `/Annots`:

```text
missing -> zero entries
present non-array -> zero entries
array -> iterate in raw order
```

Ignore non-dictionary entries and dictionaries whose `/Subtype` is not the PDF Name `Widget`. For a Widget dictionary:

- require indirect identity;
- find exactly one field-tree Widget by num/gen;
- no match -> orphan FORMAT;
- already matched -> duplicate FORMAT;
- `/P`, if present, must identify the actual page object;
- compute `page_ctm` once for the page;
- use `extractpdf_pdf_read_rect(..., PDF_NAME(Rect), page_ctm, ...)`;
- use `extractpdf_pdf_read_optional_uint32(..., PDF_NAME(F), 0, ...)`.

Append the public Widget record immediately in page/raw-Annots order only to the private in-progress model. Any later failure drops the whole model.

After all pages, every field-tree Widget must have exactly one page match; unmatched -> FORMAT.

- [ ] **Step 3: Build field-local BUTTON_STATE options in public Widget order**

For each checkbox/radio Widget that has exactly one private non-Off state:

1. search existing options for the owning field by private bytewise state equality;
2. first occurrence appends a BUTTON_STATE option with no public export/display strings;
3. set Widget `button_option_index` to that field-local option index;
4. repeated same state reuses the earlier option.

Non-button Widget `button_option_index = SIZE_MAX`. Valid button Widget with no usable non-Off state also uses `SIZE_MAX`.

- [ ] **Step 4: Lock Widget failure/tolerance cases**

`test_widgets()` must cover:

- `acroform-annots-nonarray.pdf` success with zero Widgets;
- main fixture exact global Widget order, field ownership, representative Fitz bounds, raw `/F=2147483649` preservation;
- bad button AP, orphan, missing, duplicate, `/P` mismatch, direct Widget, bad Rect, bad F -> exact FORMAT.

- [ ] **Step 5: Run task GREEN**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case widgets
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: Widgets case and old 19 pass.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_form_common.c tests/test_pdf_form.c
git commit -m "feat: reconcile AcroForm widgets"
```

---

### Task 6: Materialize Text, Signature, UNKNOWN values and finish generic accessor semantics

**Files:**
- Modify: `src/pdf_form_common.c`
- Modify: `src/pdf_form.c`
- Test: `tests/test_pdf_form.c --case scalar-values`, `--case api`

**Interfaces:**
- Text values append UTF8 records into the model string arena.
- Signature/PushButton/UNKNOWN ordinary value model is N/A.
- Public accessor semantics are fully stable after this task.

- [ ] **Step 1: Implement strict PDF text-string decoding helper**

Only accept `pdf_is_string`; use `pdf_to_text_string` for decode after type validation, copy immediately into the model arena, and use `strlen` as UTF-8 byte count. Stream, array, name, number, dict for Text `/V` -> FORMAT. Never call `pdf_field_value()`.

- [ ] **Step 2: Materialize Text presence exactly**

```text
no effective /V -> MISSING / 0
string () -> PRESENT / one UTF8 present-empty
string text -> PRESENT / one UTF8
other -> FORMAT
```

- [ ] **Step 3: Materialize Signature and UNKNOWN**

Signature:

```text
ordinary value_presence = NOT_APPLICABLE
value_count = 0
missing /V -> is_signed=0
/V dictionary with absent /Type -> is_signed=1
/V dictionary with /Type /Sig -> is_signed=1
/V non-dict, or dict /Type present and not /Sig -> FORMAT
```

PushButton and UNKNOWN:

```text
value_presence = NOT_APPLICABLE
value_count=0
option_count=0 unless PushButton-specific options are explicitly modeled (they are not in V1)
is_multiselect=0
is_signed=0
```

Do not guess UNKNOWN `/V` semantics.

- [ ] **Step 4: Complete public accessor implementation**

`extractpdf_form_field_value_get_info` returns stored kind/option index. `value_utf8` succeeds only for UTF8. `option_export/display` succeeds only for CHOICE. All functions reset outputs before any later validation. Field/widget/value/option out-of-range -> ARGUMENT.

For info structs, use `offsetof(last_known_field)+sizeof(last_known_field)` for minimum size and preserve `struct_size`/trailing bytes.

- [ ] **Step 5: Run scalar/API GREEN**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case api
./build/tests/extractpdf_test_pdf_form --case scalar-values
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

`scalar-values` checks main fixture fields 0-6 and 13-16, missing/present-empty strings/labels, signature states, UNKNOWN, plus bad Text value, stream value twice, and bad signature.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_form_common.c src/pdf_form.c tests/test_pdf_form.c
git commit -m "feat: materialize AcroForm scalar values"
```

---

### Task 7: Implement Choice options, `/I` identity, and Combo/List values

**Files:**
- Modify: `src/pdf_form_common.c`
- Test: `tests/test_pdf_form.c --case choice-values`

**Interfaces:**
- Produces CHOICE options with deep-owned export/display strings and OPTION/UTF8 field values for Combo/List.

- [ ] **Step 1: Parse `/Opt` strictly for Choice fields**

If `/Opt` is missing, option_count=0. If present it must be an array. Each element is either:

```text
PDF string -> export=display=decoded string
array of exactly 2 PDF strings -> export=decoded element 0, display=decoded element 1
otherwise -> FORMAT
```

Copy both strings into the arena preserving present-empty.

- [ ] **Step 2: Parse `/I` only when present and validate identity**

`/I` when present must be an array of integers. Every index must be non-negative, `< option_count`, unique, and cardinality-compatible with field flags. Preserve `/I` array order as public selected-value order for multi-select.

- [ ] **Step 3: Normalize `/V` and reconcile it with `/I`**

Choice `/V` accepted shapes:

- Combo/single List: one PDF string;
- multi List: PDF string for one selection or array of PDF strings for multiple selections;
- explicit empty selection may be represented by an empty `/V` array for multi List; treat as PRESENT/0;
- any other type -> FORMAT.

If valid `/I` exists, map each selected index directly to OPTION and require the corresponding option export string(s) exactly match `/V` selection string(s). Contradiction -> FORMAT.

Without `/I`, map each `/V` string to exactly one option export value. Duplicate export values causing more than one match -> FORMAT. For editable Combo only, an unmatched single string becomes one UTF8 custom value. Non-editable Combo/List unmatched value -> FORMAT.

- [ ] **Step 4: Lock field-cardinality semantics**

- normal Combo: max one selected value;
- editable Combo custom string allowed only when Edit flag is set together with Combo;
- single List: max one;
- MultiSelect List: zero or more OPTION values;
- contradictory flag combinations that make those rules ambiguous -> FORMAT.

- [ ] **Step 5: Run Choice GREEN**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case choice-values
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Assert main fixture:

```text
country options: (US, United States), (JP, Japan); value OPTION(1)
city options: Tokyo/Osaka; value UTF8("Kyoto")
size: value OPTION(1)
colors: values OPTION(0), OPTION(2) in /I order
```

Also assert bad `/Opt` and contradictory `/I` -> FORMAT.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_form_common.c tests/test_pdf_form.c
git commit -m "feat: materialize AcroForm choice values"
```

---

### Task 8: Finish checkbox/radio values, lifetime, no-execution proof, and full 20th CTest GREEN

**Files:**
- Modify: `src/pdf_form_common.c`
- Modify: `src/pdf_form.c` only if final ownership/publication corrections are required
- Test: `tests/test_pdf_form.c --case button-values`, `--case lifetime`, full CTest

**Interfaces:**
- Completes all spec semantics; after this task no temporary UNSUPPORTED parser boundary remains for valid AcroForm fields.

- [ ] **Step 1: Normalize checkbox/radio `/V` against BUTTON_STATE options**

Effective button `/V`:

```text
missing -> MISSING / 0
PDF Name /Off -> PRESENT / 0
PDF Name matching exactly one field BUTTON_STATE option -> PRESENT / OPTION(index)
non-name or unmatched state -> FORMAT
```

A selected state must have been discovered from reconciled Widgets; do not invent MuPDF's fallback `/Yes` when the file contains no such state.

- [ ] **Step 2: Assert main button semantics**

`agree`: option_count 1, selected OPTION(0), both Widgets point to button option 0.

`payment`: option_count 2 in global Widget first-seen order: Visa=0, Mastercard=1; selected OPTION(1); third Visa Widget points back to option 0.

`acroform-bad-button-ap.pdf` stays FORMAT because one Widget advertises >1 non-Off normal state.

- [ ] **Step 3: Prove document-independent lifetime and independent snapshots**

From `acroform-main.pdf`:

1. extract two form snapshots;
2. assert distinct handles and identical semantic contents;
3. close the source document;
4. read representative field names, labels, UTF8 values, options, and Widgets from both snapshots;
5. drop independently.

- [ ] **Step 4: Prove no source normalization and no PDF behavior execution**

`acroform-stream-value.pdf`: call `extractpdf_document_form()` twice on the same still-open document; both return FORMAT and reset output. This catches source write-back normalization.

`acroform-js.pdf`: call snapshot extraction twice; both snapshots must show `marker="SAFE"` and `dependent="UNCHANGED"`. No OpenAction, field AA Keystroke/Validate/Calculate/Format action is executed.

Do not enable or call any MuPDF JS/form-event API in production code.

- [ ] **Step 5: Prove late-failure publication atomicity**

On `acroform-late-malformed.pdf`, initialize output to a sentinel, call extraction twice, require FORMAT and output NULL both times. The earlier valid field is never obtainable as a prefix.

- [ ] **Step 6: Run the full local feature gate**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case button-values
./build/tests/extractpdf_test_pdf_form --case lifetime
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 20`.

Configure/build/test ASan/UBSan exactly like CI:

```bash
cmake -S . -B build-asan \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

Expected: 20/20.

- [ ] **Step 7: Commit final behavior**

```bash
git add src/pdf_form_common.c src/pdf_form.c tests/test_pdf_form.c
git commit -m "feat: complete immutable AcroForm snapshot"
```

Freeze the resulting feature SHA; no more feature commits after the cross-platform proof starts unless a failing proof requires a new fix and a completely new proof cycle.

---

### Task 9: Exact-head feature verification, full-ci, scope review, then STOP

**Files:**
- No production changes expected.
- Update PR/#43 evidence text only after results are known.

**Interfaces:**
- Produces immutable exact-head evidence required before integration authorization.

- [ ] **Step 1: Verify ordinary PR Linux workflow is GREEN on the frozen head**

Confirm the workflow checkout corresponds to the exact frozen feature head (or GitHub's synthetic merge whose PR head is that exact SHA and base is current master). Require Linux static build+20/20 and sanitizer build+20/20.

If master moved after branch creation, inspect the synthetic merge proof rather than silently rebasing. Any feature commit change invalidates previous same-head evidence and restarts this step.

- [ ] **Step 2: Trigger same-head `full-ci`**

Add the `full-ci` label to the draft PR without changing the feature SHA. Require one run whose PR head is the frozen SHA and whose jobs all succeed:

```text
Linux static       20/20
Linux ASan/UBSan   20/20
macOS              20/20
Windows DLL        20/20
```

Windows evidence must show `pdf_object_common.c`, `pdf_form_common.c`, `pdf_form.c` compiling into `extractpdf.dll`, `extractpdf_test_pdf_form.exe` building, and `extractpdf.pdf_form` running as test 20/20.

- [ ] **Step 3: Review exact base->head scope**

Compare integrated base/current merge base to frozen feature head. Expected changed paths are limited to:

```text
docs/superpowers/specs/2026-08-29-extractpdf-acroform-snapshot-design.md
docs/superpowers/plans/2026-08-29-extractpdf-acroform-snapshot.md
include/extractpdf/extractpdf.h
CMakeLists.txt
src/pdf_object_common.h
src/pdf_object_common.c
src/pdf_annotation_common.h      (only if include wiring changed)
src/pdf_annotation_common.c
src/pdf_form_common.h
src/pdf_form_common.c
src/pdf_form.c
tests/CMakeLists.txt
tests/test_pdf_form.c
tests/fixtures/acroform-*.pdf
```

Reject unrelated render/text/image/link/outline/metadata/composition/editor changes.

- [ ] **Step 4: Fresh review against the spec**

Review Critical/Important boundaries explicitly:

- field-group `/T` semantics and unnamed appearance children;
- validated inheritance/no repair;
- exact missing vs empty states;
- Choice `/I`/duplicate-export behavior;
- button state mapping without invented `/Yes`;
- Widget page reconciliation/order/geometry/u32 flags;
- Signature/UNKNOWN policy;
- source immutability/no JS/events;
- public output reset/struct_size/ownership/lifetime;
- no partial publication.

Resolve any blocker with a new commit and repeat all proof steps on the new SHA.

- [ ] **Step 5: Record evidence and STOP for explicit integration authorization**

Update PR and #43 with strict RED SHA/workflow, final GREEN SHA/workflow, same-head full-ci run ID, test counts/platforms, final scope, and review result. Update roadmap #2 to `AcroForm Snapshot V1 — implementation proven; integration authorization pending`.

**STOP. Do not mark ready/merge/close #43 and do not create Form Value Mutation V1 yet.**

---

### Task 10: Integrate only after explicit authorization

**Files:**
- No code changes expected.
- GitHub state/evidence updates only.

**Interfaces:**
- Consumes: explicit human integration authorization plus Task 9 exact-head proof.
- Produces: merged master, integrated-master push proof, #43 closed completed, roadmap updated.

- [ ] **Step 1: Re-verify merge gate immediately before merge**

Fetch PR metadata, exact head SHA, base SHA, comments/reviews/threads, and same-head full-ci. Head must still equal the proven feature SHA; no unresolved Critical/Important blocker.

- [ ] **Step 2: Mark PR ready and merge with expected head**

Use GitHub expected-head merge with `expected_head_sha=<proven-feature-sha>` and merge method `merge`. If the draft->ready connector action has the same upstream GraphQL schema failure previously observed, use a non-draft integration carrier branch pointing directly at the exact proven feature SHA, create no new feature commit, re-verify its base/head/review state, and merge that carrier with the same expected-head guard. Record the workaround transparently.

- [ ] **Step 3: Require integrated-master push proof**

Find the `push` workflow whose `head_sha` equals the resulting merge commit. Require completed/success and all three jobs:

```text
Linux static + sanitizer   20/20 each
macOS                      20/20
Windows DLL                20/20
```

Fetch Windows logs and confirm Forms sources, DLL, Forms test executable, and test 20/20.

- [ ] **Step 4: Close bookkeeping only after integrated proof**

Add integration evidence to #43, close it with reason `completed`, update roadmap #2 to `AcroForm Snapshot V1 — integrated`, and update canonical PR body with merge SHA + integrated workflow. Only after that bookkeeping is complete may a separate Form Value Mutation V1 issue be created in a later user-authorized task.
