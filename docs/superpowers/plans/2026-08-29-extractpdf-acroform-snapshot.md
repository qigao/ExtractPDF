# ExtractPDF AcroForm Snapshot V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a strict, immutable, document-independent AcroForm snapshot that exposes logical field groups, typed values/options, and reconciled page Widget instances without mutating the source PDF or executing PDF behavior.

**Architecture:** Parse raw AcroForm objects into a private deep-owned semantic model in `pdf_form_common.c` after strict Parent/Kids/group preflight, validated inheritance, typed value normalization, and page-Widget reconciliation. `pdf_form.c` owns only public snapshot publication/accessors. A tiny shared raw-object helper keeps Widget Rect/F behavior identical to annotation Rect/F behavior without coupling Forms to annotation classification.

**Tech Stack:** C11, MuPDF 1.28.2, pinned vcpkg commit `f74a2eade17a628413746557d04db25ccf6e76f9`, CMake 3.20+, CTest, Linux `-Wall -Wextra -Wpedantic -Werror`, Linux ASan/UBSan, macOS, Windows shared-library build with MSVC `/W4 /WX`.

**Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-acroform-snapshot-design.md`

## Global Constraints

- Integrated base is `0e4b769753215725797a557c4f18c4654e444e30`; final self-reviewed design is `d586aceb3837ba8169af67470fd81ffb4a75a867`.
- Scope is issue #43 only. Do not create Form Value Mutation V1, field refs/setters, XFA support, field/Widget structural mutation, JavaScript/event execution, signing, NeedAppearances rewriting, or flattening.
- Public logical objects are field **groups**: local `/T` creates a logical-name boundary; unnamed descendants remain in the nearest group.
- Public field/widget/option indices are snapshot-local coordinates only.
- Valid PDF with no `/AcroForm`, no `/Fields`, or empty `/Fields` -> `OK` plus non-NULL empty snapshot.
- Non-PDF -> `UNSUPPORTED`, with output reset to NULL.
- Any failure publishes no snapshot and no valid prefix.
- Never repair Parent/Kids, normalize/write `/V`, dirty fields, update appearances, calculate forms, or execute PDF JavaScript/actions/events.
- Do not define immutable semantics with `pdf_field_value()` or a repairing inheritable-parent walker; use the already-validated private Parent graph.
- Widget order is page index ascending, then raw page `/Annots` order. Missing/non-array `/Annots` is empty; non-dictionary members are ignored.
- Every published Widget is reachable from exactly one terminal field group and appears exactly once on exactly one page. Direct Widget dictionaries cannot satisfy cross-structure identity and are `FORMAT`.
- Widget `/Rect` uses the existing PDF-user-space -> Fitz page-space transform. Widget `/F` and field `/Ff` preserve full non-negative `uint32_t` range.
- Snapshot strings are deep-owned; public string accessors return borrowed pointers until `extractpdf_drop_form()`.
- String tri-state is exact: missing `NULL/0`; present-empty non-NULL/0; present value non-NULL/size.
- Public structs follow the existing minimum-known `struct_size` rule: too small `ARGUMENT`; larger accepted; trailing caller bytes untouched.
- Suite progression is exactly 19 -> 20 CTests.
- Final feature proof: exact-head Linux static 20/20 + ASan/UBSan 20/20, then same-SHA `full-ci` macOS 20/20 + Windows DLL 20/20.
- No merge/close before explicit integration authorization and integrated-master push proof.

---

## File Map

**Create**
- `src/pdf_object_common.h`
- `src/pdf_object_common.c`
- `src/pdf_form_common.h`
- `src/pdf_form_common.c`
- `src/pdf_form.c`
- `tests/test_pdf_form.c`
- deterministic `tests/fixtures/acroform-*.pdf`

**Modify**
- `include/extractpdf/extractpdf.h`
- `src/pdf_annotation_common.c`
- `src/pdf_annotation_common.h` only if include wiring requires it
- `CMakeLists.txt`
- `tests/CMakeLists.txt`

**Do not modify**
- `.github/workflows/ci.yml`
- `src/pdf_edit*`
- unrelated render/text/image/link/outline/metadata/composition behavior

---

### Task 1: Define the complete strict RED surface

**Files:**
- Create: `tests/test_pdf_form.c`
- Create: deterministic `tests/fixtures/acroform-*.pdf`
- Modify: `tests/CMakeLists.txt`
- Do not modify production headers/sources

**Interfaces:**
- Consumes: current 19-test master API.
- Produces: one wished-for twentieth CTest that compile-references every approved Forms declaration and therefore fails because the Forms ABI does not yet exist.

- [ ] **Step 1: Generate checked-in PDFs with a one-shot offset-safe writer**

Use a temporary local Python script, not committed and never required by CTest:

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

All valid pages use `/MediaBox [0 0 200 200]` and no rotation so `[x0 y0 x1 y1]` maps predictably to Fitz `[x0, 200-y1, x1, 200-y0]`.

Create these **layered valid fixtures** so each later task can become GREEN without depending on semantics implemented in a later task:

1. `acroform-structure.pdf` — no Widgets and no non-empty `/V`; public DFS fields are:

```text
0 profile.nickname  TEXT  named parent `profile` supplies /FT /Ff /TU; named child supplies /T (nickname)
1 repeat            TEXT  one /T boundary with two unnamed descendant field nodes; no second public field
2 <missing name>    TEXT  anonymous top-level group
3 <present-empty>   TEXT  top-level local /T ()
```

All four values are missing. This fixture proves grouping, names, labels, type/flag inheritance, anonymous name, and present-empty name without requiring Widget/value normalization.

2. `acroform-widgets.pdf` — only fields whose value is missing, so Widget work is isolated. Include:

```text
field 0 textWidget TEXT      one Widget
field 1 agree      CHECKBOX  two Widgets; each /AP/N has /Off and /Yes; no /V
field 2 payment    RADIO     three Widgets: Visa, Mastercard, Visa; no /V
field 3 future     UNKNOWN   one Widget
```

Use this exact global page/raw-Annots order:

```text
page 0: textWidget [10 170 40 190], agree-A [50 170 70 190], payment-Visa-A [80 170 100 190]
page 1: agree-B [10 170 30 190] with /F 2147483649, payment-Master [40 170 60 190], future [70 170 100 190]
page 2: payment-Visa-B [10 170 30 190]
```

Include one scalar and one ordinary non-Widget annotation in `/Annots`; both are ignored.

3. `acroform-scalars.pdf` — no Choice/button selected values. Public fields:

```text
textMissing   TEXT       no /V
textEmpty     TEXT       /V ()
textValue     TEXT       /V (hello), /TU ()
submit        PUSHBUTTON
sigUnsigned   SIGNATURE  no /V
sigSigned     SIGNATURE  /V << /Type /Sig >>
future        UNKNOWN    /FT /Future
```

4. `acroform-choice.pdf` — only Choice fields:

```text
country COMBO  /Ff 131072; /Opt [[(US)(United States)] [(JP)(Japan)]]; /V (JP); /I [1]
city    COMBO  /Ff 393216; /Opt [(Tokyo) (Osaka)]; /V (Kyoto)
size    LIST   /Opt [(S) (M) (L)]; /V (M); /I [1]
colors  LIST   /Ff 2097152; /Opt [[(r)(Red)] [(g)(Green)] [(b)(Blue)]]; /V [(r)(b)]; /I [0 2]
```

5. `acroform-main.pdf` — final three-page combined fixture with seventeen public terminal fields in this exact order:

```text
0  profile.nickname  TEXT          inherits value "Ada" and label "Profile label" from parent
1  repeat            TEXT          unnamed appearance-only descendants; value "same"; 2 Widgets
2  <missing name>    TEXT          value "anon"
3  <present-empty>   TEXT          /T (); /V ()
4  textMissing       TEXT          no /V
5  textEmpty         TEXT          /V ()
6  textValue         TEXT          /V (hello); /TU (); 1 Widget
7  agree             CHECKBOX      /V /Yes; two /Yes Widgets
8  payment           RADIO         /Ff 32768; /V /Mastercard; Visa/Mastercard/Visa Widgets
9  country           COMBO         same options/value as choice fixture
10 city              COMBO|EDIT    custom /V (Kyoto)
11 size              LIST          selected M
12 colors            MULTI LIST    selected r,b
13 submit            PUSHBUTTON
14 sigUnsigned       SIGNATURE
15 sigSigned         SIGNATURE      /V << /Type /Sig >>
16 future            UNKNOWN        /FT /Future
```

Final main Widget order:

```text
page 0: repeat-A, agree-A, payment-Visa-A, country, submit
page 1: repeat-B, textValue, agree-B(/F=2147483649), payment-Master, city, sigUnsigned
page 2: payment-Visa-B, size, colors, sigSigned, future
```

Button option order from this order is `agree: Yes=0`; `payment: Visa=0, Mastercard=1`, with the later Visa deduplicating to option 0.

6. `acroform-no-fields.pdf` — AcroForm dictionary with no `/Fields`.
7. `acroform-empty-fields.pdf` — `/Fields []`.
8. `acroform-annots-nonarray.pdf` — one valid Text field with zero Widgets; page `/Annots 17`; succeeds.
9. `acroform-js.pdf` — fields `marker=SAFE`, `dependent=UNCHANGED`, plus document OpenAction JS and field AA Keystroke/Validate/Calculate/Format JS that would change those values if executed; repeated snapshots must remain SAFE/UNCHANGED.
10. `acroform-stream-value.pdf` — Text `/V` is an indirect stream; two consecutive extractions on the same document both return FORMAT.

Create these **strict failure fixtures**, each with all unrelated structures valid:

```text
acroform-bad-root.pdf           /AcroForm integer                                  -> FORMAT
acroform-bad-fields.pdf         /Fields integer                                    -> FORMAT
acroform-bad-kid.pdf            /Kids includes scalar                              -> FORMAT
acroform-cycle.pdf              Parent/Kids cycle                                  -> FORMAT
acroform-repeated-node.pdf      same field object reached twice                    -> FORMAT
acroform-parent-mismatch.pdf    child /Parent != traversed parent                  -> FORMAT
acroform-root-parent.pdf        top-level field has external /Parent               -> FORMAT
acroform-depth-257.pdf          otherwise-valid depth 257                          -> UNSUPPORTED
acroform-mixed-group.pdf        one group owns Widget and named child group         -> FORMAT
acroform-group-conflict.pdf     unnamed same-group child conflicts on effective /V  -> FORMAT
acroform-duplicate-name.pdf     distinct public fields same non-empty full name     -> FORMAT
acroform-period-name.pdf        partial /T contains '.'                            -> FORMAT
acroform-bad-ft.pdf             terminal effective /FT is not Name                 -> FORMAT
acroform-bad-ff.pdf             effective /Ff = 4294967296                         -> FORMAT
acroform-bad-value.pdf          Text /V integer                                    -> FORMAT
acroform-bad-opt.pdf            Choice /Opt malformed entry                        -> FORMAT
acroform-bad-i.pdf              /I [1] contradicts /V selecting option 0           -> FORMAT
acroform-bad-button-ap.pdf      /AP/N has /Off plus two non-Off states             -> FORMAT
acroform-orphan-widget.pdf      page Widget not reachable from Fields              -> FORMAT
acroform-missing-widget.pdf     field-tree Widget absent from page Annots          -> FORMAT
acroform-duplicate-widget.pdf   same Widget ref appears twice                      -> FORMAT
acroform-p-mismatch.pdf         actual page 0 but Widget /P -> page 1              -> FORMAT
acroform-direct-widget.pdf      reconciled Widget is direct dict                   -> FORMAT
acroform-bad-widget-rect.pdf    Widget /Rect length != 4                           -> FORMAT
acroform-bad-widget-flags.pdf   Widget /F negative or >UINT32_MAX                  -> FORMAT
acroform-bad-signature.pdf      Signature /V dict /Type /NotSig                    -> FORMAT
acroform-late-malformed.pdf     first Text valid, later field malformed /Ff         -> FORMAT/no prefix
```

- [ ] **Step 2: Write `tests/test_pdf_form.c` against the approved ABI**

Use only `<extractpdf/extractpdf.h>` for library API. Copy the existing `CHECK` style. Add a case dispatcher so intermediate tasks can run an isolated contract:

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

The file compile-references every approved enum, info struct, accessor, `extractpdf_document_form()`, and `extractpdf_drop_form()` from the design. `test_api_shell()` covers NULL/reset behavior that does not require a populated field. Populated `struct_size`, index, kind-specific accessor and trailing-canary checks live in `test_scalar_values_and_full_api()` so Task 3 can become independently GREEN.

- [ ] **Step 3: Register the twentieth target**

Append `extractpdf_test_pdf_form`, CTest name `extractpdf.pdf_form`, timeout 60, all fixture path compile definitions, and add the target to the existing Windows shared-DLL copy `foreach` list. Include macros for:

```text
ACROFORM_STRUCTURE_PDF
ACROFORM_WIDGETS_PDF
ACROFORM_SCALARS_PDF
ACROFORM_CHOICE_PDF
ACROFORM_MAIN_PDF
ACROFORM_NO_FIELDS_PDF
ACROFORM_EMPTY_FIELDS_PDF
ACROFORM_ANNOTS_NONARRAY_PDF
ACROFORM_JS_PDF
ACROFORM_STREAM_VALUE_PDF
all strict-failure acroform fixture paths
NON_PDF=composition-non-pdf.txt
NO_ACROFORM_PDF=one-page.pdf
```

- [ ] **Step 4: Prove strict compile RED and old 19/19**

Configure exactly like Linux CI:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_OVERLAY_PORTS="$PWD/vcpkg-ports" \
  -DBUILD_SHARED_LIBS=OFF
```

Build old targets explicitly and run old CTests:

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

Expected: 19/19 pass.

Then:

```bash
cmake --build build --target extractpdf_test_pdf_form --parallel 2
```

Expected: compile failure naming absent approved Forms declarations such as `extractpdf_form`, `extractpdf_document_form`, or `EXTRACTPDF_FORM_FIELD_TEXT`. Fixture/runtime/link failure is not valid RED.

- [ ] **Step 5: Commit and capture remote RED**

```bash
git add tests/test_pdf_form.c tests/CMakeLists.txt tests/fixtures/acroform-*.pdf
git commit -m "test: define AcroForm snapshot red"
```

Open a draft PR to `master`, reference #43 + design + plan, and record exact RED SHA. Ordinary Linux PR CI should fail on the new target's missing ABI. Do not label `full-ci` while RED.

---

### Task 2: Extract shared raw PDF observation helpers

**Files:**
- Create: `src/pdf_object_common.h`, `src/pdf_object_common.c`
- Modify: `src/pdf_annotation_common.c`
- Modify: `src/pdf_annotation_common.h` only if needed for includes
- Modify: `CMakeLists.txt`
- Test: existing 19 only; Forms remains compile RED

**Interfaces:**

```c
int extractpdf_pdf_dict_find(
    fz_context *ctx, pdf_obj *dictionary, pdf_obj *key, pdf_obj **out_value);

extractpdf_status extractpdf_pdf_read_rect(
    fz_context *ctx, pdf_obj *dictionary, pdf_obj *key,
    fz_matrix page_ctm, extractpdf_rect *out_rect);

extractpdf_status extractpdf_pdf_read_optional_uint32(
    fz_context *ctx, pdf_obj *dictionary, pdf_obj *key,
    uint32_t missing_value, uint32_t *out_value);
```

`read_rect` preserves current annotation semantics exactly: required key, four finite numbers, raw endpoint normalization, supplied transform, finite transformed endpoints, transformed normalization. `read_optional_uint32`: missing -> supplied default; present must be integer `[0,UINT32_MAX]`.

- [ ] **Step 1: Move current annotation dictionary/Rect/F logic into the shared helper**
- [ ] **Step 2: Make annotation common call the helper without changing subtype/Contents behavior**
- [ ] **Step 3: Add `src/pdf_object_common.c` to `add_library(extractpdf ...)`**
- [ ] **Step 4: Rebuild/run old 19 exactly as Task 1; then confirm Forms still fails only on absent ABI**
- [ ] **Step 5: Commit**

```bash
git add src/pdf_object_common.h src/pdf_object_common.c \
  src/pdf_annotation_common.c src/pdf_annotation_common.h CMakeLists.txt
git commit -m "refactor: share strict PDF object readers"
```

---

### Task 3: Add public ABI plus empty/non-PDF snapshot shell

**Files:**
- Create: `src/pdf_form_common.h`, `src/pdf_form_common.c`, `src/pdf_form.c`
- Modify: `include/extractpdf/extractpdf.h`, `CMakeLists.txt`
- Test: `--case api-shell`, `--case empty`, old 19

**Interfaces:**

Public declarations are copied verbatim from the approved design:

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

Private parser API:

```c
typedef struct extractpdf_pdf_form_model extractpdf_pdf_form_model;
extractpdf_status extractpdf_pdf_form_parse(
    fz_context *ctx, pdf_document *document,
    extractpdf_pdf_form_model **out_model);
void extractpdf_pdf_form_drop_model(extractpdf_pdf_form_model *model);
```

Private deep model uses C-heap arrays and one string arena; no `pdf_obj *`, `pdf_page *`, or borrowed MuPDF string survives parse return. Records:

```c
typedef struct extractpdf_pdf_form_string {
    size_t offset, size;
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
    int is_multiselect, is_signed;
    extractpdf_pdf_form_string name, label;
} extractpdf_pdf_form_field;

typedef struct extractpdf_pdf_form_widget {
    size_t field_index;
    int page_index;
    extractpdf_rect bounds;
    uint32_t flags;
    size_t button_option_index;
} extractpdf_pdf_form_widget;
```

- [ ] **Step 1: Add exact public ABI and all symbols**

Add the opaque handle, enums/info structs, `extractpdf_document_form`, field/name/label/value/option/widget accessors, `extractpdf_drop_form`. No mutation declarations.

- [ ] **Step 2: Implement overflow-safe model/string-array grow/drop helpers**

All appended strings copy bytes plus NUL; present-empty still allocates/stores a stable non-NULL arena location.

- [ ] **Step 3: Implement only the empty/non-PDF parser boundary**

`extractpdf_document_form` resets output, validates document, obtains `pdf_document_from_fz_document`, calls private parse, and publishes wrapper only after complete success.

Task-3 parse behavior:

```text
no AcroForm -> empty model
AcroForm present non-dict -> FORMAT
AcroForm dict/no Fields -> empty model
Fields present non-array -> FORMAT
Fields [] -> empty model
non-empty Fields -> temporary UNSUPPORTED staging boundary
```

- [ ] **Step 4: Implement all public symbols over empty model with correct resets**

Count functions reset count first. String functions reset pointer/size. Info functions validate minimum known size and reset known fields/sentinels. Populated kind-specific behavior is finalized in Task 6.

- [ ] **Step 5: GREEN commands**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case api-shell
./build/tests/extractpdf_test_pdf_form --case empty
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: two Forms cases pass; old 19/19 pass.

- [ ] **Step 6: Commit**

```bash
git add include/extractpdf/extractpdf.h src/pdf_form_common.h \
  src/pdf_form_common.c src/pdf_form.c CMakeLists.txt
git commit -m "feat: add AcroForm snapshot ABI shell"
```

---

### Task 4: Implement field graph, logical groups, validated inheritance, names/labels

**Files:** `src/pdf_form_common.[ch]`, `tests/test_pdf_form.c`

**Interfaces:** produces terminal field array in exact DFS group order. No Widgets are required for the positive fixture.

- [ ] **Step 1: Build transient field-node graph**

Use borrowed objects only during parse:

```c
typedef struct extractpdf_pdf_form_node {
    pdf_obj *raw_object;
    pdf_obj *resolved_dict;
    size_t parent_node, group_index, depth;
    int has_local_t;
    int is_widget;
} extractpdf_pdf_form_node;
```

Private object identity: indirect objects compare num+gen; direct dictionaries require same resolved pointer. Do not deep-equal distinct direct dictionaries into one identity.

Traversal order: repeated/cycle check first; new node depth >256 -> UNSUPPORTED; `/Kids` if present must array; every child dict; child `/Parent` exactly traversed parent; top-level external `/Parent` forbidden.

- [ ] **Step 2: Assign groups by `/T` boundaries**

```text
top-level node -> starts a group
child with local /T present, including empty -> new child group
child without local /T -> remains in parent group
```

Every observed `/T` must be PDF string. Partial `/T` containing literal `.` -> FORMAT. Group with Widget(s) and named child group(s) -> FORMAT. Publish only groups with no named child group.

- [ ] **Step 3: Resolve effective keys through only the validated node chain**

Use:

```c
typedef struct extractpdf_pdf_form_effective {
    pdf_obj *value;
    size_t owner_node;
    int present;
} extractpdf_pdf_form_effective;
```

Provide validated effective lookup for `/FT`, `/Ff`, `/V`, `/TU`, `/Opt`, and `/I`. The owner node is retained transiently so same-group conflict checks and future parser reuse do not invent another inheritance model.

Same-group semantic consistency: participating nodes in one group must agree on effective `/FT`, `/Ff`, `/V`; conflicting effective `/Opt` or `/I` definitions that make option/selection semantics ambiguous -> FORMAT.

- [ ] **Step 4: Materialize type, flags, full name, label**

`/FT`: required on public terminal group; non-Name -> FORMAT; unknown Name -> UNKNOWN. `/Ff`: missing 0; present strict integer `[0,UINT32_MAX]`; contradictory type flags -> FORMAT.

Fully-qualified name joins only `/T` boundary components in ancestor order. Anonymous top-level -> missing. One empty component can yield present-empty. Distinct public fields with same non-empty final name -> FORMAT. `/TU` is effective label only; never fall back to `/T`.

For Task 4 positive fixtures all values are missing, so Text can materialize `MISSING/0`; UNKNOWN N/A. Choice/button non-missing value behavior is not needed yet.

- [ ] **Step 5: Run structure cases**

`test_structure()` validates `acroform-structure.pdf` exact 4-field order/name/label/type/flags plus bad root/fields/kid/cycle/repeated/parent/root-parent/depth/mixed-group/group-conflict/duplicate-name/period-name/bad-FT/bad-FF statuses.

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case structure
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

Expected: structure pass + old 19/19.

- [ ] **Step 6: Commit**

```bash
git add src/pdf_form_common.h src/pdf_form_common.c tests/test_pdf_form.c
git commit -m "feat: parse strict AcroForm field groups"
```

---

### Task 5: Reconcile page Widgets and normalize Widget/button-option structure

**Files:** `src/pdf_form_common.c`, `tests/test_pdf_form.c`

**Interfaces:** produces `model->widgets[]` in global page/Annots order, per-field `widget_count`, strict Fitz bounds/raw flags, BUTTON_STATE option table and Widget `button_option_index`. Button `/V` selection is completed in Task 8.

- [ ] **Step 1: Record field-tree Widget identities**

A field-tree dict with `/Subtype /Widget` belongs to current logical group and must be indirect for reconciliation. Record num/gen + group index. For checkbox/radio, inspect raw `/AP/N`: absent -> no usable state; present must dict with zero or one non-Off key; >1 non-Off -> FORMAT. Keep state bytes private.

- [ ] **Step 2: Scan all pages in ascending index**

Load/drop each page inside MuPDF try/always. Page `/Annots`: missing/non-array -> empty; array -> raw order. Ignore non-dicts and non-Widget subtypes. Widget dict must be indirect and match exactly one field-tree Widget; unmatched -> orphan FORMAT; already matched -> duplicate FORMAT. `/P` if present must identify actual page.

Compute page transform once, then use shared `read_rect(Rect)` and `read_optional_uint32(F,0)` and append Widget private record in page/raw order. After all pages every field-tree Widget must have matched once.

- [ ] **Step 3: Build field-local BUTTON_STATE options in public Widget order**

For each checkbox/radio Widget with one non-Off state, bytewise-deduplicate within owning field; first occurrence appends BUTTON_STATE; set Widget option index. Non-button and valid no-state Widget -> `SIZE_MAX`.

- [ ] **Step 4: Run Widget cases**

`test_widgets()` uses `acroform-widgets.pdf` for exact order, ownership, Fitz geometry, radio option first-seen/dedup order, and `/F=2147483649`. It also checks annots-nonarray success and bad AP/orphan/missing/duplicate/P/direct/Rect/F fixtures -> FORMAT.

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case widgets
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

- [ ] **Step 5: Commit**

```bash
git add src/pdf_form_common.c tests/test_pdf_form.c
git commit -m "feat: reconcile AcroForm widgets"
```

---

### Task 6: Materialize Text/Signature/UNKNOWN and finish populated public accessor contracts

**Files:** `src/pdf_form_common.c`, `src/pdf_form.c`, `tests/test_pdf_form.c`

- [ ] **Step 1: Strict text-string helper**

Accept only PDF string; decode with `pdf_to_text_string` after type validation; copy immediately to model arena. Never call `pdf_field_value`. Text stream/array/name/number/dict -> FORMAT.

- [ ] **Step 2: Text value normalization**

```text
no effective /V -> MISSING/0
/V () -> PRESENT/one UTF8 present-empty
/V text -> PRESENT/one UTF8
other -> FORMAT
```

- [ ] **Step 3: Signature/PushButton/UNKNOWN**

Signature ordinary value is N/A. Missing `/V` -> unsigned. `/V` dict with absent `/Type` or `/Type /Sig` -> signed. Non-dict or dict with non-Sig `/Type` -> FORMAT. PushButton and UNKNOWN -> N/A/0 options/0 values; do not guess UNKNOWN `/V`.

- [ ] **Step 4: Finish populated accessor mechanics**

`field_get_info`, `value_get_info`, `option_get_info`, `widget_get_info` use minimum-known `struct_size`; too small ARGUMENT; larger accepted and trailing canary unchanged. Reset all known fields before later index validation. `value_utf8` only for UTF8; OPTION -> UNSUPPORTED after reset. `option_export/display` only for CHOICE; BUTTON_STATE -> UNSUPPORTED after reset. Out-of-range indices -> ARGUMENT.

- [ ] **Step 5: Run scalar/full-API cases**

`test_scalar_values_and_full_api()` uses `acroform-scalars.pdf` for missing/empty/text, label present-empty, PushButton, signature states, UNKNOWN, plus bad Text value, stream value twice, bad signature, struct-size/trailing-canary and kind/index errors.

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case api-shell
./build/tests/extractpdf_test_pdf_form --case scalar-values
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

- [ ] **Step 6: Commit**

```bash
git add src/pdf_form_common.c src/pdf_form.c tests/test_pdf_form.c
git commit -m "feat: materialize AcroForm scalar values"
```

---

### Task 7: Implement Choice options, `/I` identity, Combo/List values

**Files:** `src/pdf_form_common.c`, `tests/test_pdf_form.c`

- [ ] **Step 1: Parse effective `/Opt` strictly**

Missing -> option_count 0. Present must array. Each entry:

```text
PDF string -> export=display=decoded string
array exactly [PDF string, PDF string] -> separate export/display
anything else -> FORMAT
```

Copy both strings including present-empty.

- [ ] **Step 2: Parse effective `/I` strictly when present**

Must array of integers. Every index non-negative, `< option_count`, unique, and cardinality-compatible with field type/flags. Preserve `/I` order as selected-value order for multi-select.

- [ ] **Step 3: Reconcile `/V` with `/I`**

Combo/single List accepts one PDF string. Multi List accepts one string or array of strings; empty array -> PRESENT/0. Other types -> FORMAT.

If `/I` exists, map indices directly to OPTION and require corresponding export strings exactly match `/V`. Contradiction -> FORMAT. Without `/I`, each `/V` string must match exactly one export; duplicate ambiguous match -> FORMAT. Editable Combo only may turn one unmatched string into UTF8 custom value; non-editable unmatched -> FORMAT.

- [ ] **Step 4: Run Choice case**

Expected from `acroform-choice.pdf`:

```text
country: options (US,United States),(JP,Japan); value OPTION(1)
city: options Tokyo,Osaka; value UTF8("Kyoto")
size: OPTION(1)
colors: OPTION(0), OPTION(2) in /I order
```

Bad Opt and bad I -> FORMAT.

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case choice-values
ctest --test-dir build --output-on-failure -E '^extractpdf\.pdf_form$'
```

- [ ] **Step 5: Commit**

```bash
git add src/pdf_form_common.c tests/test_pdf_form.c
git commit -m "feat: materialize AcroForm choice values"
```

---

### Task 8: Finish button values, full fixture, lifetime/no-execution/atomicity, local 20/20

**Files:** `src/pdf_form_common.c`, `src/pdf_form.c` only for final ownership corrections, `tests/test_pdf_form.c`

- [ ] **Step 1: Normalize checkbox/radio effective `/V`**

```text
missing -> MISSING/0
Name /Off -> PRESENT/0
Name matching exactly one normalized field BUTTON_STATE -> PRESENT/OPTION(index)
non-Name or unknown state -> FORMAT
```

Do not invent `/Yes` when absent from the file.

- [ ] **Step 2: Assert full main semantics**

`agree`: one Yes option, value OPTION(0), both Widgets option 0. `payment`: Visa=0, Mastercard=1, third Visa=0; value OPTION(1). Verify all 17 field order/types/values, choice strings, representative Widget order/geometry/flags.

- [ ] **Step 3: Prove document-independent snapshots**

Extract two snapshots from main; assert distinct handles/equal semantics; close source; continue reading representative names, labels, text values, choice strings, options, Widgets; drop independently.

- [ ] **Step 4: Prove no source normalization and no PDF behavior execution**

On stream-value fixture, two extractions on same open document both FORMAT/NULL. On JS fixture, two snapshots both show marker SAFE and dependent UNCHANGED. Production code never enables/calls JS or form-event APIs.

- [ ] **Step 5: Prove late failure atomicity**

Late-malformed fixture: sentinel output -> FORMAT + NULL twice; no prefix handle.

- [ ] **Step 6: Run full static and sanitizer suites**

```bash
cmake --build build --parallel 2
./build/tests/extractpdf_test_pdf_form --case button-values
./build/tests/extractpdf_test_pdf_form --case lifetime
ctest --test-dir build --output-on-failure
```

Expected 20/20.

Then exact CI sanitizer shape:

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

- [ ] **Step 7: Commit and freeze feature SHA**

```bash
git add src/pdf_form_common.c src/pdf_form.c tests/test_pdf_form.c
git commit -m "feat: complete immutable AcroForm snapshot"
```

No further feature commit after cross-platform proof begins unless a failure requires a new fix and full proof restart.

---

### Task 9: Exact-head feature verification/full-ci/scope review, then STOP

**Files:** no production changes expected; PR/#43/roadmap evidence only after results.

- [ ] **Step 1: Require ordinary PR Linux success on frozen head**

Confirm PR head is frozen feature SHA. Require Linux static 20/20 and sanitizer 20/20. If master moved, inspect GitHub synthetic merge proof; do not silently rebase. Any feature commit change invalidates prior evidence.

- [ ] **Step 2: Apply `full-ci` label without changing SHA**

Require one same-head run:

```text
Linux static      20/20
Linux ASan/UBSan  20/20
macOS             20/20
Windows DLL       20/20
```

Windows log must show `pdf_object_common.c`, `pdf_form_common.c`, `pdf_form.c` compiling into `extractpdf.dll`, Forms test executable building, and `extractpdf.pdf_form` running as test 20/20.

- [ ] **Step 3: Review exact base->head scope**

Allowed paths only:

```text
docs/superpowers/specs/2026-08-29-extractpdf-acroform-snapshot-design.md
docs/superpowers/plans/2026-08-29-extractpdf-acroform-snapshot.md
include/extractpdf/extractpdf.h
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

Reject unrelated subsystem changes.

- [ ] **Step 4: Fresh Critical/Important review against spec**

Explicitly check field-group `/T` semantics; no repair; missing/empty; Choice `/I` and duplicate export; button state without invented Yes; Widget reconciliation/order/geometry/u32; Signature/UNKNOWN; no JS/events; public reset/struct_size/ownership/lifetime; atomic publication.

Any blocker -> new fix commit -> repeat all proof on new SHA.

- [ ] **Step 5: Record evidence and STOP**

Update PR/#43 with strict RED SHA/workflow, final GREEN SHA/workflow, same-head full-ci ID, platform counts, final scope/review. Update roadmap #2 to `AcroForm Snapshot V1 — implementation proven; integration authorization pending`.

**STOP. Do not mark ready, merge, close #43, or create Form Value Mutation V1.**

---

### Task 10: Integrate only after explicit authorization

**Files:** GitHub state/evidence only.

- [ ] **Step 1: Re-verify merge gate immediately before merge**

Fetch PR metadata, exact head/base, comments/reviews/threads, and full-ci. Head must equal the frozen feature SHA recorded in Task 9 Step 5; no unresolved Critical/Important blocker.

- [ ] **Step 2: Mark ready and expected-head merge**

Use merge method `merge` with `expected_head_sha` set to the exact frozen SHA recorded in Task 9 Step 5. If the draft->ready connector action still fails on the previously observed upstream GraphQL schema mismatch, create a non-draft integration carrier branch pointing directly at that exact SHA, create no new feature commit, re-verify base/head/review state, then merge the carrier with the same expected-head guard and record the workaround.

- [ ] **Step 3: Require integrated-master push proof**

Find the push workflow whose `head_sha` equals the merge commit. Require completed/success:

```text
Linux static + sanitizer  20/20 each
macOS                     20/20
Windows DLL               20/20
```

Fetch Windows logs and confirm Forms sources, DLL, test executable, and test 20/20.

- [ ] **Step 4: Close bookkeeping only after integrated proof**

Add integration evidence to #43, close reason `completed`, update roadmap #2 to `AcroForm Snapshot V1 — integrated`, update canonical PR with merge SHA + integrated run. Only after this may a separate Form Value Mutation V1 issue be created in a later user-authorized task.
