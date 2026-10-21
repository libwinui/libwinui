# SPDX-License-Identifier: MIT
# The project's warning set, after cpp-best-practices/cmake_template's CompilerWarnings.cmake.
#
#   winui_set_warnings(<target>)              PRIVATE warning flags for one of our own targets
#   winui_configure_directory_targets(<dir>)  the same, plus hardening and the ASan runtime, for
#                                             every target defined under <dir>
#
# LIBWINUI_WARNINGS_AS_ERRORS turns every warning into an error. It is on by default when libwinui
# is the top-level project and off when it is consumed through add_subdirectory(), so a newer
# compiler with a new warning never breaks somebody else's build.

if(LIBWINUI_STANDALONE)
    set(LIBWINUI_WARNINGS_AS_ERRORS_DEFAULT ON)
else()
    set(LIBWINUI_WARNINGS_AS_ERRORS_DEFAULT OFF)
endif()
option(LIBWINUI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${LIBWINUI_WARNINGS_AS_ERRORS_DEFAULT})

set(LIBWINUI_MSVC_WARNINGS
    /W4
    /w14242 # conversion from 'type1' to 'type2', possible loss of data
    /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
    /w14263 # member function does not override any base class virtual member function
    /w14265 # class has virtual functions, but its destructor is not virtual
    /w14287 # 'operator': unsigned/negative constant mismatch
    /we4289 # loop control variable declared in the for-loop is used outside of it
    /w14296 # 'operator': expression is always 'boolean_value'
    /w14311 # pointer truncation from 'type1' to 'type2'
    /w14545 # expression before comma evaluates to a function which is missing an argument list
    /w14546 # function call before comma missing argument list
    /w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
    /w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
    /w14555 # expression has no effect; expected expression with side-effect
    /w14619 # pragma warning: there is no warning number 'number'
    /w14640 # thread-unsafe static member initialization
    /w14826 # conversion from 'type1' to 'type2' is sign-extended
    /w14905 # wide string literal cast to 'LPSTR'
    /w14906 # string literal cast to 'LPWSTR'
    /w14928 # illegal copy-initialization; more than one user-defined conversion
    /permissive- # standards conformance
    # The Windows SDK and every other <...> include are not ours to fix.
    /external:anglebrackets
    /external:W0)

set(LIBWINUI_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wformat=2
    -Wimplicit-fallthrough)

set(LIBWINUI_GCC_WARNINGS
    ${LIBWINUI_CLANG_WARNINGS}
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wlogical-op)

function(winui_set_warnings target)
    # clang-cl sets MSVC too and takes the cl-style flags.
    if(MSVC)
        set(warnings ${LIBWINUI_MSVC_WARNINGS})
        # clang-cl already treats the INCLUDE paths as system headers, and clang-cl 20 rejects
        # /external:anglebrackets as unused under /WX.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            list(REMOVE_ITEM warnings /external:anglebrackets /external:W0)
        endif()
        if(LIBWINUI_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(warnings ${LIBWINUI_CLANG_WARNINGS})
        if(LIBWINUI_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(warnings ${LIBWINUI_GCC_WARNINGS})
        if(LIBWINUI_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    else()
        message(AUTHOR_WARNING "libwinui: no warning flags for compiler '${CMAKE_CXX_COMPILER_ID}'")
    endif()
    target_compile_options(${target} PRIVATE ${warnings})
    if(MSVC AND LIBWINUI_WARNINGS_AS_ERRORS)
        target_link_options(${target} PRIVATE /WX)
        # lld-link warns that the SDK's prntvpt.lib imports a DllMain; that's the SDK's doing.
        if(CMAKE_CXX_COMPILER_LINKER_ID STREQUAL "LLD" OR CMAKE_LINKER MATCHES "lld")
            target_link_options(${target} PRIVATE /ignore:importeddllmain)
        endif()
    endif()
endfunction()

# Warnings, hardening and the ASan runtime for every target defined in `dir` and below it.
function(winui_configure_directory_targets dir)
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT type STREQUAL "UTILITY")
            winui_set_warnings(${target})
        endif()
        if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT type STREQUAL "UTILITY")
            winui_enable_hardening(${target})
        endif()
        if(type STREQUAL "EXECUTABLE")
            winui_copy_asan_runtime(${target})
        endif()
    endforeach()
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        winui_configure_directory_targets(${subdir})
    endforeach()
endfunction()
