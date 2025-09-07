cmake_minimum_required(VERSION 3.16)
project(goof2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)

# On Windows, Unity builds can create objects with >65k sections.
# - MSVC/clang-cl: support big objects via /bigobj.
# - MinGW (GNU/Clang with GNU frontend): GNU binutils often cannot link big-obj,
#   so prefer disabling Unity to avoid exceeding section limits.
if(WIN32)
    if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
        add_compile_options(/bigobj)
    elseif(MINGW AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(STATUS "Disabling Unity builds on MinGW to avoid COFF section limits")
        set(CMAKE_UNITY_BUILD OFF)
        add_compile_options(-Wa,-mbig-obj)
    endif()
endif()

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

# Allow GCC/Clang cross‑platform. On Windows: default MinGW (GCC), allow clang-cl; disallow MSVC.
if(WIN32)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "MSVC (cl.exe) is not supported. Use MinGW (GCC) or clang-cl.")
    endif()
    # clang-cl specific setup (Clang with MSVC frontend)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Prefer static MSVC runtime to avoid redistributable dependency
        if(POLICY CMP0091)
            cmake_policy(SET CMP0091 NEW)
        endif()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        # Prefer lld-link when available
        find_program(LLD_LINK_EXE NAMES lld-link lld-link.exe)
        if(LLD_LINK_EXE)
            message(STATUS "Using lld-link: ${LLD_LINK_EXE}")
            set(CMAKE_LINKER "${LLD_LINK_EXE}" CACHE FILEPATH "linker" FORCE)
        endif()
    endif()
else()
    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}. Use GCC or Clang.")
    endif()
endif()

include(FetchContent)
find_program(GIT_PROGRAM NAMES git git.exe QUIET)

#Link the C++ runtime statically on Windows to avoid missing procedure
#entry point errors when running the prebuilt executable.
if(MINGW)
    # Prefer static libstdc++/libgcc in Release; avoid fully static linking
    # which can produce invalid images with some binutils versions.
    add_link_options($<$<CONFIG:Release>:-static-libgcc> $<$<CONFIG:Release>:-static-libstdc++>)
    # Ensure AR/RANLIB use GCC wrappers so LTO plugin is available and add
    # an index on creation with the 's' flag.
    if(CMAKE_CXX_COMPILER_AR)
        set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}" CACHE FILEPATH "" FORCE)
    endif()
    if(CMAKE_CXX_COMPILER_RANLIB)
        set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}" CACHE FILEPATH "" FORCE)
    endif()
    set(CMAKE_C_ARCHIVE_CREATE        "<CMAKE_AR> crs <TARGET> <LINK_FLAGS> <OBJECTS>" CACHE STRING "" FORCE)
    set(CMAKE_C_ARCHIVE_FINISH        "<CMAKE_RANLIB> <TARGET>" CACHE STRING "" FORCE)
    set(CMAKE_CXX_ARCHIVE_CREATE      "<CMAKE_AR> crs <TARGET> <LINK_FLAGS> <OBJECTS>" CACHE STRING "" FORCE)
    set(CMAKE_CXX_ARCHIVE_FINISH      "<CMAKE_RANLIB> <TARGET>" CACHE STRING "" FORCE)
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

# Enable multi-processor compilation and faster Debug PDB handling on MSVC/clang-cl
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
    # Compile in parallel and allow concurrent PDB writes; prefer fastlink for Debug
    add_compile_options(/MP $<$<CONFIG:Debug>:/FS /Z7>)
    add_link_options($<$<CONFIG:Debug>:/DEBUG:FASTLINK>)
endif()

# Use pipes and split DWARF for GCC/Clang to reduce I/O and speed links in Debug
add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-pipe>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-gsplit-dwarf>)

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
    if(IS_DIRECTORY ${CPP_TERMINAL_SOURCE_DIR} AND EXISTS ${CPP_TERMINAL_SOURCE_DIR}/CMakeLists.txt)
        # Apply our local patch script to the vendored source before configuring it
        execute_process(
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/patch-cpp-terminal-winternl.cmake
            WORKING_DIRECTORY ${CPP_TERMINAL_SOURCE_DIR}
        )
        add_subdirectory(${CPP_TERMINAL_SOURCE_DIR} _deps/cpp-terminal-build)
        set(HAVE_CPP_TERMINAL TRUE)
    else()
        # Fallback: fetch via Git when available, otherwise download archive URL
        set(CPP_TERMINAL_COMMIT a5c1d2619c96ae609357a7109076000b05ad006c)
        if(GIT_PROGRAM)
        FetchContent_Declare(cpp-terminal
            GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal
            GIT_TAG ${CPP_TERMINAL_COMMIT}
            GIT_SHALLOW TRUE
            PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/patch-cpp-terminal-winternl.cmake
        )
        else()
            # Use zip archive to avoid requiring Git; apply patch via a CMake script
            FetchContent_Declare(cpp-terminal
                URL https://github.com/jupyter-xeus/cpp-terminal/archive/${CPP_TERMINAL_COMMIT}.zip
                URL_HASH SHA256=SKIP
                PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/patch-cpp-terminal-winternl.cmake
            )
        endif()
        FetchContent_MakeAvailable(cpp-terminal)
    endif()
    # cpp-terminal triggers some warnings that can become build errors when
    # projects compile with -Werror. Silence the specific warnings about ignored
    # return values and unused variables so that enabling -Werror does not break
    # the build.
    target_compile_options(cpp-terminal PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unknown-pragmas -Wno-unused-result -Wno-unused-variable -Wno-unused-but-set-variable>
    )
endif()

set(SIMDE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/simde)
if(IS_DIRECTORY ${SIMDE_SOURCE_DIR})
    set(SIMDE_INCLUDE_DIR ${SIMDE_SOURCE_DIR})
else()
    set(SIMDE_COMMIT 1dd80a3c29c5b0d5c89458c148de43cfc562d73d)
    if(GIT_PROGRAM)
        FetchContent_Declare(simde
            GIT_REPOSITORY https://github.com/simd-everywhere/simde
            GIT_TAG ${SIMDE_COMMIT}
            GIT_SHALLOW TRUE
        )
    else()
        # Download archive to avoid requiring Git
        FetchContent_Declare(simde
            URL https://github.com/simd-everywhere/simde/archive/${SIMDE_COMMIT}.zip
            URL_HASH SHA256=SKIP
        )
    endif()
    FetchContent_MakeAvailable(simde)
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
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:MINGW>>>:${ltoFlag}>
    )
    target_link_options(goof2 PRIVATE
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:MINGW>>>:${ltoFlag}>
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
    )
    if(NOT MINGW)
        set_property(TARGET goof2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
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
