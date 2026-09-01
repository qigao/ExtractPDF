# QuantaPDF Composer Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a stable C ABI that generates bounded, deterministic PDFs from formatted base-14 text and JPEG/PNG images.

**Architecture:** A C facade owns validated page/resource/operation snapshots. A private C++ qpdf adapter converts that command list into a fresh PDF at finish time. The public builder remains backend-neutral and all caller buffers are copied.

**Tech Stack:** C11 public facade, C++20/qpdf writer backend, zlib PNG decoding, libjpeg-turbo JPEG validation, PDFium-based QuantaPDF read/render verification, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-01-quantapdf-composer-design.md`

---

### Task 1: Lock the ABI and lifecycle contract

**Files:**
- Create: `tests/test_composer.c`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/quantapdf/quantapdf.h`
- Create: `src/composer.c`
- Modify: `src/internal.h`
- Modify: `CMakeLists.txt`
- Modify: `abi/quantapdf-v2.exports`

1. Write tests that create/drop a composer, reject undersized records, validate null outputs, add valid/invalid pages, and prove copied-input ownership.
2. Configure/build the new test and record the expected compile/link failure because the API is absent.
3. Add opaque types, versioned records, enums, limits, and declarations without changing ABI version 2; bump the library feature version to 2.5.0.
4. Implement the bounded C facade and backend command snapshot declarations with transactional array growth.
5. Run `ctest --preset win-release-user -R quantapdf.composer` and commit.

### Task 2: Generate pages and formatted text

**Files:**
- Create: `src/backend/qpdf_composer.h`
- Create: `src/backend/qpdf_composer.cpp`
- Modify: `src/composer.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_composer.c`

1. Add a failing integration test for page count, dimensions, extracted wrapped text, color rendering, and alignment.
2. Implement a qpdf empty document, page dictionaries, resource dictionaries, and content streams.
3. Encode validated UTF-8 into WinAnsi/base-14 text, calculate base-font widths, wrap lines, align each line, escape PDF strings, and convert top-left coordinates.
4. Finish through qpdf memory output with deterministic IDs and copy bytes into `quantapdf_output`.
5. Run the focused test, then the full Release suite, and commit.

### Task 3: Add JPEG image resources and fitting

**Files:**
- Modify: `src/backend/qpdf_composer.cpp`
- Modify: `tests/test_composer.c`

1. Add failing tests that register a small JPEG, place it with contain/cover/stretch, reopen the output, and verify image metadata/rendered bounds.
2. Validate JPEG headers with libjpeg, copy encoded bytes, and create reusable DCT image XObjects.
3. Implement deterministic fit transforms and clipping for cover mode.
4. Test invalid/truncated images and repeated placement; run focused tests and commit.

### Task 4: Add PNG and alpha

**Files:**
- Modify: `src/backend/qpdf_composer.cpp`
- Modify: `tests/test_composer.c`

1. Add failing tests for opaque RGB PNG and RGBA PNG with a rendered alpha pixel assertion.
2. Parse bounded PNG chunks, inflate non-interlaced 8-bit scanlines, reverse filters, split alpha, and recompress image/soft-mask streams.
3. Reject unsupported bit depths, interlace, corrupt CRC/size arithmetic, and decoded data beyond the composer resource budget.
4. Run focused tests and commit.

### Task 5: Harden determinism, packaging, and documentation

**Files:**
- Modify: `tests/test_composer.c`
- Modify: `tests/test_version.c`
- Modify: `README.md`
- Modify: `THIRD_PARTY.md` if required
- Create: `docs/releases/v2.5.0.md`

1. Add failing tests for repeated-finish equality, post-finish mutation isolation, limits at capacity/capacity+1, invalid UTF-8, and malformed image inputs.
2. Make serialization order and IDs deterministic and close all rollback/leak paths.
3. Document the coordinate system, ownership, V1 limits, formatting examples, and unsupported complex scripts.
4. Build and test Release and MSVC ASan presets; install Release and compile/run an installed-package consumer.
5. Run ABI export verification and inspect the branch diff for accidental dependencies or unstable public layouts.
6. Commit, request code review, address findings, then use the finishing-a-development-branch workflow.

