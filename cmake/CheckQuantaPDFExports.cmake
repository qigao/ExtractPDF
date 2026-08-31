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

file(STRINGS "${QUANTAPDF_EXPORT_BASELINE}" expected
  REGEX "^quantapdf_[A-Za-z0-9_]+$")
list(REMOVE_DUPLICATES expected)
list(SORT expected)
list(LENGTH expected expected_count)

string(REGEX MATCH "([0-9]+)[ \t]+number of functions" function_match "${output}")
if(function_match STREQUAL "")
  message(FATAL_ERROR "dumpbin output is missing the function count")
endif()
set(function_count "${CMAKE_MATCH_1}")

string(REGEX MATCH "([0-9]+)[ \t]+number of names" name_match "${output}")
if(name_match STREQUAL "")
  message(FATAL_ERROR "dumpbin output is missing the name count")
endif()
set(name_count "${CMAKE_MATCH_1}")

string(REPLACE "\r\n" "\n" output_lines "${output}")
string(REPLACE "\r" "\n" output_lines "${output_lines}")
string(REPLACE "\n" ";" output_lines "${output_lines}")
set(actual)
foreach(line IN LISTS output_lines)
  if(line MATCHES "^[ \t]*[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t=]+)")
    list(APPEND actual "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(REMOVE_DUPLICATES actual)
list(SORT actual)

if(NOT function_count EQUAL expected_count OR NOT name_count EQUAL expected_count)
  message(FATAL_ERROR
    "QuantaPDF ABI export count mismatch\n"
    "Expected named functions: ${expected_count}\n"
    "Functions: ${function_count}\n"
    "Names: ${name_count}")
endif()

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
