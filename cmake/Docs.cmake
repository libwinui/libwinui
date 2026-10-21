# SPDX-License-Identifier: MIT
# Documentation targets. Both are optional: they appear only when their tools are found.
#
#   docs-reference  regenerates docs/reference/** from include/winui via Doxygen XML
#                   (docs/tools/gen_reference.py, docs/tools/control_map.toml)
#   docs            builds the site into site/ with Zensical (depends on docs-reference)
#   api-surface     rewrites api/surface*.txt, the public API one declaration per line (tools/api_surface.cpp)
#   api-check       fails when api/surface*.txt no longer match the headers
#
# Zensical is looked up in the repo-local venv first (`python -m venv .venv &&
# .venv/Scripts/pip install zensical`, or `uv venv .venv && uv pip install --python .venv zensical`),
# then on PATH.

find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(LIBWINUI_DOXYGEN_EXECUTABLE doxygen)
find_program(LIBWINUI_ZENSICAL_EXECUTABLE zensical
    HINTS "${PROJECT_SOURCE_DIR}/.venv/Scripts" "${PROJECT_SOURCE_DIR}/.venv/bin")

if(Python3_Interpreter_FOUND AND LIBWINUI_DOXYGEN_EXECUTABLE)
    add_custom_target(docs-reference
        COMMAND ${Python3_EXECUTABLE} docs/tools/gen_reference.py --doxygen "${LIBWINUI_DOXYGEN_EXECUTABLE}"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating docs/reference from include/winui"
        VERBATIM)
else()
    message(STATUS "libwinui: docs-reference target unavailable (needs Python 3 and doxygen)")
endif()

# api/surface.txt and api/surface-testing.txt: the public API, one declaration per line, written by
# tools/api_surface.cpp from Doxygen's XML. api-check fails when they no longer match the headers.
if(LIBWINUI_DOXYGEN_EXECUTABLE AND TARGET winui_api_surface)
    add_custom_target(api-surface
        COMMAND "${LIBWINUI_DOXYGEN_EXECUTABLE}" docs/tools/Doxyfile
        COMMAND winui_api_surface "${PROJECT_SOURCE_DIR}"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Writing api/surface*.txt from include/winui"
        VERBATIM)
    add_custom_target(api-check
        COMMAND "${LIBWINUI_DOXYGEN_EXECUTABLE}" docs/tools/Doxyfile
        COMMAND winui_api_surface "${PROJECT_SOURCE_DIR}" --check
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Checking api/surface*.txt against include/winui"
        VERBATIM)
else()
    message(STATUS "libwinui: api-surface and api-check targets unavailable (need doxygen)")
endif()

if(LIBWINUI_ZENSICAL_EXECUTABLE)
    add_custom_target(docs
        COMMAND "${LIBWINUI_ZENSICAL_EXECUTABLE}" build
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Building the documentation site into site/"
        VERBATIM)
    if(TARGET docs-reference)
        add_dependencies(docs docs-reference)
    endif()
else()
    message(STATUS "libwinui: docs target unavailable (zensical not found)")
endif()
