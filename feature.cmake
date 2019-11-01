################
## Features
#########################
MACRO(FEATURE_CHECK_LIB_EXIST out_result lib_name)
    set(feature_filename "${CMAKE_BINARY_DIR}/feature_check.c")

    set(FEATUTE_CHECK_STRING "
int main(void) {
    return 0;
}")

    file(WRITE ${feature_filename} "${FEATUTE_CHECK_STRING}")

    try_compile(${out_result} "${CMAKE_BINARY_DIR}" "${feature_filename}"
        CMAKE_FLAGS "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
                    "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        LINK_LIBRARIES "${lib_name}"
    )

    IF(${${out_result}})
        message(STATUS "Library check \"${lib_name}\": exist")
    ELSE()
        message(STATUS "Library check \"${lib_name}\": not exist")
    ENDIF()

    file(REMOVE ${feature_filename})

    unset(FEATUTE_CHECK_STRING)
    unset(feature_filename)
ENDMACRO()

MACRO(FEATURE_CHECK_HEADERS_EXIST out_result check_name header_list)
    set(feature_filename "${CMAKE_BINARY_DIR}/feature_check.c")

    set(FEATUTE_CHECK_STRING "")

    FOREACH(item ${header_list})
        set(FEATUTE_CHECK_STRING "${FEATUTE_CHECK_STRING}#include \"${item}\"\n")
    ENDFOREACH()

    set(FEATUTE_CHECK_STRING "
${FEATUTE_CHECK_STRING}
int main(void) {
    return 0;
}")

    file(WRITE ${feature_filename} "${FEATUTE_CHECK_STRING}")

    try_compile(${out_result} "${CMAKE_BINARY_DIR}" "${feature_filename}"
        CMAKE_FLAGS "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
                    "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    )

    IF(${${out_result}})
        message(STATUS "Includes check \"${check_name}\": exist")
    ELSE()
        message(STATUS "Includes check \"${check_name}\": not exist")
    ENDIF()

    file(REMOVE ${feature_filename})

    unset(FEATUTE_CHECK_STRING)
    unset(feature_filename)
ENDMACRO()
