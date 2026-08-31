foreach(required IN ITEMS
    QUANTAPDF_DUMPBIN
    QUANTAPDF_LIBRARY
    QUANTAPDF_EXPORT_BASELINE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${QUANTAPDF_DUMPBIN}" /nologo /exports "${QUANTAPDF_LIBRARY}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dumpbin failed: ${error}")
endif()

string(REGEX MATCHALL "quantapdf_[A-Za-z0-9_]+" actual "${output}")
list(REMOVE_DUPLICATES actual)
list(SORT actual)

file(STRINGS "${QUANTAPDF_EXPORT_BASELINE}" expected
  REGEX "^quantapdf_[A-Za-z0-9_]+$")
list(REMOVE_DUPLICATES expected)
list(SORT expected)

if(NOT "${actual}" STREQUAL "${expected}")
  set(unexpected ${actual})
  list(REMOVE_ITEM unexpected ${expected})
  set(missing ${expected})
  list(REMOVE_ITEM missing ${actual})
  message(FATAL_ERROR
    "QuantaPDF ABI export mismatch\n"
    "Missing: ${missing}\n"
    "Unexpected: ${unexpected}")
endif()
