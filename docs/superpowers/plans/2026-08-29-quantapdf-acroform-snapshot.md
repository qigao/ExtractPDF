# QuantaPDF AcroForm Snapshot V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a strict, immutable, document-independent AcroForm snapshot that exposes logical field groups, typed values/options, and reconciled page Widget instances without mutating the source PDF or executing PDF behavior.

**Architecture:** Parse raw AcroForm objects into a private deep-owned semantic model in `pdf_form_common.c` after strict Parent/Kids/group preflight, validated inheritance, typed value normalization, and page-Widget reconciliation. `pdf_form.c` owns only public snapshot publication/accessors. A tiny shared raw-object helper keeps Widget Rect/F semantics identical to annotation Rect/F semantics without coupling Forms to annotation classification.

**Tech Stack:** C11, MuPDF 1.28.2, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, CMake 3.20+, CTest, Linux warnings-as-errors, Linux ASan/UBSan, macOS, Windows DLL with MSVC `/W4 /WX`.

**Spec:** `docs/superpowers/specs/2026-08-29-quantapdf-acroform-snapshot-design.md`

## Global Constraints

- Integrated base: `0e4b769753215725797a557c4f18c4654e444e30`.
- Final self-reviewed design: `d586aceb3837ba8169af67470fd81ffb4a75a867`.
- Scope is #43 only. Do not create/implement Form Value Mutation V1, field refs/setters, XFA, structural form mutation, JavaScript/events, signing, NeedAppearances rewriting, or flattening.
- Public logical objects are field **groups**: local `/T` starts a logical-name boundary; unnamed descendants remain in the nearest group.
- Public field/widget/option indices are snapshot-local coordinates only.
- Valid PDF with no `/AcroForm`, no `/Fields`, or empty `/Fields` -> `OK` + non-NULL empty snapshot.
- Non-PDF -> `UNSUPPORTED`, output reset to NULL.
- Failure publication is atomic: no partial prefix.
- Never repair Parent/Kids, normalize/write `/V`, dirty fields, update appearances, calculate forms, or execute JavaScript/actions/events.
- Never use `pdf_field_value()` or a repairing inheritable-parent walker as public semantic authority; parse raw objects over the validated Parent graph.
- Widget order: page index ascending, then raw page `/Annots` order. Missing/non-array `/Annots` is empty; non-dict members ignored.
- Every published Widget is reachable from exactly one terminal group and appears exactly once on exactly one page. Direct reconciled Widgets are `FORMAT`.
- Widget `/Rect` uses existing Fitz page-space transform. Widget `/F` and field `/Ff` preserve `[0, UINT32_MAX]` without signed narrowing.
- Snapshot strings are deep-owned and borrowed until `quantapdf_drop_form()`.
- String tri-state: missing `NULL/0`; present-empty non-NULL/0; present-value non-NULL/size.
- Public structs use minimum-known `struct_size`: too small `ARGUMENT`; larger accepted; caller trailing bytes untouched.
- CTest progression: 19 -> 20.
- Final feature proof: exact-head Linux static 20/20 + ASan/UBSan 20/20, then same-SHA `full-ci` macOS 20/20 + Windows DLL 20/20.
- No merge/close before explicit integration authorization and integrated-master push proof.

---

## File Map

**Create**
- `src/pdf_object_common.h`, `src/pdf_object_common.c` — shared raw dict/Rect/u32 readers.
- `src/pdf_form_common.h`, `src/pdf_form_common.c` — strict private AcroForm semantic parser/model.
- `src/pdf_form.c` — public immutable snapshot/accessors.
- `tests/test_pdf_form.c` — twentieth CTest with task-level case selector.
- deterministic `tests/fixtures/acroform-*.pdf`.

**Modify**
- `include/quantapdf/quantapdf.h` — approved read-only Forms ABI only.
- `src/pdf_annotation_common.c` and, only if include wiring needs it, `src/pdf_annotation_common.h` — use shared Rect/F helper, no semantic change.
- `CMakeLists.txt`, `tests/CMakeLists.txt`.

**Do not modify**
- `.github/workflows/ci.yml`.
- `src/pdf_edit*`.
- unrelated subsystems.

---

### Task 1: Complete strict compile RED

**Files:**
- Create: `tests/test_pdf_form.c`.
- Create: deterministic `tests/fixtures/acroform-*.pdf` listed below.
- Modify: `tests/CMakeLists.txt`.
- No production header/source changes.

**Interfaces:** consumes only current 19-test API; produces one wished-for Forms test target that must fail to compile because approved Forms declarations are absent.

- [ ] **Step 1: Generate offset-safe checked-in PDF fixtures with a one-shot helper**

Use a temporary local script, never committed/runtime-required:

```python
from pathlib import Path


def pdf_bytes(objects, root=1):
    out = bytearray(b"%PDF-1.7\n% deterministic-acroform\n")
    offsets = [0] * (max(objects) + 1)
    for n in sorted(objects):
        offsets[n] = len(out)
        out += f"{n} 0 obj\n".encode("ascii")
        body = objects[n]
        if isinstance(body, str):
            body = body.encode("latin-1")
        out += body
        if not body.endswith(b"\n"):
            out += b"\n"
        out += b"endobj\n"
    xref = len(out)
    out += f"xref\n0 {len(offsets)}\n".encode("ascii")
    out += b"0000000000 65535 f \n"
    for n in range(1, len(offsets)):
        out += (f"{offsets[n]:010d} 00000 n \n".encode("ascii")
                if offsets[n] else b"0000000000 00000 f \n")
    out += (f"trailer\n<< /Size {len(offsets)} /Root {root} 0 R >>\n"
            f"startxref\n{xref}\n%%EOF\n").encode("ascii")
    return bytes(out)


def write(name, objects):
    Path("tests/fixtures", name).write_bytes(pdf_bytes(objects))
```

All valid pages: `/MediaBox [0 0 200 200]`, no rotation, so `[x0 y0 x1 y1]` -> Fitz `[x0, 200-y1, x1, 200-y0]`.

Create layered valid fixtures so each later Task can become independently GREEN:

```text
acroform-structure.pdf
  public fields:
    0 profile.nickname TEXT: parent `profile` owns /FT /Ff /TU; child /T(nickname); no /V
    1 repeat TEXT: one /T boundary + two unnamed descendant field nodes; no /V
    2 anonymous top-level TEXT: no /T, no /V
    3 present-empty-name TEXT: /T(), no /V
  no Widgets

acroform-widgets.pdf
  field0 textWidget TEXT, one Widget, missing /V
  field1 agree CHECKBOX, two Widgets, each AP/N Off+Yes, missing /V
  field2 payment RADIO /Ff 32768, Widgets Visa/Mastercard/Visa, missing /V
  field3 future UNKNOWN /FT /Future, one Widget
  page0 raw order: textWidget [10 170 40 190], agree-A [50 170 70 190], payment-Visa-A [80 170 100 190]
  page1 raw order: agree-B [10 170 30 190] /F=2147483649, payment-Master [40 170 60 190], future [70 170 100 190]
  page2 raw order: payment-Visa-B [10 170 30 190]
  include one scalar and one ordinary non-Widget annotation in Annots

acroform-scalars.pdf
  textMissing TEXT no /V
  textEmpty TEXT /V()
  textValue TEXT /V(hello), /TU()
  submit PUSHBUTTON
  sigUnsigned SIGNATURE no /V
  sigSigned SIGNATURE /V << /Type /Sig >>
  future UNKNOWN /FT /Future

acroform-choice.pdf
  country COMBO /Ff 131072; /Opt [[(US)(United States)] [(JP)(Japan)]]; /V(JP); /I[1]
  city COMBO|EDIT /Ff 393216; /Opt[(Tokyo)(Osaka)]; /V(Kyoto)
  size LIST /Opt[(S)(M)(L)]; /V(M); /I[1]
  colors MULTI LIST /Ff 2097152; /Opt [[(r)(Red)] [(g)(Green)] [(b)(Blue)]]; /V[(r)(b)]; /I[0 2]

acroform-main.pdf
  final combined 3-page document, public field order:
    0 profile.nickname TEXT value Ada, label Profile label
    1 repeat TEXT value same, 2 Widgets
    2 anonymous TEXT value anon
    3 present-empty-name TEXT /T() /V()
    4 textMissing TEXT missing
    5 textEmpty TEXT empty
    6 textValue TEXT hello, /TU(), 1 Widget
    7 agree CHECKBOX /V/Yes, 2 Yes Widgets
    8 payment RADIO /V/Mastercard, Visa/Mastercard/Visa Widgets
    9 country COMBO
    10 city COMBO|EDIT
    11 size LIST
    12 colors MULTI LIST
    13 submit PUSHBUTTON
    14 sigUnsigned SIGNATURE
    15 sigSigned SIGNATURE /V << /Type /Sig >>
    16 future UNKNOWN
  Widget order:
    page0 repeat-A, agree-A, payment-Visa-A, country, submit
    page1 repeat-B, textValue, agree-B(/F=2147483649), payment-Master, city, sigUnsigned
    page2 payment-Visa-B, size, colors, sigSigned, future
  button options: agree Yes=0; payment Visa=0, Mastercard=1, later Visa reuses 0

acroform-no-fields.pdf          AcroForm dict, no /Fields
acroform-empty-fields.pdf       /Fields []
acroform-annots-nonarray.pdf    one Text field/zero Widgets, page /Annots 17; succeeds
acroform-js.pdf                 marker=SAFE, dependent=UNCHANGED + OpenAction/AA JS; values must never change
acroform-stream-value.pdf       Text /V indirect stream; repeated extraction FORMAT twice
```

Create strict failure fixtures with all unrelated structures valid:

```text
acroform-bad-root.pdf           AcroForm integer                         -> FORMAT
acroform-bad-fields.pdf         Fields integer                          -> FORMAT
acroform-bad-kid.pdf            Kids includes scalar                    -> FORMAT
acroform-cycle.pdf              Parent/Kids cycle                       -> FORMAT
acroform-repeated-node.pdf      same field object reached twice         -> FORMAT
acroform-parent-mismatch.pdf    child Parent != traversed parent        -> FORMAT
acroform-root-parent.pdf        top field has external Parent           -> FORMAT
acroform-depth-257.pdf          otherwise-valid depth 257               -> UNSUPPORTED
acroform-mixed-group.pdf        group owns Widget + named child group   -> FORMAT
acroform-group-conflict.pdf     unnamed same-group child conflicts /V   -> FORMAT
acroform-duplicate-name.pdf     two public fields same non-empty name   -> FORMAT
acroform-period-name.pdf        partial /T contains '.'                 -> FORMAT
acroform-bad-ft.pdf             effective /FT non-Name                  -> FORMAT
acroform-bad-ff.pdf             effective /Ff 4294967296                -> FORMAT
acroform-bad-value.pdf          Text /V integer                         -> FORMAT
acroform-bad-opt.pdf            malformed Choice /Opt entry             -> FORMAT
acroform-bad-i.pdf              /I[1] contradicts /V option0            -> FORMAT
acroform-bad-button-ap.pdf      AP/N Off + two non-Off states           -> FORMAT
acroform-orphan-widget.pdf      page Widget absent from Fields          -> FORMAT
acroform-missing-widget.pdf     field Widget absent from pages          -> FORMAT
acroform-duplicate-widget.pdf   Widget appears twice                    -> FORMAT
acroform-p-mismatch.pdf         actual page0, /P page1                  -> FORMAT
acroform-direct-widget.pdf      reconciled Widget direct dict           -> FORMAT
acroform-bad-widget-rect.pdf    Rect length !=4                         -> FORMAT
acroform-bad-widget-flags.pdf   F negative or >UINT32_MAX               -> FORMAT
acroform-bad-signature.pdf      Signature V dict Type /NotSig           -> FORMAT
acroform-late-malformed.pdf     first field valid, later bad Ff         -> FORMAT and NULL output
```

- [ ] **Step 2: Write the wished-for public test and case selector**

`tests/test_pdf_form.c` includes only `<quantapdf/quantapdf.h>` for library API, uses existing `CHECK` style, and compile-references every approved enum/struct/function. Dispatcher:

```c
static void run_case(const char *name)
{
    if (strcmp(name, "api-shell") == 0) test_api_shell();
    else if (strcmp(name, "empty") == 0) test_empty_and_non_pdf();
    else if (strcmp(name, "structure") == 0) test_structure();
    else if (strcmp(name, "widgets") == 0) test_widgets();
    else if (strcmp(name, "scalar-values") == 0) test_scalar_values_and_full_api();
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
    test_api_shell();
    test_empty_and_non_pdf();
    test_structure();
    test_widgets();
    test_scalar_values_and_full_api();
    test_choice_values();
    test_button_values();
    test_lifetime_atomicity_no_execution();
    return EXIT_SUCCESS;
}
```

`api-shell` contains only NULL/reset contracts that do not require a populated field. Populated `struct_size`, trailing-canary, index, and kind-specific accessor checks live in `scalar-values` so Task 3 can be independently GREEN.

- [ ] **Step 3: Register target/fixtures/Windows copy**

Add `quantapdf_test_pdf_form`, CTest `quantapdf.pdf_form`, timeout 60, compile definitions for every fixture above plus `NON_PDF=composition-non-pdf.txt` and `NO_ACROFORM_PDF=one-page.pdf`; add target to Windows DLL-copy foreach. No workflow change.

- [ ] **Step 4: Prove strict RED and exact old 19/19**

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2 --target \
  quantapdf_test_status quantapdf_test_document quantapdf_test_render \
  quantapdf_test_text quantapdf_test_structured_text quantapdf_test_text_search \
  quantapdf_test_images quantapdf_test_image_bitmap quantapdf_test_links \
  quantapdf_test_pdf_export quantapdf_test_pdf_range quantapdf_test_pdf_order \
  quantapdf_test_pdf_delete quantapdf_test_pdf_merge quantapdf_test_output_file \
  quantapdf_test_pdf_metadata quantapdf_test_pdf_outline \
  quantapdf_test_pdf_annotations quantapdf_test_pdf_annotation_mutation
ctest --test-dir build --output-on-failure -E '^quantapdf\.pdf_form$'
cmake --build build --target quantapdf_test_pdf_form --parallel 2
```

Expected: old 19/19 pass; new target compile fails on absent approved Forms declarations, not fixtures/runtime/linking.

- [ ] **Step 5: Commit/open draft PR/capture remote RED**

```bash
git add tests/test_pdf_form.c tests/CMakeLists.txt tests/fixtures/acroform-*.pdf
git commit -m "test: define AcroForm snapshot red"
```

Open draft PR `feat: add immutable AcroForm field/widget snapshot`, reference #43/design/plan, record exact RED SHA. Ordinary Linux PR run should fail at the missing Forms ABI. Do not label `full-ci` while RED.

---

### Task 2: Shared strict raw PDF object readers; old 19 regression gate

**Files:** create `src/pdf_object_common.[ch]`; modify annotation common and top-level CMake.

**Interfaces produced:**

```c
int quantapdf_pdf_dict_find(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    pdf_obj **out_value);

quantapdf_status quantapdf_pdf_read_rect(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    fz_matrix page_ctm,
    quantapdf_rect *out_rect);

quantapdf_status quantapdf_pdf_read_optional_uint32(
    fz_context *ctx,
    pdf_obj *dictionary,
    pdf_obj *key,
    uint32_t missing_value,
    uint32_t *out_value);
```

`read_rect`: key required, exactly four finite numbers, normalize raw endpoints, transform, require finite transformed endpoints, normalize transformed endpoints. `read_optional_uint32`: missing -> supplied default; present integer `[0,UINT32_MAX]` only.

- [ ] Move current annotation dict/Rect/F code into these helpers verbatim in semantics.
- [ ] Make annotation common use helpers; leave subtype/Contents behavior unchanged.
- [ ] Add `src/pdf_object_common.c` to library.
- [ ] Rebuild/run old 19 with Task-1 commands; Forms must still compile RED only on absent ABI.
- [ ] Commit:

```bash
git add src/pdf_object_common.h src/pdf_object_common.c \
  src/pdf_annotation_common.c src/pdf_annotation_common.h CMakeLists.txt
git commit -m "refactor: share strict PDF object readers"
```

---

### Task 3: Public Forms ABI + empty/non-PDF shell

**Files:** create `src/pdf_form_common.[ch]`, `src/pdf_form.c`; modify public header/top CMake.

**Public interfaces produced exactly:**

```c
typedef struct quantapdf_form quantapdf_form;

typedef enum quantapdf_form_field_type {
    QUANTAPDF_FORM_FIELD_UNKNOWN = 0,
    QUANTAPDF_FORM_FIELD_PUSH_BUTTON = 1,
    QUANTAPDF_FORM_FIELD_CHECKBOX = 2,
    QUANTAPDF_FORM_FIELD_RADIO_BUTTON = 3,
    QUANTAPDF_FORM_FIELD_TEXT = 4,
    QUANTAPDF_FORM_FIELD_COMBO_BOX = 5,
    QUANTAPDF_FORM_FIELD_LIST_BOX = 6,
    QUANTAPDF_FORM_FIELD_SIGNATURE = 7
} quantapdf_form_field_type;

typedef enum quantapdf_form_value_presence {
    QUANTAPDF_FORM_VALUE_NOT_APPLICABLE = 0,
    QUANTAPDF_FORM_VALUE_MISSING = 1,
    QUANTAPDF_FORM_VALUE_PRESENT = 2
} quantapdf_form_value_presence;

typedef enum quantapdf_form_value_kind {
    QUANTAPDF_FORM_VALUE_UTF8 = 1,
    QUANTAPDF_FORM_VALUE_OPTION = 2
} quantapdf_form_value_kind;

typedef struct quantapdf_form_value_info {
    size_t struct_size;
    quantapdf_form_value_kind kind;
    size_t option_index;
} quantapdf_form_value_info;

typedef enum quantapdf_form_option_kind {
    QUANTAPDF_FORM_OPTION_BUTTON_STATE = 1,
    QUANTAPDF_FORM_OPTION_CHOICE = 2
} quantapdf_form_option_kind;

typedef struct quantapdf_form_option_info {
    size_t struct_size;
    quantapdf_form_option_kind kind;
} quantapdf_form_option_info;

typedef struct quantapdf_form_field_info {
    size_t struct_size;
    quantapdf_form_field_type type;
    uint32_t flags;
    quantapdf_form_value_presence value_presence;
    size_t value_count;
    size_t option_count;
    size_t widget_count;
    int is_multiselect;
    int is_signed;
} quantapdf_form_field_info;

typedef struct quantapdf_form_widget_info {
    size_t struct_size;
    size_t field_index;
    int page_index;
    quantapdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} quantapdf_form_widget_info;

quantapdf_status quantapdf_document_form(
    quantapdf_document *document,
    quantapdf_form **out_form);
quantapdf_status quantapdf_form_field_count(
    const quantapdf_form *form,
    size_t *out_count);
quantapdf_status quantapdf_form_field_get_info(
    const quantapdf_form *form,
    size_t field_index,
    quantapdf_form_field_info *out_info);
quantapdf_status quantapdf_form_field_name(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_form_field_label(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_form_field_value_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    quantapdf_form_value_info *out_info);
quantapdf_status quantapdf_form_field_value_utf8(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_form_field_option_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    quantapdf_form_option_info *out_info);
quantapdf_status quantapdf_form_field_option_export(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_form_field_option_display(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_form_widget_count(
    const quantapdf_form *form,
    size_t *out_count);
quantapdf_status quantapdf_form_widget_get_info(
    const quantapdf_form *form,
    size_t widget_index,
    quantapdf_form_widget_info *out_info);
void quantapdf_drop_form(quantapdf_form *form);
```

**Private parser interface produced:**

```c
typedef struct quantapdf_pdf_form_model quantapdf_pdf_form_model;
quantapdf_status quantapdf_pdf_form_parse(
    fz_context *ctx,
    pdf_document *document,
    quantapdf_pdf_form_model **out_model);
void quantapdf_pdf_form_drop_model(quantapdf_pdf_form_model *model);
```

Private model is deep-owned C heap only after parse returns. Use string descriptors `{offset,size,present}`; values `{kind,option_index,utf8}`; options `{kind,export_text,display_text,private_button_state}`; fields store type/flags/presence/value range/option range/widget_count/multiselect/signed/name/label; Widgets store field/page/bounds/flags/button option.

- [ ] Add exact ABI; no mutation declarations.
- [ ] Implement overflow-safe arrays/string arena; present-empty gets stable non-NULL arena address.
- [ ] Implement `quantapdf_document_form`: reset output, validate document, require PDF, private parse, publish wrapper only on success.
- [ ] Task-3 private parse supports exactly:

```text
no AcroForm -> empty
AcroForm non-dict -> FORMAT
AcroForm dict/no Fields -> empty
Fields non-array -> FORMAT
Fields [] -> empty
non-empty Fields -> temporary UNSUPPORTED staging boundary
```

- [ ] Implement every public symbol so it links. Count/string/info outputs reset before later validation. Populated kind behavior finalizes in Task 6.
- [ ] Run:

```bash
cmake --build build --parallel 2
./build/tests/quantapdf_test_pdf_form --case api-shell
./build/tests/quantapdf_test_pdf_form --case empty
ctest --test-dir build --output-on-failure -E '^quantapdf\.pdf_form$'
```

Expected two Forms cases + old 19 pass.

- [ ] Commit:

```bash
git add include/quantapdf/quantapdf.h src/pdf_form_common.h \
  src/pdf_form_common.c src/pdf_form.c CMakeLists.txt
git commit -m "feat: add AcroForm snapshot ABI shell"
```

---

### Task 4: Strict field graph, logical groups, inheritance, names/labels

**Files:** `src/pdf_form_common.[ch]`, `tests/test_pdf_form.c`.

**Private transient node shape:**

```c
typedef struct quantapdf_pdf_form_node {
    pdf_obj *raw_object;
    pdf_obj *resolved_dict;
    size_t parent_node;
    size_t group_index;
    size_t depth;
    int has_local_t;
    int is_widget;
} quantapdf_pdf_form_node;

typedef struct quantapdf_pdf_form_effective {
    pdf_obj *value;
    size_t owner_node;
    int present;
} quantapdf_pdf_form_effective;
```

Borrowed PDF objects exist only while parse runs.

- [ ] Build identity table: indirect identity is num+gen; direct dict identity is resolved pointer; never deep-equal distinct direct dicts into one node.
- [ ] Traverse: repeated/cycle check first; otherwise-new depth >256 -> UNSUPPORTED; `/Kids` if present array; child dict; child `/Parent` exact traversed parent; top-level external Parent forbidden.
- [ ] Grouping:

```text
top-level -> starts group
child local /T present, including empty -> starts child group
child local /T missing -> remains parent group
```

Observed `/T` must string; literal `.` in partial `/T` -> FORMAT. Group owning any Widget and any named child group -> FORMAT. Publish groups with no named child group.

- [ ] Implement validated-chain effective lookup for `/FT`, `/Ff`, `/V`, `/TU`, `/Opt`, `/I`; keep owner node. Same-group effective `/FT`/`/Ff`/`/V` must agree; conflicting `/Opt`/`/I` definitions making options/selection ambiguous -> FORMAT.
- [ ] Materialize type/flags/name/label: public terminal `/FT` required Name; unknown -> UNKNOWN; `/Ff` missing 0 or strict u32; contradictory subtype flags -> FORMAT. Full name joins only `/T` boundary components. Anonymous -> missing. Present-empty boundary -> present-empty if final name empty. Duplicate non-empty public full name -> FORMAT. Label is effective `/TU` only; no fallback.
- [ ] Positive `acroform-structure.pdf`: exact 4 fields/order/names/label/type/flags/missing values. Failure fixtures: bad root/fields/kid/cycle/repeated/parent/root-parent/depth/mixed-group/group-conflict/duplicate-name/period/bad-FT/bad-FF.
- [ ] Run structure + old 19:

```bash
cmake --build build --parallel 2
./build/tests/quantapdf_test_pdf_form --case structure
ctest --test-dir build --output-on-failure -E '^quantapdf\.pdf_form$'
```

- [ ] Commit `feat: parse strict AcroForm field groups`.

---

### Task 5: Page Widget reconciliation, geometry/flags, button-option discovery

**Files:** `src/pdf_form_common.c`, `tests/test_pdf_form.c`.

**Produces:** global Widget array in page/raw order; per-field widget_count; BUTTON_STATE option table/button_option_index. Button selected `/V` is Task 8.

- [ ] Field-tree `/Subtype /Widget` belongs to current group and must be indirect. Record num/gen+group. Checkbox/radio `/AP/N`: absent -> no usable state; present must dict with zero or one non-Off key; >1 -> FORMAT. Keep raw state private.
- [ ] Scan every page ascending; load/drop safely. `/Annots` missing/non-array -> empty. Ignore non-dict/non-Widget. Widget must indirect and match exactly one field-tree identity; orphan/already matched -> FORMAT. `/P` if present must identify actual page.
- [ ] Compute page matrix once; call shared strict Rect and F readers; append private Widget in raw order. End-of-document unmatched field Widget -> FORMAT.
- [ ] Build BUTTON_STATE field options by first Widget appearance, bytewise dedup private state; set field-local option index. Non-button/no-state -> `SIZE_MAX`.
- [ ] `widgets` case uses `acroform-widgets.pdf`: exact order/ownership/Fitz bounds, `/F=2147483649`, agree one Yes option, payment Visa/Mastercard order + duplicate Visa. Also annots-nonarray success and bad AP/orphan/missing/duplicate/P/direct/Rect/F -> FORMAT.
- [ ] Run widgets + old 19 and commit `feat: reconcile AcroForm widgets`.

---

### Task 6: Text/Signature/UNKNOWN values + complete populated accessor semantics

**Files:** `src/pdf_form_common.c`, `src/pdf_form.c`, `tests/test_pdf_form.c`.

- [ ] Text helper accepts PDF string only, decodes with `pdf_to_text_string` after type check, copies immediately. Never call `pdf_field_value`. Stream/array/name/number/dict -> FORMAT.
- [ ] Text:

```text
missing V -> MISSING/0
V() -> PRESENT/one UTF8 size0
V(text) -> PRESENT/one UTF8
other -> FORMAT
```

- [ ] Signature ordinary value N/A. Missing V -> unsigned. V dict with no Type or Type/Sig -> signed. V non-dict or dict Type other -> FORMAT. PushButton/UNKNOWN -> N/A/0 values; UNKNOWN does not guess V semantics.
- [ ] Complete all public accessor contracts. Minimum sizes:

```c
offsetof(quantapdf_form_field_info, is_signed) + sizeof(info->is_signed)
offsetof(quantapdf_form_value_info, option_index) + sizeof(info->option_index)
offsetof(quantapdf_form_option_info, kind) + sizeof(info->kind)
offsetof(quantapdf_form_widget_info, button_option_index) + sizeof(info->button_option_index)
```

Info functions reset all known fields before later index validation, preserve caller `struct_size`, and never touch bytes beyond known fields. Larger-wrapper canary unchanged. `value_utf8` on OPTION -> UNSUPPORTED after reset. `option_export/display` on BUTTON_STATE -> UNSUPPORTED after reset. Out-of-range -> ARGUMENT.
- [ ] `scalar-values` uses `acroform-scalars.pdf`; checks missing/empty/text, `/TU()` present-empty, PushButton, unsigned/signed, UNKNOWN, bad Text V, stream V twice, bad signature, populated struct_size/canary/index/kind contracts.
- [ ] Run:

```bash
cmake --build build --parallel 2
./build/tests/quantapdf_test_pdf_form --case api-shell
./build/tests/quantapdf_test_pdf_form --case scalar-values
ctest --test-dir build --output-on-failure -E '^quantapdf\.pdf_form$'
```

- [ ] Commit `feat: materialize AcroForm scalar values`.

---

### Task 7: Choice `/Opt`, `/I`, Combo/List values

**Files:** `src/pdf_form_common.c`, `tests/test_pdf_form.c`.

- [ ] Effective `/Opt`: missing -> zero options; present array only. Entry string -> export=display; exactly two-string array -> separate export/display; otherwise FORMAT. Copy present-empty correctly.
- [ ] Effective `/I` when present: array of integer indices; each non-negative, in range, unique, cardinality-compatible; preserve `/I` order for multi-select.
- [ ] `/V`: Combo/single List one string; multi List one string or array strings; empty array -> PRESENT/0; other -> FORMAT.
- [ ] With `/I`, map indices directly to OPTION and require export strings exactly agree with `/V`; contradiction -> FORMAT. Without `/I`, each V string must match exactly one export; duplicate ambiguous export -> FORMAT. Editable Combo only may expose unmatched one-string V as UTF8 custom value; non-editable unmatched -> FORMAT.
- [ ] `choice-values` exact expected:

```text
country options (US,United States),(JP,Japan), value OPTION(1)
city options Tokyo,Osaka, value UTF8(Kyoto)
size value OPTION(1)
colors values OPTION(0), OPTION(2) in /I order
bad Opt -> FORMAT
bad I -> FORMAT
```

- [ ] Run choice + old 19 and commit `feat: materialize AcroForm choice values`.

---

### Task 8: Button selected values + full main/lifetime/no-execution/atomicity + local 20/20

**Files:** `src/pdf_form_common.c`, `src/pdf_form.c` only if ownership correction is needed, `tests/test_pdf_form.c`.

- [ ] Checkbox/radio effective V:

```text
missing -> MISSING/0
Name /Off -> PRESENT/0
Name matching exactly one BUTTON_STATE -> PRESENT/OPTION(index)
non-Name/unmatched -> FORMAT
```

Never invent `/Yes`.

- [ ] Main fixture: verify all 17 field order/types/values; `agree` Yes option0/value0/both Widgets0; `payment` Visa0/Mastercard1/third Visa0/value1; choice strings; representative Widget global order/Fitz geometry/u32 F.
- [ ] Lifetime: extract two main snapshots, verify distinct/equal semantics, close source, keep reading names/labels/text/options/Widgets, drop independently.
- [ ] No normalization: stream-value extraction twice on same open source -> FORMAT/NULL twice.
- [ ] No behavior execution: JS fixture extraction twice -> marker SAFE and dependent UNCHANGED; production contains no JS/form-event calls.
- [ ] Late atomicity: late-malformed sentinel output -> FORMAT/NULL twice, no prefix.
- [ ] Static full suite:

```bash
cmake --build build --parallel 2
./build/tests/quantapdf_test_pdf_form --case button-values
./build/tests/quantapdf_test_pdf_form --case lifetime
ctest --test-dir build --output-on-failure
```

Expected 20/20.

- [ ] Sanitizer exact CI shape:

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

Expected 20/20.

- [ ] Commit `feat: complete immutable AcroForm snapshot`; freeze resulting feature SHA. Any later feature fix restarts proof on the new SHA.

---

### Task 9: Exact-head PR proof/full-ci/review, then STOP

**Files:** no production changes expected; PR/#43/roadmap evidence only after results.

- [ ] Ordinary PR run on frozen head: Linux static 20/20 + sanitizer 20/20. If master moved, inspect synthetic merge proof; no silent rebase. Any feature commit change invalidates old proof.
- [ ] Apply `full-ci` label without changing feature SHA. Require same-head Linux static 20/20, Linux sanitizer 20/20, macOS 20/20, Windows DLL 20/20. Windows logs must show `pdf_object_common.c`, `pdf_form_common.c`, `pdf_form.c` -> `quantapdf.dll`, Forms executable, Forms test 20/20.
- [ ] Base->head scope allowed only:

```text
docs/superpowers/specs/2026-08-29-quantapdf-acroform-snapshot-design.md
docs/superpowers/plans/2026-08-29-quantapdf-acroform-snapshot.md
include/quantapdf/quantapdf.h
CMakeLists.txt
src/pdf_object_common.h
src/pdf_object_common.c
src/pdf_annotation_common.h (only if include wiring changed)
src/pdf_annotation_common.c
src/pdf_form_common.h
src/pdf_form_common.c
src/pdf_form.c
tests/CMakeLists.txt
tests/test_pdf_form.c
tests/fixtures/acroform-*.pdf
```

- [ ] Fresh Critical/Important review: `/T` group semantics, no repair, missing/empty, Choice `/I`/duplicate export, button no invented Yes, Widget reconciliation/order/geometry/u32, Signature/UNKNOWN, no JS/events, reset/struct_size/ownership/lifetime, atomic publication. Any blocker -> fix commit -> repeat proof.
- [ ] Record RED SHA/workflow, final GREEN SHA/workflow, same-head full-ci ID, platform counts, scope/review. Roadmap #2 -> `AcroForm Snapshot V1 — implementation proven; integration authorization pending`.

**STOP. Do not mark ready, merge, close #43, or create Form Value Mutation V1.**

---

### Task 10: Integration only after explicit authorization

**Files:** GitHub state/evidence only.

- [ ] Immediately re-fetch PR head/base, reviews/comments/threads and full-ci. Head must equal the frozen feature SHA recorded in Task 9; no Critical/Important blocker.
- [ ] Mark ready and merge method `merge` with `expected_head_sha` equal to that exact recorded frozen SHA. If draft->ready connector still hits the known upstream GraphQL schema mismatch, create a non-draft carrier branch pointing directly to the same frozen SHA, create no feature commit, re-verify base/head/reviews, and expected-head merge the carrier; document the workaround.
- [ ] Find master `push` run with `head_sha` exactly the merge commit. Require Linux static+sanitizer 20/20 each, macOS 20/20, Windows DLL 20/20; fetch Windows log and confirm Forms sources/DLL/executable/test20.
- [ ] Only then add integration evidence to #43, close `completed`, roadmap #2 -> `AcroForm Snapshot V1 — integrated`, and update canonical PR body. A separate Form Value Mutation V1 issue may be created only in a later user-authorized task.
