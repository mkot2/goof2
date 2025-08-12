cmake_minimum_required(VERSION 3.14)
project(goof2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
include(ProcessorCount)
ProcessorCount(LTO_PARALLEL_JOBS)
if(NOT LTO_PARALLEL_JOBS OR LTO_PARALLEL_JOBS EQUAL 0)
    set(LTO_PARALLEL_JOBS 1)
endif()

# Link the C++ runtime statically on Windows to avoid missing procedure
# entry point errors when running the prebuilt executable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
    add_link_options(-static)
endif()

option(BUILD_COVERAGE "Enable coverage reporting" OFF)
option(GOOF2_ENABLE_REPL "Enable interactive REPL" ON)
option(GooF2_USE_SLJIT "Enable SLJIT JIT support" OFF)

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
if(GOOF2_ENABLE_REPL)
    set(CPPTERMINAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
    set(CPPTERMINAL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
endif()

if(GOOF2_ENABLE_REPL)
    FetchContent_Declare(cpp-terminal
        GIT_REPOSITORY https://github.com/jupyter-xeus/cpp-terminal
        GIT_TAG dfb5ddf47dca73ce3cb51e3b8a80f2485bb74dff
    )
endif()
FetchContent_Declare(simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde
    GIT_TAG 7da3fb1d7f9ad859290f7141403036c3c90bf668
)

if(GOOF2_ENABLE_REPL)
    FetchContent_MakeAvailable(cpp-terminal)
    # cpp-terminal triggers some warnings that can become build errors
    # when projects compile with -Werror.  Silence the specific warnings
    # about ignored return values and unused variables so that enabling
    # -Werror does not break the build.
    target_compile_options(cpp-terminal PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-result -Wno-unused-variable -Wno-unused-but-set-variable>
    )
endif()
FetchContent_GetProperties(simde)
if(NOT simde_POPULATED)
    FetchContent_Populate(simde)
endif()
set(SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})

if(GooF2_USE_SLJIT)
    add_library(sljit STATIC
        sljit/sljit_src/sljitLir.c
        sljit/sljit_src/sljitUtils.c
        sljit/sljit_src/sljitSerialize.c
    )
    target_include_directories(sljit PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sljit/sljit_src>
    )
endif()

set(VM_SOURCES
    src/vm.cxx
    src/jit.cxx
    include/vm.hxx
    src/jit.hxx
)

add_library(vm ${VM_SOURCES})
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
if(GooF2_USE_SLJIT)
    target_link_libraries(vm PRIVATE sljit)
    target_compile_definitions(vm PUBLIC GOOF2_USE_SLJIT)
    target_include_directories(vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sljit/sljit_src)
endif()

set(EXEC_SOURCES
    main.cxx
)
if(GOOF2_ENABLE_REPL)
    list(APPEND EXEC_SOURCES
        src/repl.cxx
        include/repl.hxx
    )
endif()

set(PROJECT_SOURCES ${EXEC_SOURCES} ${VM_SOURCES})

add_executable(goof2
    ${EXEC_SOURCES}
)

target_link_libraries(goof2 PRIVATE
    vm
    $<BUILD_INTERFACE:Warnings>
)
if(GOOF2_ENABLE_REPL)
    target_link_libraries(goof2 PRIVATE cpp-terminal::cpp-terminal)
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
        $<$<CONFIG:Release>:-flto=${LTO_PARALLEL_JOBS}>
    )
    target_link_options(goof2 PRIVATE
        $<$<CONFIG:Release>:-flto=${LTO_PARALLEL_JOBS}>
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
