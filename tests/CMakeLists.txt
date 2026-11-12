if(NOT KRC_BUILD_TARGET_RUNTIME)
    message(FATAL_ERROR "KRC_BUILD_TESTS requires KRC_BUILD_TARGET_RUNTIME")
endif()

add_executable(krc_tests
    unit/main.cpp
    unit/test_source_manager.cpp
    unit/test_diagnostics.cpp
    unit/test_ir.cpp
    unit/test_irgen.cpp
    unit/test_kr32_encoder.cpp
    unit/test_kr16_encoder.cpp
    unit/test_kr16_assembler.cpp
    unit/test_kr16_image.cpp
    unit/test_kr16_object.cpp
    unit/test_assembler.cpp
    unit/test_register_allocator.cpp
    unit/test_kro_object.cpp
    unit/test_linker.cpp
    unit/test_kdbg.cpp
    unit/test_archive.cpp
    unit/test_lexer.cpp
    unit/test_parser.cpp
    unit/test_preprocessor.cpp
    unit/test_sema.cpp
    unit/test_sysroot_bits.cpp
    unit/test_target.cpp
    unit/test_fixture_extractor.cpp
)
target_link_libraries(krc_tests PRIVATE krc_core krc_fixture_extractor)
target_include_directories(krc_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/doctest")
target_compile_definitions(krc_tests PRIVATE KRC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_features(krc_tests PRIVATE cxx_std_20)
krc_apply_warnings(krc_tests)
krc_apply_sanitizers(krc_tests)

add_test(NAME krc_tests COMMAND krc_tests)

add_test(NAME krc_tree_boundary
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTreeBoundary.cmake)

if(TARGET krc)
    add_test(NAME krc_version COMMAND $<TARGET_FILE:krc> --version)
    set_tests_properties(krc_version PROPERTIES PASS_REGULAR_EXPRESSION "krc 0\\.1\\.0")

    add_test(NAME krc_list_targets COMMAND $<TARGET_FILE:krc> --list-targets)
    set_tests_properties(krc_list_targets PROPERTIES PASS_REGULAR_EXPRESSION "kr32")

    add_test(NAME krc_unknown_target COMMAND $<TARGET_FILE:krc> --target=unknown)
    set_tests_properties(krc_unknown_target PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_kr16_syntax
        COMMAND $<TARGET_FILE:krc> --target=kr16 -std=c11 -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sizes.c")

    add_test(NAME krc_kr16_ir
        COMMAND $<TARGET_FILE:krc> --target=kr16 --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sizes.c")
    set_tests_properties(krc_kr16_ir PROPERTIES PASS_REGULAR_EXPRESSION "; target: kr16")

    add_test(NAME krc_kr16_assembly
        COMMAND $<TARGET_FILE:krc> -S --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c")
    set_tests_properties(krc_kr16_assembly PROPERTIES
        PASS_REGULAR_EXPRESSION "MOV R0, #sum_to")

    add_test(NAME krc_kr16_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_sum.kex")

    add_test(NAME krc_kr16_hello_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_hello.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_hello.kex")

    add_test(NAME krc_kr16_data_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_data.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_data.kex")

    add_test(NAME krc_kr16_string_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_string.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_string.kex")

    add_test(NAME krc_kr16_index_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_index.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_index.kex")

    add_test(NAME krc_kr16_switch_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_switch.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_switch.kex")

    add_test(NAME krc_kr16_call_loop_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_call_loop.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_call_loop.kex")

    add_test(NAME krc_kr16_arith_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arith.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_arith.kex")

    add_test(NAME krc_kr16_arith_object
        COMMAND $<TARGET_FILE:krc> -c --target=kr16 --emit-object
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arith.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_arith.kro")

    add_test(NAME krc_kr16_asm_object
        COMMAND $<TARGET_FILE:krc> -c --target=kr16 --emit-object
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr16/entry.s"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_entry.kro")

    add_test(NAME krc_kr16_crt0_object
        COMMAND $<TARGET_FILE:krc> -c --target=kr16 --emit-object
                "${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_crt0.kro")

    add_test(NAME krc_kr16_link_objects
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arith.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_linked.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)

    add_test(NAME krc_kr16_link_multi
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_lib.c
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_main.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_multi.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Multi.cmake)

    add_test(NAME krc_kr16_link_extern
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_extern_use.c
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_extern_def.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_extern.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Multi.cmake)

    add_test(NAME krc_kr16_link_struct_local
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_struct_local.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_struct_local.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)

    add_test(NAME krc_kr16_link_runtime
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_string_lib.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_runtime.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)

    add_test(NAME krc_kr16_shift_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_shift.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_shift.kex")

    add_test(NAME krc_kr16_pointer_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_pointer.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_pointer.kex")

    add_test(NAME krc_kr16_matrix_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/matrix2d.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_matrix.kex")

    add_test(NAME krc_kr16_fnptr_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_fnptr.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_fnptr.kex")

    add_test(NAME krc_kr16_ptrdiff_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_ptrdiff.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_ptrdiff.kex")

    add_test(NAME krc_kr16_bits_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bitfields_init.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_bits.kex")

    add_test(NAME krc_kr16_bits_designated_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bitfields_designated.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_bits_designated.kex")

    add_test(NAME krc_kr16_bits_nested_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bitfields_nested.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_bits_nested.kex")

    add_test(NAME krc_link_bitfields
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bitfields_init.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/bitfields_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_matrix
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/matrix2d.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/matrix_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_unknown_option COMMAND $<TARGET_FILE:krc> --no-such-option)
    set_tests_properties(krc_unknown_option PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_c_syntax_hello
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c")

    add_test(NAME krc_c_syntax_includes
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/includes.c")

    add_test(NAME krc_sysroot_override
        COMMAND $<TARGET_FILE:krc> --sysroot "${KRC_STAGE_SYSROOT}" -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/includes.c")

    add_test(NAME krc_link_goto
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/goto.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/goto_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_switch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/switch.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/switch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_volatile_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile.c")
    set_tests_properties(krc_volatile_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "load i32, @counter, volatile")

    add_test(NAME krc_volatile_store_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile.c")
    set_tests_properties(krc_volatile_store_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "store i32 2, %local.addr, volatile")

    add_test(NAME krc_volatile_member_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile.c")
    set_tests_properties(krc_volatile_member_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "member.load = load i32, %member.[0-9]+, volatile")

    add_test(NAME krc_link_volatile
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/volatile_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_c_syntax_stdint
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/stdint.c")

    add_test(NAME krc_c_sema_error
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/sema_error.c")
    set_tests_properties(krc_c_sema_error PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_c_threads_unsupported
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/threads_unsupported.c")
    set_tests_properties(krc_c_threads_unsupported PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_c_dump_ast
        COMMAND $<TARGET_FILE:krc> --dump-ast
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c")
    set_tests_properties(krc_c_dump_ast PROPERTIES PASS_REGULAR_EXPRESSION "FunctionDefinition 'main'")

    add_test(NAME krc_c_dump_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c")
    set_tests_properties(krc_c_dump_ir PROPERTIES PASS_REGULAR_EXPRESSION "define i32 @main")

    add_test(NAME krc_c_emit_hello_asm
        COMMAND $<TARGET_FILE:krc> -S
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c")
    set_tests_properties(krc_c_emit_hello_asm PROPERTIES PASS_REGULAR_EXPRESSION "CALL putchar")

    add_test(NAME krc_c_emit_mainentry_asm
        COMMAND $<TARGET_FILE:krc> -S
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mainentry.c")
    set_tests_properties(krc_c_emit_mainentry_asm PROPERTIES PASS_REGULAR_EXPRESSION "CALL printf")

    add_test(NAME krc_c_object
        COMMAND $<TARGET_FILE:krc> -c
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c"
                -o "${KRC_TEST_OUTPUT_DIR}/hello.kro")

    add_test(NAME krc_link_hello
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/hello_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_puts
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/puts.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/puts_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_lengths
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_lengths.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_lengths_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_long_long
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_long_long.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_long_long_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_width
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_width.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_width_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_string_width
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_string_width.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_string_width_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_integer_precision
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_integer_precision.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_integer_precision_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_star
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_star.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_star_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_strings
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strings.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strings_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_logical_truth
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/logical_truth.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/logical_truth_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_sort
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_sort.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_sort_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_args5
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/args5.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/args5_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_sprintf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_sprintf.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_sprintf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_snprintf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_snprintf.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_snprintf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_convert
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_convert.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_convert_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_func_name
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/func_name.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/func_name_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_func_name_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/func_name_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/func_name_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_string_concat
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/string_concat.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/string_concat_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_default_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/default_member.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/default_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_default_member_plain
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/default_member_plain.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/default_member_plain_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_default_member_polymorphic
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/default_member_polymorphic.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/default_member_polymorphic_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_tokenize
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_tokenize.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_tokenize_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_fileio
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_fileio.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_fileio_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_strerror
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strerror.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strerror_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_strerror_r
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strerror_r.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strerror_r_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_locale
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_locale.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_locale_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_stdio
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_stdio.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_stdio_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_string_extra
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_string_extra.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_string_extra_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_memmem
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_memmem.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_memmem_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_pointer_compare
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/pointer_compare.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/pointer_compare_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_strings_case
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strings_case.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strings_case_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_strsep
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strsep.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strsep_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_sscanf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_sscanf.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_sscanf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_dprintf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_dprintf.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_dprintf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_getdelim
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_getdelim.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_getdelim_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_fdopen
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_fdopen.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_fdopen_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_fscanf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_fscanf.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_fscanf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_fputs
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_fputs.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_fputs_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_fpos
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_fpos.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_fpos_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_tmpfile
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_tmpfile.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_tmpfile_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_unistd
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_unistd.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_unistd_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_unistd_io
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_unistd_io.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_unistd_io_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_unistd_dup
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_unistd_dup.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_unistd_dup_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_assert
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_assert.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_assert_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_time
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_time.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_time_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_time_format
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_time_format.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_time_format_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_time_alias
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_time_alias.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_time_alias_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_time_zone
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_time_zone.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_time_zone_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_time_extra
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_time_extra.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_time_extra_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_stdlib_extra
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_stdlib_extra.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_stdlib_extra_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_ctype
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_ctype.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_ctype_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_math_minmax
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_math_minmax.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_math_minmax_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_character_escapes
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/character_escapes.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/character_escapes_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_character_hex_escape
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/character_hex_escape.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/character_hex_escape_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_extern_shared
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/extern_shared_use.c
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/extern_shared_def.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/extern_shared_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntimeTwo.cmake)

    add_test(NAME krc_link_static_global
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_global_one.c
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_global_two.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_global_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntimeTwo.cmake)

    add_test(NAME krc_link_extern_redeclare
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/extern_redeclare.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/extern_redeclare_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_string_array_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/string_array_init.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/string_array_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_string_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_string_array.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_string_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_struct_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_struct_init.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_struct_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_array_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_array_init.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_array_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_pointer_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_pointer_init.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_pointer_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_aggregate_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_aggregate_init.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_aggregate_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_compound_literal
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/compound_literal.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/compound_literal_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_math
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_math.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_math_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_conditional_types
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/conditional_types.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/conditional_types_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_memory
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_memory.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_memory_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_template_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/template_call.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/template_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_default_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_default_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_default_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_default_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_default_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_default_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_with_args
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_with_args.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_with_args_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_member_default_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_default_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_default_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_member_default_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_member_default_ctor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_member_default_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_member_default_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_member_default_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_member_default_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_member_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_member_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_member_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_global_member_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_member_dtor.cpp")
    set_tests_properties(krc_global_member_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "global.member.dtor")

    add_test(NAME krc_link_local_member_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_member_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/local_member_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_local_member_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_member_dtor.cpp")
    set_tests_properties(krc_local_member_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "__krc_member_dtor_Outer")

    add_test(NAME krc_link_delete_member_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_member_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_member_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_delete_member_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_member_dtor.cpp")
    set_tests_properties(krc_delete_member_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "delete.member.dtor")

    add_test(NAME krc_link_delete_array_member_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_member_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_array_member_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_delete_array_member_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_member_dtor.cpp")
    set_tests_properties(krc_delete_array_member_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "__krc_member_dtor_Outer")

    add_test(NAME krc_link_libc_strtod
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_strtod.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_strtod_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_libc_atof
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/libc_atof.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/libc_atof_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_ctor_member_default
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/ctor_member_default.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/ctor_member_default_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_ctor_member_default_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/ctor_member_default.cpp")
    set_tests_properties(krc_ctor_member_default_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "ctor.member.default")

    add_test(NAME krc_link_ctor_base_default
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/ctor_base_default.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/ctor_base_default_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_ctor_base_default_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/ctor_base_default.cpp")
    set_tests_properties(krc_ctor_base_default_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "ctor.base.default")

    add_test(NAME krc_link_derived_dtor_base
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/derived_dtor_base.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/derived_dtor_base_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_derived_dtor_base_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/derived_dtor_base.cpp")
    set_tests_properties(krc_derived_dtor_base_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "@Derived_dtor")

    add_test(NAME krc_link_global_derived_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_derived_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_global_derived_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_dtor.cpp")
    set_tests_properties(krc_global_derived_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "global.base.dtor")

    add_test(NAME krc_link_local_derived_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_derived_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/local_derived_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_local_derived_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_derived_dtor.cpp")
    set_tests_properties(krc_local_derived_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "own.dtor")

    add_test(NAME krc_link_delete_derived_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_derived_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_derived_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_delete_derived_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_derived_dtor.cpp")
    set_tests_properties(krc_delete_derived_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "__krc_member_dtor_Derived")

    add_test(NAME krc_link_delete_array_derived_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_derived_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_array_derived_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_delete_array_derived_dtor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_derived_dtor.cpp")
    set_tests_properties(krc_delete_array_derived_dtor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "__krc_member_dtor_Derived")

    add_test(NAME krc_link_global_derived_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_derived_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_global_derived_ctor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_ctor.cpp")
    set_tests_properties(krc_global_derived_ctor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "global.base.ctor")

    add_test(NAME krc_link_local_derived_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_derived_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/local_derived_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_local_derived_ctor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_derived_ctor.cpp")
    set_tests_properties(krc_local_derived_ctor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "@Base_ctor_0_value")

    add_test(NAME krc_link_new_derived_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_derived_ctor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_derived_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_new_derived_ctor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_derived_ctor.cpp")
    set_tests_properties(krc_new_derived_ctor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "implicit.base.this")

    add_test(NAME krc_link_global_derived_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_member.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_derived_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_global_derived_member_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_derived_member.cpp")
    set_tests_properties(krc_global_derived_member_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "global.default.field")

    add_test(NAME krc_link_generic_selection
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/generic_selection.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/generic_selection_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_generic_cast
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/generic_cast.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/generic_cast_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_decltype_expression
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/decltype_expression.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/decltype_expression_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_float_width
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_float_width.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_float_width_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_dump_symbols
        COMMAND $<TARGET_FILE:krc> --dump-symbols
                ${KRC_TEST_OUTPUT_DIR}/puts_linked.k32)
    set_tests_properties(krc_dump_symbols PROPERTIES
        DEPENDS krc_link_puts
        PASS_REGULAR_EXPRESSION "KDBG target=")

    add_test(NAME krc_link_struct
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/struct_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_struct_param
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct_param.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/struct_param_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_struct_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct_return.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/struct_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_local
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_local.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_local_object_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_init.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_object.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_object_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_object_arg
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_object_arg.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_object_arg_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_object.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_object_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_destructor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_global_destructor_ir
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_destructor.cpp")
    set_tests_properties(krc_global_destructor_ir PROPERTIES
        PASS_REGULAR_EXPRESSION "__krc_destroy_globals")

    add_test(NAME krc_link_global_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_init.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_varargs
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/varargs.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/varargs_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mainentry
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mainentry.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mainentry_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_functionptr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/functionptr.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/functionptr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_aggregate
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/aggregate.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/aggregate_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_floats
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/floats.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/floats_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_debug_struct
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_struct.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_struct_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_bitfield
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bitfield.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/bitfield_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/class.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/class_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/method.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constructor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_newdelete
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/newdelete.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/newdelete_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_recursive_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/recursive_method.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/recursive_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constructor_args
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constructor_args.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constructor_args_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_balanced_tree
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/balanced_tree.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/balanced_tree_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_hello_cpp
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello_cpp.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/hello_cpp_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_copy
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_auto_nullptr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/auto_nullptr.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/auto_nullptr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_range_for
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/range_for.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/range_for_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_enum_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/enum_class.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/enum_class_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_decltype
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/decltype.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/decltype_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_designated
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/designated.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/designated_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_designated
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_designated.c
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_designated_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_namespace
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/namespace.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/namespace_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_qualified_namespace
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/qualified_namespace.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/qualified_namespace_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_namespace
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_namespace.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_namespace_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_default_args
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/default_args.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/default_args_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_method_default
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/method_default.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/method_default_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_explicit
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/explicit_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_reference_param
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_param.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_param_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_method_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/method_reference.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/method_reference_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_reference_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_member.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_operator_plus
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/operator_plus.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/operator_plus_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_free_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/free_operator.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/free_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_structured
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/structured.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/structured_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_if_constexpr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/if_constexpr.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/if_constexpr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_template
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/template.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/template_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_enum_alignas
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/enum_alignas.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/enum_alignas_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inline_two_tu
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inline_a.cpp
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inline_b.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inline_two_tu_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkTwo.cmake)

    add_test(NAME krc_link_inline_class_two_tu
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE1=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inline_class_a.cpp
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inline_class_b.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inline_class_two_tu_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkTwo.cmake)

    add_test(NAME krc_link_if_constexpr_discard
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/if_constexpr_discard.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/if_constexpr_discard_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_ctor_overloads
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/ctor_overloads.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/ctor_overloads_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_copy_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_copy_assign
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_assign.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_assign_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_array.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_call.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_array_objects
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/array_objects.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/array_objects_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_move_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/move_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/move_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_move
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_move.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_move_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_move_assign
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/move_assign.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/move_assign_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_placement_new
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/placement_new.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/placement_new_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_brace_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/brace_init.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/brace_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_brace_aggregate
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/brace_aggregate.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/brace_aggregate_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_range_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/range_init.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/range_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_structured_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/structured_array.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/structured_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_subscript_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/subscript_operator.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/subscript_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_subscript_ref
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/subscript_ref.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/subscript_ref_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inherited_subscript
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inherited_subscript.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inherited_subscript_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inherited_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inherited_call.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inherited_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_named_cast
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/named_cast.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/named_cast_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_call_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/call_operator.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/call_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_compound_assign
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/compound_assign.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/compound_assign_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_member_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_init.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_delegating_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delegating_ctor.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delegating_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_assert
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_assert.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_assert_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_auto_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/auto_return.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/auto_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_unary_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/unary_operator.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/unary_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_incdec_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/incdec_operator.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/incdec_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_branch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_branch.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_branch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_loop
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_loop.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_loop_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_while
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_while.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_while_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_recursion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_recursion.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_recursion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_dispatch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_dispatch.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_dispatch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_array_dtor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_array_dtor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_array_dtor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_reference.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_reference_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_self
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_self.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_self_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_destructor_dispatch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_destructor_dispatch.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_destructor_dispatch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_constructor_dispatch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_constructor_dispatch.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_constructor_dispatch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_virtual_destructor_other
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/virtual_destructor_other.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/virtual_destructor_other_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constructor_once
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constructor_once.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constructor_once_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_member_brace_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_brace_init.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_brace_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_value_initialization
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/value_initialization.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/value_initialization_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_auto_struct
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/auto_struct.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/auto_struct_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_friend_access
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/friend_access.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/friend_access_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_method_data
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_method_data.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_method_data_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_const_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_const_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_const_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_constexpr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_constexpr.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_constexpr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_decltype_copy
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/decltype_copy.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/decltype_copy_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_using_alias
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/using_alias.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/using_alias_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mutable_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mutable_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mutable_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_class.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_class_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_method_args
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_method_args.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_method_args_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_virtual
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_virtual.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_virtual_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_member_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_member_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_member_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_static_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_static_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_static_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_out_of_line_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_out_of_line_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_out_of_line_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_static_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_static_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_static_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_delegating_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delegating_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delegating_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_static_method_args
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_static_method_args.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_static_method_args_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_member_access
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_member_access.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_member_access_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_operator_assign
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_operator_assign.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_operator_assign_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_subscript
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_subscript.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_subscript_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_call_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_call_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_call_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_increment
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_increment.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_increment_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_decrement
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_decrement.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_decrement_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_less
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_less.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_less_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_bool_type
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bool_type.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/bool_type_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_bool_operators
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/bool_operators.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/bool_operators_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_conversion_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/conversion_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/conversion_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_explicit_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/explicit_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_conversion_while
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/conversion_while.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/conversion_while_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_cast_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_cast_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_cast_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_cast_int
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_cast_int.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_cast_int_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_implicit_int_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/implicit_int_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/implicit_int_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_argument_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/argument_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/argument_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_int_conversion_condition
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/int_conversion_condition.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/int_conversion_condition_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_int_conversion_not
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/int_conversion_not.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/int_conversion_not_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_double_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/double_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/double_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_implicit_double_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/implicit_double_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/implicit_double_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_argument_double_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/argument_double_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/argument_double_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_double_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_double_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_double_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_float_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/float_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/float_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_argument_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/argument_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/argument_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_string
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_string.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_string_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_argument_string_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/argument_string_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/argument_string_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_string_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_string_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_string_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_bool
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_bool.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_bool_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_nullptr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_nullptr.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_nullptr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_braced
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_braced.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_braced_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_delete_array_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_array_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_unqualified_static_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/unqualified_static_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/unqualified_static_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_float_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/float_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/float_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_double_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/double_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/double_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_converting_float
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/converting_float.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/converting_float_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_float_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_float_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_float_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mixed_float_arguments
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mixed_float_arguments.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mixed_float_arguments_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mixed_float_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mixed_float_return.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mixed_float_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mixed_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mixed_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mixed_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_functional
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_functional.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_functional_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_init_functional
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/init_functional.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/init_functional_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_local_array_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_array_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/local_array_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_local_array_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/local_array_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/local_array_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_early_return_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/early_return_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/early_return_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_member_array_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_array_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_array_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_member_array_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_array_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_array_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_global_array_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/global_array_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/global_array_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_delete_member_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_member_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_member_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_member_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_member_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_member_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_local_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_local_object.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_local_object_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_local_once
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_local_once.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_local_once_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_local_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_local_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_local_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_delete_array_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_array_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_array_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_array_once
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_array_once.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_array_once_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_using_base
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/using_base.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/using_base_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inherited_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inherited_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inherited_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_function
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_function.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_function_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_if_constexpr_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/if_constexpr_call.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/if_constexpr_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_final_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/final_class.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/final_class_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_defaulted_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/defaulted_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/defaulted_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_constexpr_static
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constexpr_static.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constexpr_static_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_noexcept_function
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/noexcept_function.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/noexcept_function_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_namespace_alias
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/namespace_alias.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/namespace_alias_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_nested_namespace_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_namespace_call.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_namespace_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inline_namespace
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inline_namespace.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inline_namespace_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_using_namespace
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/using_namespace.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/using_namespace_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_trailing_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/trailing_return.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/trailing_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_copy_member_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_member_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_member_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_assign_member_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/assign_member_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/assign_member_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_alignof
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/alignof.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/alignof_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_postincrement
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_postincrement.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_postincrement_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_out_of_line_postdecrement
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_postdecrement.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_postdecrement_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_decltype_variable
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/decltype_variable.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/decltype_variable_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_range_for_records
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/range_for_records.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/range_for_records_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_range_for_auto
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/range_for_auto.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/range_for_auto_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_range_for_auto_ref
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/range_for_auto_ref.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/range_for_auto_ref_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_friend_function
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/friend_function.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/friend_function_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_mutable_counter
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/mutable_counter.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/mutable_counter_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_friend_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/friend_class.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/friend_class_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_reinterpret_cast
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reinterpret_cast.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reinterpret_cast_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_enum_class_switch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/enum_class_switch.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/enum_class_switch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_if_constexpr_false
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/if_constexpr_false.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/if_constexpr_false_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_move_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/move_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/move_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_move_assignment_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/move_assignment_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/move_assignment_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_extern_c
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/extern_c.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/extern_c_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_extern_c_block
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/extern_c_block.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/extern_c_block_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_abstract_override
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/abstract_override.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/abstract_override_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_abstract_pointer
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/abstract_pointer.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/abstract_pointer_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_pure_virtual_destructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/pure_virtual_destructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/pure_virtual_destructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_const_cast
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_cast.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/const_cast_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_static_cast_ref
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_cast_ref.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/static_cast_ref_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_sizeof_record
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/sizeof_record.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/sizeof_record_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_record_logical_and
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/record_logical_and.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/record_logical_and_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_record_logical_or
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/record_logical_or.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/record_logical_or_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_float_precision
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_float_precision.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_float_precision_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_printf_float_forms
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/printf_float_forms.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/printf_float_forms_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_environment
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/environment.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/environment_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_atexit
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/atexit.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/atexit_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_quick_exit
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/quick_exit.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/quick_exit_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_quick_exit_handlers
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/quick_exit_handlers.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/quick_exit_handlers_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_const_global
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_global.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/const_global_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_braced
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_braced.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_braced_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_functional_braced
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/functional_braced.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/functional_braced_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_method
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_method.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_method_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_braced
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_braced.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_braced_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_condition
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_condition.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_condition_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_logical
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_logical.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_logical_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_implicit
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_implicit.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_implicit_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_return_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_argument
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_argument_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_argument_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_assignment
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_assignment_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_assignment_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_comparison
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_comparison.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_comparison_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_float_comparison
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_float_comparison.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_float_comparison_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_double_conversion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_double_conversion.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_double_conversion_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_arithmetic
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_arithmetic.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_arithmetic_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_static_assert_fail
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_assert_fail.cpp")
    set_tests_properties(krc_cxx_static_assert_fail PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_link_delete_null
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/delete_null.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/delete_null_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_struct_ctor_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct_ctor_init.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/struct_ctor_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_pure_virtual_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/pure_virtual_call.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/pure_virtual_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_return.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_capture
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_capture.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_capture_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_ref_capture
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_ref_capture.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_ref_capture_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_init_capture
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_init_capture.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_init_capture_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_copy_all
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_copy_all.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_copy_all_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lambda_ref_all
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lambda_ref_all.cpp
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lambda_ref_all_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_c_preprocess
        COMMAND $<TARGET_FILE:krc> -E
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/includes.c")
    set_tests_properties(krc_c_preprocess PROPERTIES PASS_REGULAR_EXPRESSION "int printf")

    add_test(
        NAME krc_missing_file
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:krc>
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckMissingFile.cmake"
    )

    add_test(NAME krc_determinism_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism1.kro
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism2.kro
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckObjectDeterminism.cmake)

    add_test(NAME krc_determinism_kr16_image
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRC_TARGET=kr16
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism1.kex
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism2.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckObjectDeterminism.cmake)

    add_test(NAME krc_determinism_kr16_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRC_TARGET=kr16
                -DKRC_ARGS=--emit-object
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arith.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism1.kro
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism2.kro
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckObjectDeterminism.cmake)

    add_test(NAME krc_determinism_kr16_link
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arith.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism1_linked.kex
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism2_linked.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckLinkDeterminism.cmake)

    add_test(NAME krc_debug_paths_relative
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
                -DCRT0=${KRC_STAGE_RUNTIME}/crt0.s
                -DROOT=${CMAKE_SOURCE_DIR}
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_paths.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDebugPaths.cmake)

    krc_add_cli_determinism_test(krc)
endif()

if(TARGET krld)
    add_test(NAME krld_version COMMAND $<TARGET_FILE:krld> --version)
    set_tests_properties(krld_version PROPERTIES PASS_REGULAR_EXPRESSION "krld 0\\.1\\.0")
endif()
