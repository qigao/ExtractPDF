add_executable(extractpdf_test_status test_status.c)
target_link_libraries(extractpdf_test_status PRIVATE ExtractPDF::ExtractPDF)
add_test(NAME extractpdf.status COMMAND extractpdf_test_status)
set_tests_properties(extractpdf.status PROPERTIES TIMEOUT 10)

set(UTF8_PDF "${CMAKE_CURRENT_BINARY_DIR}/extractpdf-测试.pdf")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  "${UTF8_PDF}"
  COPYONLY)

add_executable(extractpdf_test_document test_document.c)
target_link_libraries(extractpdf_test_document PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_document PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TWO_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two-page.pdf"
  PAGE_BOXES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-boxes.pdf"
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  TRUNCATED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/truncated.pdf"
  MISSING_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/does-not-exist.pdf"
  UTF8_PDF="${UTF8_PDF}")
if(MSVC)
  target_compile_options(extractpdf_test_document PRIVATE /utf-8)
endif()
add_test(NAME extractpdf.document COMMAND extractpdf_test_document)
set_tests_properties(extractpdf.document PROPERTIES TIMEOUT 60)

add_executable(extractpdf_test_render test_render.c)
target_link_libraries(extractpdf_test_render PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_render PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  PAGE_BOXES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-boxes.pdf")
add_test(NAME extractpdf.render COMMAND extractpdf_test_render)
set_tests_properties(extractpdf.render PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_text test_text.c)
target_link_libraries(extractpdf_test_text PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_text PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf")
add_test(NAME extractpdf.text COMMAND extractpdf_test_text)
set_tests_properties(extractpdf.text PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_structured_text test_structured_text.c)
target_link_libraries(extractpdf_test_structured_text PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_structured_text PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  STRUCTURED_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/structured-text.pdf")
add_test(NAME extractpdf.structured_text COMMAND extractpdf_test_structured_text)
set_tests_properties(extractpdf.structured_text PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_text_search test_text_search.c)
target_link_libraries(extractpdf_test_text_search PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_text_search PRIVATE
  STRUCTURED_TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/structured-text.pdf")
add_test(NAME extractpdf.text_search COMMAND extractpdf_test_text_search)
set_tests_properties(extractpdf.text_search PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_images test_images.c)
target_link_libraries(extractpdf_test_images PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_images PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  PAGE_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf")
add_test(NAME extractpdf.images COMMAND extractpdf_test_images)
set_tests_properties(extractpdf.images PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_image_bitmap test_image_bitmap.c)
target_link_libraries(extractpdf_test_image_bitmap PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_image_bitmap PRIVATE
  PAGE_IMAGES_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images.pdf"
  PAGE_IMAGES_ALPHA_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-images-alpha.pdf")
add_test(NAME extractpdf.image_bitmap COMMAND extractpdf_test_image_bitmap)
set_tests_properties(extractpdf.image_bitmap PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_links test_links.c)
target_link_libraries(extractpdf_test_links PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_links PRIVATE
  PAGE_LINKS_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/page-links.pdf")
add_test(NAME extractpdf.links COMMAND extractpdf_test_links)
set_tests_properties(extractpdf.links PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_pdf_export test_pdf_export.c)
target_link_libraries(extractpdf_test_pdf_export PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_export PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  COMPOSITION_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-export-output.pdf")
add_test(NAME extractpdf.pdf_export COMMAND extractpdf_test_pdf_export)
set_tests_properties(extractpdf.pdf_export PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_pdf_range test_pdf_range.c)
target_link_libraries(extractpdf_test_pdf_range PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_range PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  COMPOSITION_NON_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-non-pdf.txt"
  RANGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-range-output.pdf")
add_test(NAME extractpdf.pdf_range COMMAND extractpdf_test_pdf_range)
set_tests_properties(extractpdf.pdf_range PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_pdf_order test_pdf_order.c)
target_link_libraries(extractpdf_test_pdf_order PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_order PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  ORDER_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-order-output.pdf")
add_test(NAME extractpdf.pdf_order COMMAND extractpdf_test_pdf_order)
set_tests_properties(extractpdf.pdf_order PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_pdf_delete test_pdf_delete.c)
target_link_libraries(extractpdf_test_pdf_delete PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_delete PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  DELETE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-delete-output.pdf")
add_test(NAME extractpdf.pdf_delete COMMAND extractpdf_test_pdf_delete)
set_tests_properties(extractpdf.pdf_delete PROPERTIES TIMEOUT 30)

add_executable(extractpdf_test_pdf_merge test_pdf_merge.c)
target_link_libraries(extractpdf_test_pdf_merge PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_pdf_merge PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  TEXT_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-one-page.pdf"
  MERGE_OUTPUT_PDF="${CMAKE_CURRENT_BINARY_DIR}/composition-merge-output.pdf")
add_test(NAME extractpdf.pdf_merge COMMAND extractpdf_test_pdf_merge)
set_tests_properties(extractpdf.pdf_merge PROPERTIES TIMEOUT 30)

set(OUTPUT_FILE_ASCII "${CMAKE_CURRENT_BINARY_DIR}/composition-save-output.pdf")
set(OUTPUT_FILE_UTF8 "${CMAKE_CURRENT_BINARY_DIR}/composition-save-测试.pdf")
set(OUTPUT_FILE_MISSING_PARENT_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/extractpdf-save-missing-parent")
set(OUTPUT_FILE_MISSING_PARENT
  "${OUTPUT_FILE_MISSING_PARENT_DIR}/output.pdf")

add_executable(extractpdf_test_output_file test_output_file.c)
target_link_libraries(extractpdf_test_output_file PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_output_file PRIVATE
  COMPOSITION_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/composition-three-page.pdf"
  ASCII_OUTPUT_PDF="${OUTPUT_FILE_ASCII}"
  UTF8_OUTPUT_PDF="${OUTPUT_FILE_UTF8}"
  MISSING_PARENT_DIR="${OUTPUT_FILE_MISSING_PARENT_DIR}"
  MISSING_PARENT_PDF="${OUTPUT_FILE_MISSING_PARENT}")
if(MSVC)
  target_compile_options(extractpdf_test_output_file PRIVATE /utf-8)
endif()
add_test(NAME extractpdf.output_file COMMAND extractpdf_test_output_file)
set_tests_properties(extractpdf.output_file PROPERTIES TIMEOUT 30)

if(WIN32 AND BUILD_SHARED_LIBS)
  foreach(test_target IN ITEMS
      extractpdf_test_status
      extractpdf_test_document
      extractpdf_test_render
      extractpdf_test_text
      extractpdf_test_structured_text
      extractpdf_test_text_search
      extractpdf_test_images
      extractpdf_test_image_bitmap
      extractpdf_test_links
      extractpdf_test_pdf_export
      extractpdf_test_pdf_range
      extractpdf_test_pdf_order
      extractpdf_test_pdf_delete
      extractpdf_test_pdf_merge
      extractpdf_test_output_file)
    add_custom_command(TARGET ${test_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:extractpdf>
        $<TARGET_FILE_DIR:${test_target}>
      VERBATIM)
  endforeach()
endif()
