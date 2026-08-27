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
  src/text.c)
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
  include(CTest)
  add_subdirectory(tests)
endif()
