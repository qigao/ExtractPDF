# ExtractPDF CropBox V1 Matrix Correction Plan

> **For agentic workers:** This is a normative correction checkpoint inside the existing inline execution of `docs/superpowers/plans/2026-08-29-extractpdf-cropbox-transform.md`. Do not resume production changes until the correction spec is approved.

**Goal:** Correct the internal page-transform direction from the disproven `Fitz -> PDF` assumption to the behaviorally proven `PDF -> Fitz/public` contract, then re-establish Task 4 GREEN without changing the public API or preservation scope.

**Correction Spec:** `docs/superpowers/specs/2026-08-29-extractpdf-cropbox-transform-matrix-correction.md`

**Current RED evidence:** feature head before correction docs `582604199ec2557738900266cb5fc0b4131755cc`; workflow #302 / run `33248344569`; all 22 executables built, CTests #1-#21 passed, #22 failed at inherited-CropBox no-op.

## Constraints

- Keep PR #50 draft.
- Do not change public ABI.
- Do not expand beyond CropBox V1.
- Do not edit workflow YAML.
- Do not modify the isolated writer except where matrix/plan data names must align with the corrected invariant.
- Existing source immutability/security/preservation rules remain unchanged.
- Task 4 remains RED until the inherited no-op/materialization regression and primary interactive preservation test both pass.
- Do not begin Task 5 until Task 4 is static + ASan/UBSan GREEN on one exact feature SHA.

---

### Correction Task 1: Rename the stored matrix to its real direction

**Files:**
- Modify: `src/pdf_crop_internal.h`
- Modify: `src/pdf_crop_preflight.c`
- Modify: `src/pdf_crop.c` only if field references require the rename

**Required interface correction:**

Replace the misleading field:

```c
fz_matrix public_to_pdf;
```

with:

```c
fz_matrix pdf_to_public;
```

The stored value is exactly the matrix returned by:

```c
pdf_page_obj_transform(ctx, page_obj, NULL, &view->pdf_to_public);
```

No inverse is stored in the page view.

**Gate:** search the CropBox production sources and prove there is no remaining `public_to_pdf` field/name referring to MuPDF's returned matrix.

---

### Correction Task 2: Derive public visible bounds in the correct direction

**File:**
- Modify: `src/pdf_crop_preflight.c`

After strict raw box resolution:

```c
view->visible_pdf = intersection(view->media_pdf, view->crop_pdf);
```

compute:

```c
pdf_page_obj_transform(ctx, page_obj, NULL, &view->pdf_to_public);
public_visible = fz_transform_rect(view->visible_pdf, view->pdf_to_public);
```

Normalize `public_visible`, verify all coordinates are finite and it has positive width/height, then publish it as `view->visible_public`.

Do not use `pdf_page_obj_transform()`'s `outbox` as the public rectangle.

**Expected inherited regression observation:**

```text
raw visible [10,20,390,280] -> public [0,0,380,260]
```

---

### Correction Task 3: Invert only for caller request mapping

**File:**
- Modify: `src/pdf_crop_preflight.c`

For each already validated public request:

```c
fz_matrix public_to_pdf = fz_invert_matrix(view.pdf_to_public);
requested_pdf = fz_transform_rect(public_rect, public_to_pdf);
```

Normalize `requested_pdf` before raw containment checks and before storing it in the plan.

For the inherited fixture, lock:

```text
public [20,10,360,250] -> raw [30,30,370,270]
```

For the primary unrotated `[0,0,400,300]` fixture, existing raw expectations remain:

```text
page 0 public [50,40,350,260] -> raw [50,40,350,260]
page 1 public [20,30,380,270] -> raw [20,30,380,270]
```

The numerical equality in the primary fixture is incidental to its origin/axis symmetry; it must not be used as evidence for matrix direction.

---

### Correction Task 4: Re-run Task 4 proof on one exact head

No new fixture is added; the existing inherited fixture is the regression.

Run through PR CI on the exact corrected head. Require:

```text
Linux static build      GREEN
CTest                    22/22 GREEN
Linux ASan/UBSan build  GREEN
ASan/UBSan CTest         22/22 GREEN
```

The CropBox test must prove:

- source observation valid;
- source remains immutable;
- primary two-page changed batch succeeds;
- raw changed CropBoxes match expected values;
- text/image/link/annotation/widget geometry follows page-space mapping;
- URI/form value/outline semantics remain unchanged;
- internal link/outline target coordinates follow cropped target page mapping;
- output survives source close;
- inherited no-op does not materialize local CropBox;
- inherited changed crop materializes `[30,30,370,270]`;
- raw preservation helper passes.

**Task 4 correction gate:** if any failure remains, use systematic debugging and do not proceed to Task 5.

---

### Correction Task 5: Resume the original plan

Only after Correction Task 4 is GREEN:

1. record exact-head Task 4 evidence in PR #50 and issue #49;
2. resume original Task 5 Rotate/UserUnit/effective-box test-first RED;
3. apply the same corrected `PDF -> public`, inverse-for-request rule to Rotate/UserUnit support;
4. keep Task 7 explicit integration STOP unchanged.

No merge, PR-ready transition, issue closure, or next Phase 6 slice is authorized by this correction plan.
