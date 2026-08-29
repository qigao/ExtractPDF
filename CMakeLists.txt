cmake_minimum_required(VERSION 3.20)
project(ExtractPDF VERSION 2.0.0 LANGUAGES C)

option(EXTRACTPDF_BUILD_TESTS "Build ExtractPDF tests" ON)

find_package(unofficial-libmupdf CONFIG REQUIRED)
if(NOT TARGET unofficial::libmupdf::libmupdf)
  message(FATAL_ERROR "unofficial-libmupdf did not provide unofficial::libmupdf::libmupdf")
endif()

add_library(extractpdf
  src/status.c
  src/document.c
  src/page.c
  src/render.c
  src/text.c
  src/structured_text.c
  src/search.c
  src/images.c
  src/image_bitmap.c
  src/links.c
  src/pdf_crop.c
  src/pdf_export.c
  src/pdf_output.c
  src/pdf_range.c
  src/pdf_merge.c
  src/output_file.c
  src/pdf_metadata.c
  src/pdf_outline.c
  src/pdf_object_common.c
  src/pdf_annotation_common.c
  src/pdf_annotations.c
  src/pdf_form_common.c
  src/pdf_form_widgets.c
  src/pdf_form_provenance_widgets.c
  src/pdf_form_values.c
  src/pdf_form_choices.c
  src/pdf_form.c
  src/pdf_edit.c
  src/pdf_edit_forms.c
  src/pdf_edit_form_preflight.c
  src/pdf_edit_form_values.c
  src/pdf_edit_form_buttons.c
  src/pdf_edit_form_choices.c
  src/pdf_edit_form_widgets.c
  src/pdf_edit_annotations.c)
add_library(ExtractPDF::ExtractPDF ALIAS extractpdf)
target_compile_features(extractpdf PUBLIC c_std_11)
target_include_directories(extractpdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(extractpdf PRIVATE unofficial::libmupdf::libmupdf)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(extractpdf
    PRIVATE EXTRACTPDF_BUILDING_LIBRARY
    PUBLIC EXTRACTPDF_SHARED)
endif()

if(MSVC)
  target_compile_options(extractpdf PRIVATE /W4 /WX)
else()
  target_compile_options(extractpdf PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(EXTRACTPDF_BUILD_TESTS)
  target_sources(extractpdf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/pdf_edit_fault_hook.c")
  target_compile_definitions(extractpdf PRIVATE EXTRACTPDF_TESTING=1)
  include(CTest)
  add_subdirectory(tests)
  target_sources(extractpdf_test_pdf_form_mutation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_form_rollback.c")
endif()
