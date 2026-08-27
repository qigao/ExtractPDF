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
  ENCRYPTED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/encrypted-one-page.pdf"
  TRUNCATED_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/truncated.pdf"
  MISSING_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/does-not-exist.pdf"
  UTF8_PDF="${UTF8_PDF}")
if(MSVC)
  target_compile_options(extractpdf_test_document PRIVATE /utf-8)
endif()
add_test(NAME extractpdf.document COMMAND extractpdf_test_document)
set_tests_properties(extractpdf.document PROPERTIES TIMEOUT 60)

add_executable(extractpdf_test_page test_page.c)
target_link_libraries(extractpdf_test_page PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_page PRIVATE
  GEOMETRY_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/geometry.pdf")
add_test(NAME extractpdf.page COMMAND extractpdf_test_page)
set_tests_properties(extractpdf.page PROPERTIES TIMEOUT 20)

add_executable(extractpdf_test_render test_render.c)
target_link_libraries(extractpdf_test_render PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_render PRIVATE
  RENDER_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/render.pdf")
add_test(NAME extractpdf.render COMMAND extractpdf_test_render)
set_tests_properties(extractpdf.render PROPERTIES TIMEOUT 30)

if(WIN32 AND BUILD_SHARED_LIBS)
  foreach(test_target IN ITEMS
      extractpdf_test_status
      extractpdf_test_document
      extractpdf_test_page
      extractpdf_test_render)
    add_custom_command(TARGET ${test_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:extractpdf>
        $<TARGET_FILE_DIR:${test_target}>
      VERBATIM)
  endforeach()
endif()
