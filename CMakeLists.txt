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

add_executable(bfvmcpp
    main.cxx
    src/vm.cpp
)

target_include_directories(bfvmcpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bfvmcpp PRIVATE
    Boost::regex
    cpp-terminal::cpp-terminal
)
