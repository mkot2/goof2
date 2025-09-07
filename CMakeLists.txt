cmake_minimum_required(VERSION 3.16)
project(goof2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)

#Use a compiler cache if available to speed up rebuilds
#Prefer sccache(great on Windows / macOS / Linux), then fall back to ccache / clcache
find_program(SCCACHE_PROGRAM NAMES sccache)
find_program(CCACHE_PROGRAM NAMES ccache)
find_program(CLCACHE_PROGRAM NAMES clcache)
if(SCCACHE_PROGRAM)
    message(STATUS "Enabling sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
elseif(CCACHE_PROGRAM)
    message(STATUS "Enabling ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
elseif(CLCACHE_PROGRAM)
    message(STATUS "Enabling clcache: ${CLCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CLCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CLCACHE_PROGRAM}")
endif()

# Allow GCC, Clang (including clang-cl), and MSVC. Fail for anything else.
if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MSVC))
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Use GCC, Clang/clang-cl, or MSVC.")
endif()

include(FetchContent)

#Link the C++ runtime statically on Windows to avoid missing procedure
#entry point errors when running the prebuilt executable.
if(MINGW)
    add_link_options(-static)
endif()

option(BUILD_COVERAGE "Enable coverage reporting" OFF)
option(GOOF2_ENABLE_REPL "Enable interactive REPL" ON)
option(GOOF2_BUILD_TESTS "Build unit and fuzz tests" ON)

if(BUILD_COVERAGE
        AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
        AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()
#Default to a Release build if no build type is explicitly set.This keeps
#binaries small and enables the high - optimization flags defined below.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified. Defaulting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

#Enable multi - processor compilation for MSVC / clang - cl to speed up builds
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    add_compile_options(/MP)
endif()

#Use pipes for GCC / Clang to reduce temp file I / O during compilation
add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-pipe>)

#Stub warnings target required by cpp - terminal
add_library(Warnings INTERFACE)
add_library(Warnings::Warnings ALIAS Warnings)

target_compile_options(Warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

#Disable building tests, examples, docs, and install rules for submodules
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(GOOF2_ENABLE_REPL)
    set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
endif()

if(GOOF2_ENABLE_REPL)
    set(CPP_TERMINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cpp-terminal)

    # Try vendored submodule first
    if(EXISTS ${CPP_TERMINAL_SOURCE_DIR}/CMakeLists.txt)
        add_subdirectory(${CPP_TERMINAL_SOURCE_DIR} _deps/cpp-terminal-build)
        set(HAVE_CPP_TERMINAL TRUE)
    else()
        FetchContent_Declare(cpp-terminal
            GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal
            GIT_TAG a5c1d2619c96ae609357a7109076000b05ad006c
            GIT_SHALLOW TRUE
            PATCH_COMMAND git apply --ignore-whitespace ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/cpp-terminal-winternl.patch
        )
        FetchContent_MakeAvailable(cpp-terminal)
    endif()
    # cpp-terminal triggers some warnings that can become build errors when
    # projects compile with -Werror. Silence the specific warnings about ignored
    # return values and unused variables so that enabling -Werror does not break
    # the build.
    target_compile_options(cpp-terminal PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-result -Wno-unused-variable -Wno-unused-but-set-variable>
    )
endif()

set(SIMDE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/simde)
if(IS_DIRECTORY ${SIMDE_SOURCE_DIR})
    set(SIMDE_INCLUDE_DIR ${SIMDE_SOURCE_DIR})
else()
    FetchContent_Declare(simde
        GIT_REPOSITORY https://github.com/simd-everywhere/simde
        GIT_TAG 9fc78ccbc7b8abee42d43ec99311c171ee8b1904
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(simde)
    if(NOT simde_POPULATED)
        FetchContent_Populate(simde)
    endif()
    set(SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})
endif()

set(PCH_HEADER include/pch.hxx)
set(VM_SOURCES
    src/vm.cxx
    include/vm.hxx
)

add_library(vm ${VM_SOURCES})
target_precompile_headers(vm PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/${PCH_HEADER}>")
target_include_directories(vm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/goof2>
    PRIVATE
        ${SIMDE_INCLUDE_DIR}
)
target_link_libraries(vm PRIVATE $<BUILD_INTERFACE:Warnings>)
target_compile_options(vm PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-psabi>
)

set(EXEC_SOURCES
    main.cxx
)
if(GOOF2_ENABLE_REPL)
    list(APPEND EXEC_SOURCES
        src/repl.cxx
        include/repl.hxx
    )
endif()

set(PROJECT_SOURCES ${EXEC_SOURCES} ${VM_SOURCES} ${PCH_HEADER})

add_executable(goof2
    ${EXEC_SOURCES}
)
target_precompile_headers(goof2 REUSE_FROM vm)

target_link_libraries(goof2 PRIVATE
    vm
    $<BUILD_INTERFACE:Warnings>
)
if(GOOF2_ENABLE_REPL)
    target_link_libraries(goof2 PRIVATE cpp-terminal::cpp-terminal)
    if (WIN32)
        # Needed for CommandLineToArgvW on MinGW/Windows builds
        target_link_libraries(goof2 PRIVATE shell32)
    endif()
    target_compile_definitions(goof2 PRIVATE GOOF2_ENABLE_REPL)
endif()

install(TARGETS goof2 vm
    EXPORT goof2Targets
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES include/*.hxx DESTINATION include/goof2)

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/goof2Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/goof2Config.cmake
    INSTALL_DESTINATION lib/cmake/goof2
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/goof2Config.cmake
    DESTINATION lib/cmake/goof2
)
install(EXPORT goof2Targets
    FILE goof2Targets.cmake
    NAMESPACE goof2::
    DESTINATION lib/cmake/goof2
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
        AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(ltoFlag "-flto=thin")
    else()
        set(ltoFlag "-flto")
    endif()
    target_compile_options(goof2 PRIVATE
        $<$<CONFIG:Release>:-Ofast>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:Release>:-fomit-frame-pointer>
        $<$<CONFIG:Release>:-fno-plt>
        $<$<CONFIG:Release>:-fno-semantic-interposition>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
        $<$<CONFIG:Release>:${ltoFlag}>
    )
    target_link_options(goof2 PRIVATE
        $<$<CONFIG:Release>:${ltoFlag}>
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
    )
    set_property(TARGET goof2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(goof2 PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
    target_link_options(goof2 PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    set_property(TARGET goof2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(MSVC)
    target_compile_options(goof2 PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
    target_link_options(goof2 PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
    set_property(TARGET goof2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${PROJECT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Format source files"
    )
endif()

enable_testing()
if(GOOF2_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Provide a convenient Valgrind memcheck target and configuration for CTest
# Only available when Valgrind is present (typically on Linux/macOS)
include(CTest)
find_program(VALGRIND_EXE NAMES valgrind)
if(VALGRIND_EXE)
    # Standard, strict Valgrind options suitable for CI
    set(MEMORYCHECK_COMMAND "${VALGRIND_EXE}")
    set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect --error-exitcode=2 --track-origins=yes --num-callers=50")
    set(MEMORYCHECK_TYPE "Valgrind")

    # Export to CTest so `ctest -T memcheck` works from the build tree
    set(CTEST_MEMORYCHECK_COMMAND "${MEMORYCHECK_COMMAND}")
    set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "${MEMORYCHECK_COMMAND_OPTIONS}")

    add_custom_target(memcheck
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --timeout 60 -T memcheck
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Run tests under Valgrind (ctest -T memcheck)"
    )
else()
    message(STATUS "Valgrind not found; 'memcheck' target will be unavailable")
endif()

if(BUILD_COVERAGE)
    find_program(LCOV_EXE lcov)
    find_program(GENHTML_EXE genhtml)
    if(LCOV_EXE AND GENHTML_EXE)
        add_custom_target(coverage
            DEPENDS vm_execute_tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXE} --capture --directory . --output-file coverage.info
            COMMAND ${LCOV_EXE} --remove coverage.info '/usr/*' --output-file coverage.info
            COMMAND ${GENHTML_EXE} coverage.info --output-directory coverage
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Run tests and generate coverage report"
        )
    else()
        message(WARNING "lcov not found; coverage target will not be available")
    endif()
endif()
