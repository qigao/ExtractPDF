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
  src/pdf_page_box_common.c
  src/pdf_crop.c
  src/pdf_crop_preflight.c
  src/pdf_trim.c
  src/pdf_trim_preflight.c
  src/pdf_poster.c
  src/pdf_poster_preflight.c
  src/pdf_poster_annotations.c
  src/pdf_poster_widget_provenance.c
  src/pdf_poster_navigation.c
  src/pdf_poster_navigation_equivalence.c
  src/pdf_poster_cleanup.c
  src/pdf_poster_migrate.c
  src/pdf_export.c
  src/pdf_output.c
  src/pdf_range.c
  src/pdf_merge.c
  src/output_file.c
  src/pdf_metadata.c
  src/pdf_outline_common.c
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
  src/pdf_edit_annotations.c
  src/pdf_appearance_common.c
  src/pdf_flatten_preflight.c
  src/pdf_flatten_form.c
  src/pdf_flatten_bake.c
  src/pdf_flatten.c)
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
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim.c"
    TARGET_DIRECTORY extractpdf_test_pdf_trim
    PROPERTY COMPILE_DEFINITIONS "main=extractpdf_pdf_trim_base_main")
  target_sources(extractpdf_test_pdf_trim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_raw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_boxes.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_transforms.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_batch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_outside_crop.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_trim_main.c")
  target_link_libraries(extractpdf_test_pdf_trim PRIVATE
    unofficial::libmupdf::libmupdf)
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split.c"
    TARGET_DIRECTORY extractpdf_test_pdf_poster_split
    PROPERTY COMPILE_DEFINITIONS "main=extractpdf_pdf_poster_split_base_main")
  target_sources(extractpdf_test_pdf_poster_split PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_raw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_navigation_raw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_geometry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_interactive.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_navigation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_batch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_poster_split_main.c")
  target_link_libraries(extractpdf_test_pdf_poster_split PRIVATE
    unofficial::libmupdf::libmupdf)
  target_compile_definitions(extractpdf_test_pdf_poster_split PRIVATE
    POSTER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/poster-split-output.pdf"
    POSTER_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-rotate-90.pdf"
    POSTER_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-userunit.pdf"
    POSTER_INTERACTIVE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-interactive.pdf"
    POSTER_NAVIGATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation.pdf"
    POSTER_NAVIGATION_ROTATE_90_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation-rotate-90.pdf"
    POSTER_NAVIGATION_USERUNIT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-navigation-userunit.pdf"
    POSTER_MALFORMED_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-malformed-destination.pdf"
    POSTER_NONXYZ_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-nonxyz-destination.pdf"
    POSTER_NULLXYZ_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-nullxyz-destination.pdf"
    POSTER_OUTSIDE_DESTINATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-outside-destination.pdf"
    POSTER_OUTLINE_UNSUPPORTED_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-outline-unsupported-action.pdf"
    POSTER_UNSELECTED_ANNOT_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-unselected-annot-action.pdf"
    POSTER_FIELD_ACTION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-field-action.pdf"
    POSTER_PRODUCTION_BOX_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-production-box.pdf"
    POSTER_TAGGED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-tagged.pdf"
    POSTER_ROOT_POLICY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-root-policy.pdf"
    POSTER_UNSELECTED_ACTIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-unselected-actions.pdf"
    POSTER_FORM_ACTIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-form-actions.pdf"
    POSTER_MALFORMED_ANNOTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-malformed-annots.pdf"
    POSTER_CROSSING_ANNOTATION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-crossing-annotation.pdf"
    POSTER_CROSSING_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-crossing-widget.pdf"
    POSTER_ORPHAN_WIDGET_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/poster-orphan-widget.pdf")
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_flatten.c"
    TARGET_DIRECTORY extractpdf_test_pdf_flatten
    PROPERTY COMPILE_DEFINITIONS "main=extractpdf_pdf_flatten_base_main")
  target_sources(extractpdf_test_pdf_flatten PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_flatten_appearance.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_flatten_raw.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_flatten_form.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pdf_flatten_main.c")
  target_link_libraries(extractpdf_test_pdf_flatten PRIVATE
    unofficial::libmupdf::libmupdf)
  target_compile_definitions(extractpdf_test_pdf_flatten PRIVATE
    FLATTEN_NO_ANNOTATIONS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/one-page.pdf"
    FLATTEN_APPEARANCE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/flatten-appearance.pdf"
    FLATTEN_APPEARANCE_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/flatten-appearance-malformed.pdf"
    FLATTEN_CONTENTS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/flatten-contents.pdf"
    FLATTEN_CONTENTS_MALFORMED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/flatten-contents-malformed.pdf"
    FLATTEN_WIDGETS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/flatten-widgets.pdf")
endif()