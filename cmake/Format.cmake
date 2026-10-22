if(NOT EXISTS ${CLANG_FORMAT_EXECUTABLE})
    find_program(CLANG_FORMAT_EXEC_TEMP ${CLANG_FORMAT_EXECUTABLE})
    if(CLANG_FORMAT_EXEC_TEMP)
        set(CLANG_FORMAT_EXECUTABLE ${CLANG_FORMAT_EXEC_TEMP})
        unset(CLANG_FORMAT_EXEC_TEMP)
    else()
        message(FATAL_ERROR "Unable to find clang-format executable: \"${CLANG_FORMAT_EXECUTABLE}\"")
    endif()
endif()

set(SOURCES "")
foreach(FOLDER IN ITEMS examples include src tests tools)
    file(GLOB_RECURSE folder_files "${FOLDER}/*.hpp" "${FOLDER}/*.cpp")
    list(APPEND SOURCES ${folder_files})
endforeach()
# The generated module interfaces: tools/api_surface.cpp writes them formatted, this keeps it so.
file(GLOB module_files "modules/*.cppm")
list(APPEND SOURCES ${module_files})

# The file list goes through a response file: on Windows the paths alone exceed the 32767
# character command line limit, and the process then fails to start.
string(REPLACE ";" "
" SOURCE_LINES "${SOURCES}")
if(NOT FORMAT_LIST_DIR)
    set(FORMAT_LIST_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set(FILE_LIST "${FORMAT_LIST_DIR}/clang-format-files.txt")
file(WRITE "${FILE_LIST}" "${SOURCE_LINES}
")

execute_process(COMMAND ${CLANG_FORMAT_EXECUTABLE} -i "--files=${FILE_LIST}" RESULT_VARIABLE FORMAT_RESULT)
if(NOT FORMAT_RESULT EQUAL 0)
    message(FATAL_ERROR "clang-format failed: ${FORMAT_RESULT}")
endif()
