include(FindPackageHandleStandardArgs)

set(MUPDF_ROOT "" CACHE PATH "MuPDF installation or source-tree root")

find_path(MUPDF_INCLUDE_DIR
  NAMES mupdf/fitz.h
  HINTS "${MUPDF_ROOT}/include")

if(WIN32)
  find_library(MUPDF_LIBRARY
    NAMES mupdfcpp64
    HINTS
      "${MUPDF_ROOT}/platform/win32/x64/Release"
      "${MUPDF_ROOT}/lib")

  find_package_handle_standard_args(MuPDF
    REQUIRED_VARS MUPDF_INCLUDE_DIR MUPDF_LIBRARY)

  if(MuPDF_FOUND AND NOT TARGET MuPDF::MuPDF)
    add_library(MuPDF::MuPDF INTERFACE IMPORTED)
    set_target_properties(MuPDF::MuPDF PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_INCLUDE_DIR}"
      INTERFACE_COMPILE_DEFINITIONS "FZ_DLL_CLIENT"
      INTERFACE_LINK_LIBRARIES "${MUPDF_LIBRARY}")
  endif()
else()
  find_library(MUPDF_LIBRARY
    NAMES mupdf
    HINTS
      "${MUPDF_ROOT}/build/release"
      "${MUPDF_ROOT}/lib")
  find_library(MUPDF_THIRD_LIBRARY
    NAMES mupdf-third
    HINTS
      "${MUPDF_ROOT}/build/release"
      "${MUPDF_ROOT}/lib")

  find_package_handle_standard_args(MuPDF
    REQUIRED_VARS MUPDF_INCLUDE_DIR MUPDF_LIBRARY MUPDF_THIRD_LIBRARY)

  if(MuPDF_FOUND AND NOT TARGET MuPDF::MuPDF)
    add_library(MuPDF::MuPDF INTERFACE IMPORTED)
    set_target_properties(MuPDF::MuPDF PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${MUPDF_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "${MUPDF_LIBRARY};${MUPDF_THIRD_LIBRARY};m;${CMAKE_DL_LIBS}")
  endif()
endif()

mark_as_advanced(MUPDF_INCLUDE_DIR MUPDF_LIBRARY MUPDF_THIRD_LIBRARY)
