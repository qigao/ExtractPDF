cmake_minimum_required(VERSION 3.20)
project(ExtractPDF VERSION 2.0.0 LANGUAGES C)

option(EXTRACTPDF_BUILD_TESTS "Build ExtractPDF tests" ON)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

find_package(unofficial-libmupdf CONFIG QUIET)
if(TARGET unofficial::libmupdf::libmupdf)
  set(EXTRACTPDF_MUPDF_TARGET unofficial::libmupdf::libmupdf)
else()
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(MuPDF REQUIRED)
  set(EXTRACTPDF_MUPDF_TARGET MuPDF::MuPDF)
endif()

add_library(extractpdf
  src/status.c
  src/document.c)
add_library(ExtractPDF::ExtractPDF ALIAS extractpdf)
target_compile_features(extractpdf PUBLIC c_std_11)
target_include_directories(extractpdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(extractpdf PRIVATE ${EXTRACTPDF_MUPDF_TARGET})

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
