include_guard(GLOBAL)

function(quantapdf_import_pdfium)
  if(TARGET QuantaPDF::PDFium)
    return()
  endif()

  set(_version "154.0.8021.0")
  set(_base_url
      "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8021")
  set(_target_processor "${CMAKE_SYSTEM_PROCESSOR}")

  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _architecture_count)
    if(NOT _architecture_count EQUAL 1)
      message(FATAL_ERROR
        "QuantaPDF requires a single-architecture macOS build for pinned PDFium artifacts")
    endif()
    list(GET CMAKE_OSX_ARCHITECTURES 0 _target_processor)
  elseif(WIN32 AND CMAKE_GENERATOR_PLATFORM)
    set(_target_processor "${CMAKE_GENERATOR_PLATFORM}")
  endif()

  if(WIN32
     AND CMAKE_SIZEOF_VOID_P EQUAL 8
     AND _target_processor MATCHES "^(x64|x86_64|amd64|AMD64)$")
    set(_platform "win-x64")
    set(_sha256
      "adac8ce034015427b5daa81f8eeddfcc8e84bc2a9f036f007890ff18bd4388c4")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux"
         AND _target_processor MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "linux-x64")
    set(_sha256
      "685f7930cd184ea22cd77afe707c1cf53b173d18118b6e16cb213c9277d7cdc3")
  elseif(APPLE AND _target_processor MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "mac-x64")
    set(_sha256
      "0e770fda56c6726a08fab84c6306ad91eceb10589020ce3a407fad3ebcbe7bb2")
  elseif(APPLE AND _target_processor MATCHES "^(arm64|aarch64)$")
    set(_platform "mac-arm64")
    set(_sha256
      "994600fa28974ce09a1c51c35039e808a6bc8ea3839050322c101ab229ad5c96")
  else()
    message(FATAL_ERROR
      "PDFium ${_version} has no pinned artifact for "
      "${CMAKE_SYSTEM_NAME}/${_target_processor}")
  endif()

  set(_url "${_base_url}/pdfium-${_platform}.tgz")
  set(_download_directory "${CMAKE_BINARY_DIR}/_deps/downloads")
  set(_archive "${_download_directory}/pdfium-${_platform}.tgz")
  set(_root "${CMAKE_BINARY_DIR}/_deps/pdfium-${_version}-${_platform}")

  if(NOT EXISTS "${_archive}")
    file(MAKE_DIRECTORY "${_download_directory}")
    file(DOWNLOAD "${_url}" "${_archive}"
      EXPECTED_HASH "SHA256=${_sha256}"
      TLS_VERIFY ON
      STATUS _download_status)
    list(GET _download_status 0 _download_code)
    list(GET _download_status 1 _download_message)
    if(NOT _download_code EQUAL 0)
      file(REMOVE "${_archive}")
      message(FATAL_ERROR "PDFium download failed: ${_download_message}")
    endif()
  endif()

  file(SHA256 "${_archive}" _actual_sha256)
  if(NOT _actual_sha256 STREQUAL _sha256)
    file(REMOVE "${_archive}")
    message(FATAL_ERROR
      "Cached PDFium archive hash mismatch: expected ${_sha256}, "
      "got ${_actual_sha256}; the bad cache entry was removed")
  endif()

  if(NOT EXISTS "${_root}/VERSION")
    file(MAKE_DIRECTORY "${_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_root}")
  endif()

  foreach(_required_path IN ITEMS
      "include/fpdfview.h"
      "LICENSE"
      "licenses"
      "args.gn"
      "VERSION")
    if(NOT EXISTS "${_root}/${_required_path}")
      message(FATAL_ERROR
        "Pinned PDFium artifact is missing ${_required_path}: ${_root}")
    endif()
  endforeach()

  file(READ "${_root}/args.gn" _build_arguments)
  foreach(_disabled_feature IN ITEMS
      "pdf_enable_v8 = false"
      "pdf_enable_xfa = false"
      "pdf_use_partition_alloc = false")
    string(FIND "${_build_arguments}" "${_disabled_feature}" _feature_position)
    if(_feature_position EQUAL -1)
      message(FATAL_ERROR
        "Pinned PDFium artifact does not prove '${_disabled_feature}'")
    endif()
  endforeach()

  file(READ "${_root}/VERSION" _artifact_version)
  foreach(_version_line IN ITEMS
      "MAJOR=154"
      "MINOR=0"
      "BUILD=8021"
      "PATCH=0")
    string(FIND "${_artifact_version}" "${_version_line}" _version_position)
    if(_version_position EQUAL -1)
      message(FATAL_ERROR
        "Pinned PDFium artifact has an unexpected VERSION file: ${_root}/VERSION")
    endif()
  endforeach()

  if(WIN32)
    set(_runtime "${_root}/bin/pdfium.dll")
    set(_import_library "${_root}/lib/pdfium.dll.lib")
    set(_runtime_directory "${_root}/bin")
  elseif(APPLE)
    set(_runtime "${_root}/lib/libpdfium.dylib")
    set(_runtime_directory "${_root}/lib")
  else()
    set(_runtime "${_root}/lib/libpdfium.so")
    set(_runtime_directory "${_root}/lib")
  endif()

  if(NOT EXISTS "${_runtime}")
    message(FATAL_ERROR "Pinned PDFium runtime is missing: ${_runtime}")
  endif()
  if(WIN32 AND NOT EXISTS "${_import_library}")
    message(FATAL_ERROR "Pinned PDFium import library is missing: ${_import_library}")
  endif()

  add_library(quantapdf_pdfium SHARED IMPORTED GLOBAL)
  add_library(QuantaPDF::PDFium ALIAS quantapdf_pdfium)
  set_target_properties(quantapdf_pdfium PROPERTIES
    IMPORTED_LOCATION "${_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${_root}/include")
  if(WIN32)
    set_target_properties(quantapdf_pdfium PROPERTIES
      IMPORTED_IMPLIB "${_import_library}")
  endif()

  set(QUANTAPDF_PDFIUM_ROOT "${_root}" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_RUNTIME_DIR "${_runtime_directory}" PARENT_SCOPE)
  set(QUANTAPDF_PDFIUM_LICENSE_DIR "${_root}/licenses" PARENT_SCOPE)
endfunction()
