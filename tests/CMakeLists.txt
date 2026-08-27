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

if(WIN32 AND BUILD_SHARED_LIBS)
  foreach(test_target IN ITEMS
      extractpdf_test_status
      extractpdf_test_document
      extractpdf_test_render
      extractpdf_test_text
      extractpdf_test_structured_text
      extractpdf_test_text_search
      extractpdf_test_images)
    add_custom_command(TARGET ${test_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:extractpdf>
        $<TARGET_FILE_DIR:${test_target}>
      VERBATIM)
  endforeach()
endif()
