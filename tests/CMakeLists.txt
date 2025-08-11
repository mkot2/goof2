add_executable(vm_execute_tests
    ${CMAKE_SOURCE_DIR}/src/vm.cxx
    test_execute.cxx
)

target_include_directories(vm_execute_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${SIMDE_INCLUDE_DIR}
)

target_link_libraries(vm_execute_tests PRIVATE
    Warnings
)
if(GOOF2_ENABLE_REPL)
    target_link_libraries(vm_execute_tests PRIVATE cpp-terminal::cpp-terminal)
endif()

add_test(NAME vm_execute_tests COMMAND vm_execute_tests)

add_executable(vm_execute_fuzz
    ${CMAKE_SOURCE_DIR}/src/vm.cxx
    fuzz_execute.cxx
)

target_include_directories(vm_execute_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${SIMDE_INCLUDE_DIR}
)

target_link_libraries(vm_execute_fuzz PRIVATE
    Warnings
)
if(GOOF2_ENABLE_REPL)
    target_link_libraries(vm_execute_fuzz PRIVATE cpp-terminal::cpp-terminal)
endif()
