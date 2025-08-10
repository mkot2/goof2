cmake_minimum_required(VERSION 3.14)
project(bfvmcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Stub warnings target required by cpp-terminal
add_library(Warnings INTERFACE)
add_library(Warnings::Warnings ALIAS Warnings)

add_subdirectory(cpp-terminal)

set(BOOST_REGEX_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory(regex-develop)

set(PROJECT_SOURCES
    main.cxx
    src/vm.cpp
    include/vm.hpp
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
)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${PROJECT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Format source files"
    )
endif()
