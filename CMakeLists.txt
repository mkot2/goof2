cmake_minimum_required(VERSION 3.14)
project(goof2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Link the C++ runtime statically on Windows to avoid missing procedure
# entry point errors when running the prebuilt executable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
    add_link_options(-static)
endif()

option(BUILD_COVERAGE "Enable coverage reporting" OFF)

if(BUILD_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()
# Default to a Release build if no build type is explicitly set.  This keeps
# binaries small and enables the high-optimization flags defined below.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified. Defaulting to Release.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Stub warnings target required by cpp-terminal
add_library(Warnings INTERFACE)
add_library(Warnings::Warnings ALIAS Warnings)

target_compile_options(Warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
)

# Disable building tests, examples, docs, and install rules for submodules
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(argh
    GIT_REPOSITORY https://github.com/adishavit/argh
    GIT_TAG c3f0d8c8a6dacb00df626b409248a34e3bcd15f5
)
FetchContent_Declare(cpp-terminal
    GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal
    GIT_TAG dfb5ddf47dca73ce3cb51e3b8a80f2485bb74dff
)
FetchContent_Declare(simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde
    GIT_TAG 7da3fb1d7f9ad859290f7141403036c3c90bf668
)

FetchContent_MakeAvailable(argh cpp-terminal)
FetchContent_GetProperties(simde)
if(NOT simde_POPULATED)
    FetchContent_Populate(simde)
endif()
set(SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})

set(PROJECT_SOURCES
    main.cxx
    src/vm.cxx
    include/vm.hxx
    src/repl.cxx
    include/repl.hxx
)

add_executable(goof2
    ${PROJECT_SOURCES}
)

target_include_directories(goof2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SIMDE_INCLUDE_DIR}
)

target_link_libraries(goof2 PRIVATE
    cpp-terminal::cpp-terminal
    argh
    Warnings
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(goof2 PRIVATE
        $<$<CONFIG:Release>:-Ofast>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:Release>:-fomit-frame-pointer>
        $<$<CONFIG:Release>:-fno-plt>
        $<$<CONFIG:Release>:-fno-semantic-interposition>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    target_link_options(goof2 PRIVATE
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
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
add_subdirectory(tests)

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
