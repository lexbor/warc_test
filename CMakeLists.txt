cmake_minimum_required(VERSION 2.8)

# For build with ASAN:
#     -DCMAKE_C_FLAGS='-O0 -g -fsanitize=address'
#
# For link lexbor library from not system path:
#     -DCMAKE_CPP_FLAGS="-I/path/to/include/lexbor"
#     -DCMAKE_EXE_LINKER_FLAGS="-L/path/to/lexbor/lib"
#

set(PROJECT_NAME "warc_parser")

project(${PROJECT_NAME})

message(STATUS "Project name: ${PROJECT_NAME}")

IF(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
ENDIF(APPLE)

################
## Version and path
#########################
set(WARC_PARSER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source" CACHE STRING "")

################
## Search and Includes
#########################
include_directories("${WARC_PARSER_SOURCE_DIR}")

################
## C_FLAGS
#########################
message(STATUS "CFLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "CXXFLAGS: ${CMAKE_CXX_FLAGS}")

################
## Features
#########################
include("feature.cmake")

################
## Check libraries
#########################
FEATURE_CHECK_LIB_EXIST(WARC_LIB_EXIST "z")
IF(NOT WARC_LIB_EXIST)
    message(ERROR "Required library not found: libz (zlib)")
ENDIF()

FEATURE_CHECK_HEADERS_EXIST(WARC_INC_EXIST "zlib" "zlib.h")
IF(NOT WARC_INC_EXIST)
    message(FATAL_ERROR "Required headers not found: zlib.h")
ENDIF()

FEATURE_CHECK_LIB_EXIST(WARC_LIB_EXIST "lexbor")
IF(NOT WARC_LIB_EXIST)
    message(FATAL_ERROR "Required library not found: liblexbor")
ENDIF()

FEATURE_CHECK_HEADERS_EXIST(WARC_INC_EXIST "lexbor HTML" "lexbor/html/base.h")
IF(NOT WARC_INC_EXIST)
    message(FATAL_ERROR "Required headers not found: lexbor/html/*")
ENDIF()

################
## Sources
#########################
file(GLOB_RECURSE WARC_SOURCES "${WARC_PARSER_SOURCE_DIR}/gzip/*.c")

################
## Target
#########################
add_executable("warc_test" ${WARC_SOURCES}
               "${WARC_PARSER_SOURCE_DIR}/warc_test.c")
target_link_libraries("warc_test" "lexbor" "z")

add_executable("warc_entry_by_index" ${WARC_SOURCES}
               "${WARC_PARSER_SOURCE_DIR}/warc_entry_by_index.c")
target_link_libraries("warc_entry_by_index" "lexbor" "z")
