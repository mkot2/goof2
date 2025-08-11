add_executable(vm_execute_tests
    ${CMAKE_SOURCE_DIR}/src/vm.cxx
    test_execute.cxx
)

target_include_directories(vm_execute_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(vm_execute_tests PRIVATE
    Boost::regex
    cpp-terminal::cpp-terminal
    Warnings
)

add_test(NAME vm_execute_tests COMMAND vm_execute_tests)

add_executable(vm_execute_fuzz
    ${CMAKE_SOURCE_DIR}/src/vm.cxx
    fuzz_execute.cxx
)

target_include_directories(vm_execute_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(vm_execute_fuzz PRIVATE
    Boost::regex
    cpp-terminal::cpp-terminal
    Warnings
)
