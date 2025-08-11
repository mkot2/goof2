cmake_minimum_required(VERSION 3.14)
project(bfvmcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_COVERAGE "Enable coverage reporting" OFF)

if(BUILD_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
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

add_subdirectory(cpp-terminal)

set(BOOST_REGEX_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory(regex-develop)

set(PROJECT_SOURCES
    main.cxx
    src/vm.cxx
    include/vm.hxx
    src/repl.cxx
    include/repl.hxx
)

add_executable(bfvmcpp
    ${PROJECT_SOURCES}
)

target_include_directories(bfvmcpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bfvmcpp PRIVATE
    Boost::regex
    cpp-terminal::cpp-terminal
    Warnings
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bfvmcpp PRIVATE
        $<$<CONFIG:Release>:-Ofast>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-funroll-loops>
        $<$<CONFIG:Release>:-fomit-frame-pointer>
        $<$<CONFIG:Release>:-fno-plt>
        $<$<CONFIG:Release>:-fno-semantic-interposition>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    target_link_options(bfvmcpp PRIVATE
        $<$<CONFIG:Release>:-flto>
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
    )
    set_property(TARGET bfvmcpp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
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
