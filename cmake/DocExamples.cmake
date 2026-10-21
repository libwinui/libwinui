# SPDX-License-Identifier: MIT
# Compiles every `@code` example in the public headers' doc comments, so the reference can't show
# one that doesn't compile. tools/doc_examples.cpp writes one source per header into the build tree;
# winui_doc_example_check compiles them and is never linked. It builds with the whole tree, not with
# the everyday `--target winui_tests`.

# Headers per compiled batch. 1 shows every error at once, where a batch stops at the compiler's
# error limit.
set(LIBWINUI_DOC_EXAMPLES_BATCH 8 CACHE STRING "Headers per batch in the doc example check")

set(doc_examples_dir "${CMAKE_CURRENT_BINARY_DIR}/doc_examples")
file(GLOB_RECURSE doc_example_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/winui/*.hpp")
set(doc_example_sources)
foreach(header IN LISTS doc_example_headers)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}/include/winui" "${header}")
    string(REPLACE "/" "_" name "${relative}")
    string(REGEX REPLACE "\\.hpp$" ".cpp" name "${name}")
    list(APPEND doc_example_sources "${doc_examples_dir}/${name}")
endforeach()

add_custom_command(
    OUTPUT ${doc_example_sources}
    COMMAND winui_doc_examples "${PROJECT_SOURCE_DIR}" "${doc_examples_dir}"
    DEPENDS winui_doc_examples ${doc_example_headers}
    COMMENT "Extracting the doc examples"
    VERBATIM)

add_library(winui_doc_example_check OBJECT ${doc_example_sources})
target_link_libraries(winui_doc_example_check PRIVATE winui::testing)
target_include_directories(winui_doc_example_check PRIVATE "${PROJECT_SOURCE_DIR}/tools/doc_examples")
target_precompile_headers(winui_doc_example_check PRIVATE "${PROJECT_SOURCE_DIR}/tools/doc_examples/placeholders.hpp")
set_target_properties(winui_doc_example_check PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE ${LIBWINUI_DOC_EXAMPLES_BATCH})
# An example shows how to call the API, so the unused variables and parameters it leaves behind
# aren't worth a warning.
if(MSVC)
    target_compile_options(winui_doc_example_check PRIVATE /W1 /bigobj)
else()
    target_compile_options(winui_doc_example_check PRIVATE -w)
endif()
