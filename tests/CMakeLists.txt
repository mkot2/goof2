# Each unit test typically completes in well under a second. A five second
# timeout per test provides a buffer for slower environments while keeping
# the overall suite under roughly one minute. The CLI evaluation test spins
# up a subprocess and receives a slightly higher limit.
option(enableFuzz "Enable fuzz tests" OFF)

add_executable(vm_execute_tests
    test_execute.cxx
)

target_link_libraries(vm_execute_tests PRIVATE
    vm
    Warnings
)
target_precompile_headers(vm_execute_tests REUSE_FROM vm)

add_test(NAME vm_execute_tests COMMAND vm_execute_tests)
set_tests_properties(vm_execute_tests PROPERTIES TIMEOUT 5)

add_executable(vm_alloc_fail_tests
    test_alloc_fail.cxx
)

target_link_libraries(vm_alloc_fail_tests PRIVATE
    vm
    Warnings
)
target_precompile_headers(vm_alloc_fail_tests REUSE_FROM vm)

add_test(NAME vm_alloc_fail_tests COMMAND vm_alloc_fail_tests)
set_tests_properties(vm_alloc_fail_tests PROPERTIES TIMEOUT 5)

add_executable(vm_memory_model_tests
    test_memory_models.cxx
)

target_link_libraries(vm_memory_model_tests PRIVATE
    vm
    Warnings
)
target_precompile_headers(vm_memory_model_tests REUSE_FROM vm)

add_test(NAME vm_memory_model_tests COMMAND vm_memory_model_tests)
set_tests_properties(vm_memory_model_tests PROPERTIES TIMEOUT 5)

add_executable(vm_load_file_tests
    test_load.cxx
)

target_link_libraries(vm_load_file_tests PRIVATE
    vm
    Warnings
)
target_precompile_headers(vm_load_file_tests REUSE_FROM vm)

add_test(NAME vm_load_file_tests COMMAND vm_load_file_tests)
set_tests_properties(vm_load_file_tests PROPERTIES TIMEOUT 5)

if(enableFuzz)
    add_executable(vm_execute_fuzz
        fuzz_execute.cxx
    )

    target_link_libraries(vm_execute_fuzz PRIVATE
        vm
        Warnings
    )
    target_precompile_headers(vm_execute_fuzz REUSE_FROM vm)

    add_test(NAME vm_execute_fuzz COMMAND vm_execute_fuzz)
    set_tests_properties(vm_execute_fuzz PROPERTIES TIMEOUT 2 LABELS fuzz)
endif()

if(NOT GOOF2_ENABLE_REPL)
    add_executable(vm_cli_eval_tests
        test_cli_eval.cxx
    )

    target_link_libraries(vm_cli_eval_tests PRIVATE
        Warnings
    )
    target_precompile_headers(vm_cli_eval_tests REUSE_FROM vm)

    target_compile_definitions(vm_cli_eval_tests PRIVATE
        GOOF2_EXE_PATH="$<TARGET_FILE:goof2>"
    )

    add_test(NAME vm_cli_eval_tests COMMAND vm_cli_eval_tests)
    set_tests_properties(vm_cli_eval_tests PROPERTIES TIMEOUT 15)
endif()

