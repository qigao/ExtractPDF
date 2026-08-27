add_executable(extractpdf_test_status test_status.c)
target_link_libraries(extractpdf_test_status PRIVATE ExtractPDF::ExtractPDF)
add_test(NAME extractpdf.status COMMAND extractpdf_test_status)

add_executable(extractpdf_test_document test_document.c)
target_link_libraries(extractpdf_test_document PRIVATE ExtractPDF::ExtractPDF)
target_compile_definitions(extractpdf_test_document PRIVATE
  ONE_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one-page.pdf"
  TWO_PAGE_PDF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two-page.pdf")
add_test(NAME extractpdf.document COMMAND extractpdf_test_document)
