add_executable(vm_execute_tests
    test_execute.cxx
)

target_link_libraries(vm_execute_tests PRIVATE
    vm
    Warnings
)

add_test(NAME vm_execute_tests COMMAND vm_execute_tests)

add_executable(vm_alloc_fail_tests
    test_alloc_fail.cxx
)

target_link_libraries(vm_alloc_fail_tests PRIVATE
    vm
    Warnings
)

add_test(NAME vm_alloc_fail_tests COMMAND vm_alloc_fail_tests)

add_executable(vm_memory_model_tests
    test_memory_models.cxx
)

target_link_libraries(vm_memory_model_tests PRIVATE
    vm
    Warnings
)

add_test(NAME vm_memory_model_tests COMMAND vm_memory_model_tests)

add_executable(vm_load_file_tests
    test_load.cxx
)

target_link_libraries(vm_load_file_tests PRIVATE
    vm
    Warnings
)

add_test(NAME vm_load_file_tests COMMAND vm_load_file_tests)

add_executable(vm_execute_fuzz
    fuzz_execute.cxx
)

target_link_libraries(vm_execute_fuzz PRIVATE
    vm
    Warnings
)
