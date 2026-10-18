message(STATUS "Checking files for whitespace validity")

set(FORMAT_CHECK_DIRS
    ${SRC_DIR}/fpsgame
    ${SRC_DIR}/engine
    ${SRC_DIR}/shared
    ${SRC_DIR}/../data)

set(FORMAT_CHECK_FILES "")
foreach(FORMAT_CHECK_DIR ${FORMAT_CHECK_DIRS})
    FILE(GLOB_RECURSE FORMAT_CHECK_FILES_DIR "${FORMAT_CHECK_DIR}/**")
    set(FORMAT_CHECK_FILES
        ${FORMAT_CHECK_FILES}
        ${FORMAT_CHECK_FILES_DIR})
endforeach()

foreach(FORMAT_FILE ${FORMAT_CHECK_FILES})
    SET(FORMAT_FILE_LINE_I 0)

    if(${FORMAT_FILE} MATCHES "(c|cpp|h|cpp|cmake)$")
        FILE(READ "${FORMAT_FILE}" FORMAT_FILE_CONTENT)
        STRING(REGEX REPLACE ";" "" FORMAT_FILE_CONTENT "${FORMAT_FILE_CONTENT}")
        STRING(REGEX REPLACE "\n\n" "\n-\n" FORMAT_FILE_CONTENT "${FORMAT_FILE_CONTENT}")
        STRING(REGEX REPLACE "\n" ";" FORMAT_FILE_CONTENT "${FORMAT_FILE_CONTENT}")
        foreach(FORMAT_FILE_LINE ${FORMAT_FILE_CONTENT})
            MATH(EXPR FORMAT_FILE_LINE_I "${FORMAT_FILE_LINE_I} + 1")
            STRING(FIND "${FORMAT_FILE_LINE}" "\t" FORMAT_LINE_POSITION)
            if(NOT  ${FORMAT_LINE_POSITION} EQUAL -1)
                message(WARNING "Found tab in source (${FORMAT_FILE}:${FORMAT_FILE_LINE_I}:${FORMAT_LINE_POSITION}): ${FORMAT_FILE_LINE}")
            endif()
        endforeach()
    endif()
endforeach()

