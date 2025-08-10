cmake_minimum_required(VERSION 3.14)
project(bfvmcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bfvmcpp
    main.cxx
    src/vm.cpp
)

target_include_directories(bfvmcpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
