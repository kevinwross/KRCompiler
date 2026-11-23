if(NOT KRC_BUILD_TARGET_RUNTIME)
    message(FATAL_ERROR "KRC_BUILD_TESTS requires KRC_BUILD_TARGET_RUNTIME")
endif()

add_executable(krc_tests
    unit/main.cpp
    unit/test_source_manager.cpp
    unit/test_diagnostics.cpp
    unit/test_ir.cpp
    unit/test_irgen.cpp
    unit/test_machine_ir.cpp
    unit/test_machine_liveness.cpp
    unit/test_mem2reg.cpp
    unit/test_kr32_encoder.cpp
    unit/test_kr16_encoder.cpp
    unit/test_kr16_assembler.cpp
    unit/test_kr16_image.cpp
    unit/test_kr16_object.cpp
    unit/test_kr16_sections.cpp
    unit/test_assembler.cpp
    unit/test_register_allocator.cpp
    unit/test_kro_object.cpp
    unit/test_linker.cpp
    unit/test_kdbg.cpp
    unit/test_kdbg_v11.cpp
    unit/test_archive.cpp
    unit/test_lexer.cpp
    unit/test_parser.cpp
    unit/test_preprocessor.cpp
    unit/test_sema.cpp
    unit/test_sysroot_bits.cpp
    unit/test_kr232_encoder.cpp
    unit/test_kr232_parser.cpp
    unit/test_kr232_assembler.cpp
    unit/test_kr232_image.cpp
    unit/test_kr232_object.cpp
    unit/test_kr232_roundtrip.cpp
    unit/test_kr232_descriptor.cpp
    unit/test_target.cpp
    unit/test_kr216_encoder.cpp
    unit/test_kr216_object.cpp
    unit/test_kr216_image.cpp
    unit/test_fixture_extractor.cpp
    unit/test_toolchain.cpp
)
target_link_libraries(krc_tests PRIVATE krc::toolchain krc_fixture_extractor)
target_include_directories(krc_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_include_directories(krc_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/doctest")
target_compile_definitions(krc_tests PRIVATE KRC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_features(krc_tests PRIVATE cxx_std_20)
krc_apply_warnings(krc_tests)

# The VM file-service tests need a writable host directory. The default is a
# dedicated directory under the test output; a sandboxed runner can point this
# at a writable location instead of weakening the test.
set(KRC_TEST_FS_ROOT "" CACHE PATH "Writable root for VM filesystem tests")
function(krc_configure_fs_root name)
    if(KRC_TEST_FS_ROOT)
        set(root "${KRC_TEST_FS_ROOT}/${name}")
    else()
        set(root "${KRC_TEST_OUTPUT_DIR}/${name}")
    endif()
    file(MAKE_DIRECTORY "${root}")
    set(KRC_FS_ROOT "${root}" PARENT_SCOPE)
endfunction()
# doctest 2.4.11 unconditionally includes <ciso646> when the compiler is Clang
# (doctest.h:498), to detect libc++ through _LIBCPP_VERSION. libstdc++ 15 answers that
# include with a #warning -- "<ciso646> is not a standard header since C++20" -- and
# Clang routes #warning through -W#warnings, which -Werror makes fatal. The diagnostic is
# not ours and cannot be fixed from our code, so it is demoted rather than silenced: the
# warning still prints, and every project source stays under -Werror.
#
# Measured, because "demoted" sounds like it could hide one of ours: a #warning written in
# a test file still fails the build. -Wpedantic reports it as a C++23 extension, and that
# error is untouched by -Wno-error=#warnings. Only a #warning from a *system* header is
# demoted, which is exactly the ciso646 case and nothing else. Upstream tracks the include
# as doctest issue #900, and a doctest upgrade that drops it retires this line.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(krc_tests PRIVATE "-Wno-error=#warnings")
endif()
krc_apply_sanitizers(krc_tests)

add_test(NAME krc_tests COMMAND krc_tests)

add_test(NAME krc_tree_boundary
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckTreeBoundary.cmake)

if(TARGET krc)
    add_test(NAME krc_version COMMAND $<TARGET_FILE:krc> --version)
    set_tests_properties(krc_version PROPERTIES PASS_REGULAR_EXPRESSION "krc 0\\.2\\.0")

    # 4.10: a krc-built `.kex` decodes through the CLI with sources, lines,
    # functions and a variable, and the payload records the image base.
    add_test(NAME krc_kr16_kdbg_decode
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_kdbg.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckKr16Kdbg.cmake)

    add_test(NAME krc_list_targets COMMAND $<TARGET_FILE:krc> --list-targets)
    set_tests_properties(krc_list_targets PROPERTIES PASS_REGULAR_EXPRESSION "kr32")

    add_test(NAME krc_unknown_target COMMAND $<TARGET_FILE:krc> --target=unknown)
    set_tests_properties(krc_unknown_target PROPERTIES WILL_FAIL TRUE)

    # kr232 (plan §5, M0): the target is named and has no backend. The expected
    # message is the one M2's profile row produces; at M0 the tool still answers
    # "unknown target", so this test is red until that row lands. It is the
    # failing test the guide's step 1 asks for, and M2 turns it green without
    # editing it.
    # The M0 refusal test asked for "has no code generator yet"; that premise is
    # obsolete now that the backend exists, so it is replaced by the two checks M3
    # promises: the profile reports a backend, and -S reaches it with a prologue.
    add_test(NAME krc_kr232_has_backend
        COMMAND $<TARGET_FILE:krc> --list-targets)
    set_tests_properties(krc_kr232_has_backend PROPERTIES
        PASS_REGULAR_EXPRESSION "kr232  register  pointer=32  data=ilp32  backend=yes")

    add_test(NAME krc_kr232_prologue
        COMMAND $<TARGET_FILE:krc> --target=kr232 -S
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_min.c")
    set_tests_properties(krc_kr232_prologue PROPERTIES
        PASS_REGULAR_EXPRESSION "enter #0, #0")

    # The kr232 sysroot's widths against the profile: 8, 16 and 32-bit integers,
    # a 32-bit pointer, and no 64-bit integer type at all, because the ABI records
    # long_long_bits as zero.
    add_test(NAME krc_kr232_sysroot_widths
        COMMAND $<TARGET_FILE:krc> --target=kr232 -fsyntax-only
                --sysroot ${CMAKE_SOURCE_DIR}/sysroot
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_stdint.c")

    # A frame-size check at the boundary: one int local is one 4-byte slot, so the
    # prologue's ENTER immediate must be 4.
    add_test(NAME krc_kr232_frame
        COMMAND $<TARGET_FILE:krc> --target=kr232 -S
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_frame.c")
    set_tests_properties(krc_kr232_frame PROPERTIES
        PASS_REGULAR_EXPRESSION "enter #0, #0")

    # A real .k232 produced by `krc -c` and `krld --entry=main --base=0x200` from a
    # two-instruction .s file. The decode says the container carries a .kdbg with
    # the fixture's source, both lines and the main function.
    add_test(NAME krc_kr232_dump_symbols
        COMMAND $<TARGET_FILE:krc> --dump-symbols
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr232-fixture.k232")
    set_tests_properties(krc_kr232_dump_symbols PROPERTIES
        PASS_REGULAR_EXPRESSION "KDBG target=5[^\n]*lines=2 functions=1")

    # The same container executed under the kr232 emulator: it pushes R2, asks to
    # exit with R7 = 0 and the emulator returns that as its process status. The
    # second test asks for the wrong status on purpose, so the runner's message is
    # exercised rather than assumed.
    if(KRC_KR232_VM AND EXISTS "${KRC_KR232_VM}")
        add_test(NAME krc_kr232_run_fixture
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR232_VM}
                    -DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr232-fixture.k232
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr232Image.cmake)
        add_test(NAME krc_kr232_run_wrong_exit
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR232_VM}
                    -DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr232-fixture.k232
                    -DEXPECT_EXIT=7
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr232Image.cmake)
        set_tests_properties(krc_kr232_run_wrong_exit PROPERTIES WILL_FAIL TRUE)

        # The drawing fixture: one pixel into the back surface, a flip, a clean
        # exit. The hash pins the rendered screen, and the wrong-hash test proves
        # the comparison runs.
        add_test(NAME krc_kr232_run_draw
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR232_VM}
                    -DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr232-draw.k232
                    -DEXPECT_EXIT=0
                    -DFRAME_HASH=e8bda242f061f369
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr232Image.cmake)
        add_test(NAME krc_kr232_wrong_frame
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR232_VM}
                    -DIMAGE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr232-draw.k232
                    -DEXPECT_EXIT=0
                    -DFRAME_HASH=0000000000000000
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr232Image.cmake)
        set_tests_properties(krc_kr232_wrong_frame PROPERTIES WILL_FAIL TRUE)

        # The first compiled programs: a byte stored to a frame slot and loaded
        # back, and a constant return. Both go through crt0 so the status `main`
        # returns is the status the process exits with.
        add_test(NAME krc_kr232_run_charload
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_charload.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=65
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        add_test(NAME krc_kr232_run_return42
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_return42.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        add_test(NAME krc_kr232_run_add
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_add.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        # Both sides of one branch: the false path falls through to 42, the true
        # path takes the early return. A single test would not show the branch is
        # decided rather than always taken.
        add_test(NAME krc_kr232_run_branch_false
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_branch.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        add_test(NAME krc_kr232_run_branch_true
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_branch_true.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=1
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        # By-value parameters: a char and a short arrive in R2 and R3 and the sum
        # comes back in R2, so 65 + 0 proves the call and both narrow loads.
        add_test(NAME krc_kr232_run_params
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_params.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=65
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)
        # The runtime's console path: putchar_ writes the terminal page and the
        # transcript is what a reader would see.
        add_test(NAME krc_kr232_run_transcript
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DLIBC=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/libc.c
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_hello.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=0
                    -DEXPECT_TRANSCRIPT=Hi!
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # The drawing packages: gfx_fill_rect into the back surface, then a flip.
        # The hash differs from a flip-only program's, which is what makes this
        # evidence that the pixels land rather than that the program halted.
        add_test(NAME krc_kr232_run_gfx_draw
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DLIBC=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/gfx.c
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_draw.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DINCLUDE=${CMAKE_SOURCE_DIR}/sysroot/kr232/include
                    -DEXPECT_EXIT=0
                    -DEXPECT_FRAME=b93590ec9d504765
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # The console package writes the console page, and the emulator's
        # transcript prints those bytes, so the text a program wrote is readable.
        add_test(NAME krc_kr232_run_cons
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DLIBC=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/cons.c
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_cons.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DINCLUDE=${CMAKE_SOURCE_DIR}/sysroot/kr232/include
                    -DEXPECT_EXIT=0
                    -DEXPECT_TRANSCRIPT=cons!
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # The three-char record by value: the copy must be size-exact, so 1 + 2 + 3
        # is 6 only if each member landed at its own byte.
        add_test(NAME krc_kr232_run_struct
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_struct.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=6
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # A record returned by value: the callee copies into the destination the
        # caller names in R6, and 4 + 5 + 6 is 15 only if all three bytes arrived.
        add_test(NAME krc_kr232_run_sret
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_sret.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=15
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # A call whose result is a record argument: the inner call returns into a
        # temporary and the outer takes it as a memory-class parameter.
        add_test(NAME krc_kr232_run_nested
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_nested.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=15
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # A virtual call through a base pointer reaches the override: 2, not 4.
        add_test(NAME krc_kr232_run_virtual
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_virtual.cpp
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=2
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # The reserved typeinfo word below the vptr must be present and zero: this
        # program reads it and returns it, so 0 is the assertion.
        add_test(NAME krc_kr232_run_typeinfo
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DVM=${KRC_KR232_VM}
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_typeinfo.cpp
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/BuildAndRunKr232.cmake)

        # The keyboard package has no way to be exercised without input injection,
        # so its check is that it compiles against the profile's headers.
        add_test(NAME krc_kr232_input_compiles
            COMMAND $<TARGET_FILE:krc> -c --target kr232
                    -I ${CMAKE_SOURCE_DIR}/sysroot/kr232/include
                    ${CMAKE_SOURCE_DIR}/targets/kr232/runtime/input.c
                    -o "${KRC_TEST_OUTPUT_DIR}/kr232_input.o")

        # The KDBG invariants a debugger relies on, on a compiled image: target 5,
        # a line and a function present, and every line inside a function span.
        add_test(NAME krc_kr232_kdbg_invariants
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_add.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/CheckKr232Kdbg.cmake)


        # Two builds of one program must be byte-identical: a frame hash or an
        # image hash is only meaningful if the image is reproducible.
        add_test(NAME krc_kr232_determinism
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr232/runtime/crt0.s
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr232_add.c
                    -DWORK=${KRC_TEST_OUTPUT_DIR}
                    -P ${CMAKE_SOURCE_DIR}/cmake/CheckKr232Determinism.cmake)
    endif()

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
    # A call target is an address, not a sixteen-bit immediate: the assembler
    # cannot resolve a symbol to one, so the call materialises it with LEA.
    set_tests_properties(krc_kr16_assembly PROPERTIES
        PASS_REGULAR_EXPRESSION "LEA R0, .sum_to.")

    add_test(NAME krc_kr16_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_sum.kex")

    # `.bss` reserves a zero-filled section with no file bytes. The image test
    # proves it assembles and links; the gated run proves the loader zero-fills it.
    add_test(NAME krc_kr16_bss_image
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr16/bss_zero.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_bss.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/AssembleKr16.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_bss
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_bss.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_bss PROPERTIES
            DEPENDS krc_kr16_bss_image)
    endif()

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

    add_test(NAME krc_kr216_asm_object
        COMMAND $<TARGET_FILE:krc> -c --target=kr216
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr216/hello.s"
                -o "${KRC_TEST_OUTPUT_DIR}/kr216_hello.kro")
    add_test(NAME krc_kr216_object_header
        COMMAND ${CMAKE_COMMAND}
                -DOBJECT=${KRC_TEST_OUTPUT_DIR}/kr216_hello.kro
                -DTARGET_ID=4
                -DPOINTER_WIDTH=16
                -DSECTION_COUNT=1
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckKroHeader.cmake)
    set_tests_properties(krc_kr216_object_header PROPERTIES
        DEPENDS krc_kr216_asm_object)

    add_test(NAME krc_kr216_link_image
        COMMAND $<TARGET_FILE:krld>
                "${KRC_TEST_OUTPUT_DIR}/kr216_hello.kro"
                -o "${KRC_TEST_OUTPUT_DIR}/kr216_hello.k216")
    set_tests_properties(krc_kr216_link_image PROPERTIES
        DEPENDS krc_kr216_asm_object)
    add_test(NAME krc_kr216_image_header
        COMMAND ${CMAKE_COMMAND}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr216_hello.k216
                -DENTRY=0x0200
                -DENTRY_HEX=0002
                -DSECTION_COUNT=2
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckK216Header.cmake)
    set_tests_properties(krc_kr216_image_header PROPERTIES
        DEPENDS krc_kr216_link_image)

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

    # A virtual call must reach the second vtable slot. The stride was the literal 4 while a KR16
    # pointer is two bytes, so slot 1 read the second entry and called whatever sat there; the
    # program raised an unhandled exception instead of returning a wrong value.
    add_test(NAME krc_link_kr16_virtual_two_slots
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_virtual_two_slots.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_virtual_two_slots.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)

    # The executed KR16 tests report the program's exit status from the sibling
    # VM; a host without a runnable VM registers none of them, as for KR32.
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_pack_template_parameter
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_pack_template_parameter.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_pack_template_parameter PROPERTIES
            DEPENDS krc_link_kr16_pack_template_parameter)
        add_test(NAME krc_run_kr16_virtual_two_slots
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_virtual_two_slots.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_virtual_two_slots PROPERTIES
            DEPENDS krc_link_kr16_virtual_two_slots)
        add_test(NAME krc_run_kr16_recursive_variadic
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_recursive_variadic.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_recursive_variadic PROPERTIES
            DEPENDS krc_link_kr16_recursive_variadic)
        add_test(NAME krc_run_kr16_partial_specialization
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_partial_specialization.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_partial_specialization PROPERTIES
            DEPENDS krc_link_kr16_partial_specialization)
        add_test(NAME krc_run_kr16_variadic_class
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_class.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_variadic_class PROPERTIES
            DEPENDS krc_link_kr16_variadic_class)
        add_test(NAME krc_run_kr16_namespace_template
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_namespace_template.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_namespace_template PROPERTIES
            DEPENDS krc_link_kr16_namespace_template)
        add_test(NAME krc_run_kr16_new_pack_expansion
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_new_pack_expansion.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_new_pack_expansion PROPERTIES
            DEPENDS krc_link_kr16_new_pack_expansion)
        add_test(NAME krc_run_kr16_branch_promotion
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_branch_promotion.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_branch_promotion PROPERTIES
            DEPENDS krc_link_kr16_branch_promotion)
        add_test(NAME krc_run_kr16_gc_sections
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_gc_sections.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_gc_sections PROPERTIES
            DEPENDS krc_link_kr16_gc_sections)
        add_test(NAME krc_run_kr16_pack_expansion
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_pack_expansion.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_pack_expansion PROPERTIES
            DEPENDS krc_link_kr16_pack_expansion)
        krc_configure_fs_root(kr16_file_layer)
        add_test(NAME krc_run_kr16_file_layer
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_file_layer.kex
                    -DFS_ROOT=${KRC_FS_ROOT}
                    -DFS_QUOTA=1G
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_file_layer PROPERTIES
            DEPENDS krc_link_kr16_file_layer)
        add_test(NAME krc_run_kr16_functional
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_functional.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_functional PROPERTIES
            DEPENDS krc_link_kr16_functional)
        add_test(NAME krc_run_kr16_call_operator_reference
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_call_operator_reference.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_call_operator_reference PROPERTIES
            DEPENDS krc_link_kr16_call_operator_reference)
        add_test(NAME krc_run_kr16_template_shared
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_template_shared.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_template_shared PROPERTIES
            DEPENDS krc_link_kr16_template_shared)
        add_test(NAME krc_run_kr16_variadic_count
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_count.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_variadic_count PROPERTIES
            DEPENDS krc_link_kr16_variadic_count)
        add_test(NAME krc_run_kr16_record_copy
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_record_copy.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_record_copy PROPERTIES
            DEPENDS krc_link_kr16_record_copy)
        add_test(NAME krc_run_kr16_dynamic_cast_void
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_dynamic_cast_void.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_dynamic_cast_void PROPERTIES
            DEPENDS krc_link_kr16_dynamic_cast_void)
        add_test(NAME krc_run_kr16_moved_record
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_moved_record.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_moved_record PROPERTIES
            DEPENDS krc_link_kr16_moved_record)
        add_test(NAME krc_run_kr16_typeinfo_members
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_typeinfo_members.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_typeinfo_members PROPERTIES
            DEPENDS krc_link_kr16_typeinfo_members)
        add_test(NAME krc_run_kr16_member_pointer_type
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_member_pointer_type.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_member_pointer_type PROPERTIES
            DEPENDS krc_link_kr16_member_pointer_type)
        add_test(NAME krc_run_kr16_class_template_signature
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_class_template_signature.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_class_template_signature PROPERTIES
            DEPENDS krc_link_kr16_class_template_signature)
        add_test(NAME krc_run_kr16_record_array_lazy
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_record_array_lazy.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_record_array_lazy PROPERTIES
            DEPENDS krc_link_kr16_record_array_lazy)
        add_test(NAME krc_run_kr16_value_parameter
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_value_parameter.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_value_parameter PROPERTIES
            DEPENDS krc_link_kr16_value_parameter)
        add_test(NAME krc_run_kr16_const_overload
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_const_overload.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_const_overload PROPERTIES
            DEPENDS krc_link_kr16_const_overload)
        add_test(NAME krc_run_kr16_utility
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_utility.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_utility PROPERTIES
            DEPENDS krc_link_kr16_utility)
        add_test(NAME krc_run_kr16_gep_stride
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_gep_stride.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_gep_stride PROPERTIES
            DEPENDS krc_link_kr16_gep_stride)
        add_test(NAME krc_run_kr16_records_array
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_records_array.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_records_array PROPERTIES
            DEPENDS krc_link_kr16_records_array)
        add_test(NAME krc_run_kr16_std_array
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/std_array_kr16.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_std_array PROPERTIES
            DEPENDS krc_link_kr16_std_array)
        add_test(NAME krc_run_kr16_linked
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_linked.kex
                    -DEXPECT_EXIT=49
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_linked PROPERTIES
            DEPENDS krc_kr16_link_objects)

        add_test(NAME krc_run_kr16_multi
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_multi.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_multi PROPERTIES
            DEPENDS krc_kr16_link_multi)

        add_test(NAME krc_run_kr16_extern
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_extern.kex
                    -DEXPECT_EXIT=7
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_extern PROPERTIES
            DEPENDS krc_kr16_link_extern)

        add_test(NAME krc_run_kr16_struct_local
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_struct_local.kex
                    -DEXPECT_EXIT=81
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_struct_local PROPERTIES
            DEPENDS krc_kr16_link_struct_local)

        add_test(NAME krc_run_kr16_runtime
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_runtime.kex
                    -DEXPECT_EXIT=3
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_runtime PROPERTIES
            DEPENDS krc_kr16_link_runtime)

        # 4.13: the shipped kr16 runtime calls `__host_call(n, block)`, which the
        # KR16 backend lowers to SYS #n. The sibling sources are read unmodified;
        # the test is registered only when the checkout is present.
        set(_krc_kr16_source "${CMAKE_SOURCE_DIR}/../kr16")
        if(EXISTS "${_krc_kr16_source}/resources/fileio.c" AND
           EXISTS "${_krc_kr16_source}/tests/kevin/fileio.c")
            add_test(NAME krc_kr16_link_shipped_fileio
                COMMAND ${CMAKE_COMMAND}
                        -DKRC=$<TARGET_FILE:krc>
                        -DKRLD=$<TARGET_FILE:krld>
                        -DKR16_SOURCE=${_krc_kr16_source}
                        -DCRT0=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/kr16/shipped_fileio_crt0.s
                        -DSYSROOT=${KRC_STAGE_SYSROOT}
                        -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_shipped_fileio.kex
                        -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16ShippedFileio.cmake)
            krc_configure_fs_root(kr16_shipped_fileio)
            add_test(NAME krc_run_kr16_shipped_fileio
                COMMAND ${CMAKE_COMMAND}
                        -DVM=${KRC_KR16_VM}
                        -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_shipped_fileio.kex
                        -DEXPECT_EXIT=0
                        -DFS_ROOT=${KRC_FS_ROOT}
                        -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
            set_tests_properties(krc_run_kr16_shipped_fileio PROPERTIES
                DEPENDS krc_kr16_link_shipped_fileio)
        endif()

        # The single-unit images cover codegen shapes the linked fixtures do
        # not: globals with initializers, an index, a switch, a call in a loop,
        # shifts, a pointer write, a function pointer, pointer difference,
        # bit-fields, and a two-dimensional array.
        add_test(NAME krc_run_kr16_hello
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_hello.kex
                    -DEXPECT_EXIT=0
                    "-DEXPECT_OUTPUT=Hello, KR16!"
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_hello PROPERTIES
            DEPENDS krc_kr16_hello_image)

        add_test(NAME krc_run_kr16_data
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_data.kex
                    -DEXPECT_EXIT=6
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_data PROPERTIES
            DEPENDS krc_kr16_data_image)

        add_test(NAME krc_run_kr16_string
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_string.kex
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_string PROPERTIES
            DEPENDS krc_kr16_string_image)

        add_test(NAME krc_run_kr16_index
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_index.kex
                    -DEXPECT_EXIT=3
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_index PROPERTIES
            DEPENDS krc_kr16_index_image)

        add_test(NAME krc_run_kr16_switch
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_switch.kex
                    -DEXPECT_EXIT=15
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_switch PROPERTIES
            DEPENDS krc_kr16_switch_image)

        add_test(NAME krc_run_kr16_call_loop
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_call_loop.kex
                    -DEXPECT_EXIT=15
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_call_loop PROPERTIES
            DEPENDS krc_kr16_call_loop_image)

        add_test(NAME krc_run_kr16_shift
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_shift.kex
                    -DEXPECT_EXIT=32
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_shift PROPERTIES
            DEPENDS krc_kr16_shift_image)

        add_test(NAME krc_run_kr16_pointer
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_pointer.kex
                    -DEXPECT_EXIT=7
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_pointer PROPERTIES
            DEPENDS krc_kr16_pointer_image)

        add_test(NAME krc_run_kr16_matrix
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_matrix.kex
                    -DEXPECT_EXIT=3
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_matrix PROPERTIES
            DEPENDS krc_kr16_matrix_image)

        add_test(NAME krc_run_kr16_fnptr
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_fnptr.kex
                    -DEXPECT_EXIT=5
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_fnptr PROPERTIES
            DEPENDS krc_kr16_fnptr_image)

        add_test(NAME krc_run_kr16_ptrdiff
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_ptrdiff.kex
                    -DEXPECT_EXIT=3
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_ptrdiff PROPERTIES
            DEPENDS krc_kr16_ptrdiff_image)

        add_test(NAME krc_run_kr16_bits
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_bits.kex
                    -DEXPECT_EXIT=22
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_bits PROPERTIES
            DEPENDS krc_kr16_bits_image)

        add_test(NAME krc_run_kr16_bits_designated
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_bits_designated.kex
                    -DEXPECT_EXIT=22
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_bits_designated PROPERTIES
            DEPENDS krc_kr16_bits_designated_image)

        add_test(NAME krc_run_kr16_bits_nested
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_bits_nested.kex
                    -DEXPECT_EXIT=22
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_bits_nested PROPERTIES
            DEPENDS krc_kr16_bits_nested_image)

        add_test(NAME krc_run_kr16_arguments
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_arguments.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_arguments PROPERTIES
            DEPENDS krc_kr16_arguments_image)

        add_test(NAME krc_run_kr16_variadic
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_variadic.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_variadic PROPERTIES
            DEPENDS krc_kr16_variadic_image)

        add_test(NAME krc_run_kr16_printf
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_printf.kex
                    -DEXPECT_EXIT=42
                    "-DEXPECT_OUTPUT=kr16 printf 7 ok -3"
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_printf PROPERTIES
            DEPENDS krc_kr16_link_printf)

        add_test(NAME krc_run_kr16_heap
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_heap.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_heap PROPERTIES
            DEPENDS krc_kr16_link_heap)

        add_test(NAME krc_run_kr16_vsprintf
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_vsprintf.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_vsprintf PROPERTIES
            DEPENDS krc_kr16_link_vsprintf)
        add_test(NAME krc_run_kr16_typedef_tags
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_typedef_tags.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_typedef_tags PROPERTIES
            DEPENDS krc_kr16_typedef_tags_image)
    endif()

    add_test(NAME krc_kr16_arguments_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_arguments.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_arguments.kex")

    add_test(NAME krc_kr16_variadic_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_variadic.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_variadic.kex")

    add_test(NAME krc_kr16_link_vsprintf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_vsprintf.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_vsprintf.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)
    add_test(NAME krc_kr16_typedef_tags_image
        COMMAND $<TARGET_FILE:krc> -c --target=kr16
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/typedef_tags.c"
                -o "${KRC_TEST_OUTPUT_DIR}/kr16_typedef_tags.kex")

    add_test(NAME krc_kr16_link_printf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_printf.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_printf.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)

    add_test(NAME krc_link_destructor_scopes
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/destructor_scopes.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/destructor_scopes_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_std_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_array.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_array_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_logical_operators
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/logical_operators.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/logical_operators_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_non_type_templates
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/non_type_templates.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/non_type_templates_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_kr16_host_services
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_host_services.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_host_services.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)

    # A literal that does not fit its type is diagnosed instead of narrowed:
    # on the KR16 profile `long` is sixteen bits, so this constant has no type
    # that can hold it.
    add_test(NAME krc_cxx_kr16_integer_too_large
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_integer_too_large.c
                -DEXPECT=integer constant is too large for its type
                -DTARGET=kr16
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)
    # A call in a return statement runs once. The forwarding wrapper in the
    # fixture is the shape that exposed a second, discarded call.
    add_test(NAME krc_link_reference_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_return.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_reference_cast_value
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_cast_value.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_cast_value_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_unique_ptr
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/unique_ptr.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/unique_ptr_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_copy_and_move_constructors
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_and_move_constructors.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_and_move_constructors_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_rvalue_reference_binding
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/rvalue_reference_binding.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/rvalue_reference_binding_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_arrow_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/arrow_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/arrow_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_side_effects_once
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/side_effects_once.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/side_effects_once_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_record_comparison
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/record_comparison.cpp
                -DEXPECT=no matching 'operator==' for operands of record type 'Item'
                -DOBJECT=${KRC_TEST_OUTPUT_DIR}/record_comparison.kro
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)
    add_test(NAME krc_cxx_return_record_no_conversion
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_record_no_conversion.cpp
                -DEXPECT=the returned record has no conversion to the return type
                -DOBJECT=${KRC_TEST_OUTPUT_DIR}/return_record_no_conversion.kro
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)
    add_test(NAME krc_link_return_call_once
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_call_once.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_call_once_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    # Corpus fixtures extracted from ../kr32/examples. hello, mainentry and
    # binarytree link and run: they need the C library, so LIBCRT is passed.
    # strings.c links and runs on both targets too — libcrt does provide memmove —
    # and the Tier 1 list below executes it. This note used to name three Tier 1 files
    # (types.c, varargs.c, libc-test.c) as KR32-only for using double or float; two of
    # those are not in the corpus, and varargs.c uses neither and compiles for KR16.
    # The sources the profile actually refuses are asserted by
    # krc_cxx_kr16_rejects_double and krc_cxx_kr16_rejects_longlong above.
    # Tier 2 sources. The first three compile on both targets; cxx_tls.cpp uses
    # long long, which the KR16 profile rejects, so it is KR32-only. matrix.cpp is not
    # extracted: it needs kr32/matrix.hpp, which this sysroot does not carry.
    foreach(corpus_source IN ITEMS
            corpus/extracted/tier2/balanced-tree.cpp
            corpus/extracted/tier2/cxx_libc.cpp
            corpus/extracted/tier2/FileOpen.cpp)
        string(REPLACE "/" "_" corpus_name "${corpus_source}")
        string(REPLACE ".cpp" "" corpus_name "${corpus_name}")
        foreach(corpus_target IN ITEMS kr32 kr16)
            set(corpus_flags -fsyntax-only)
            if(corpus_target STREQUAL "kr16")
                set(corpus_flags -fsyntax-only --target=kr16)
            endif()
            add_test(NAME krc_corpus_${corpus_target}_${corpus_name}
                COMMAND $<TARGET_FILE:krc> ${corpus_flags}
                        -I ${CMAKE_SOURCE_DIR}/sysroot/include
                        --sysroot ${KRC_STAGE_SYSROOT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/${corpus_source})
        endforeach()
    endforeach()

    foreach(corpus_source IN ITEMS
            corpus/extracted/tier2/cxx_tls.cpp
            corpus/extracted/tier1/floats.c)
        string(REPLACE "/" "_" corpus_name "${corpus_source}")
        string(REPLACE ".cpp" "" corpus_name "${corpus_name}")
        string(REPLACE ".c" "" corpus_name "${corpus_name}")
        add_test(NAME krc_corpus_kr32_${corpus_name}
            COMMAND $<TARGET_FILE:krc> -fsyntax-only
                    -I ${CMAKE_SOURCE_DIR}/sysroot/include
                    --sysroot ${KRC_STAGE_SYSROOT}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${corpus_source})
    endforeach()
    foreach(corpus_case IN ITEMS hello:corpus/extracted/tier0/hello.c
                                 mainentry:corpus/extracted/tier1/mainentry.c
                                 binarytree:corpus/extracted/tier1/binarytree.c
                                 strings:corpus/extracted/tier1/strings.c)
        string(FIND "${corpus_case}" ":" separator)
        string(SUBSTRING "${corpus_case}" 0 ${separator} corpus_name)
        math(EXPR after "${separator} + 1")
        string(SUBSTRING "${corpus_case}" ${after} -1 corpus_source)
        add_test(NAME krc_corpus_kr16_link_${corpus_name}
            COMMAND ${CMAKE_COMMAND}
                    -DKRC=$<TARGET_FILE:krc>
                    -DKRLD=$<TARGET_FILE:krld>
                    -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                    -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                    -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                    -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${corpus_source}
                    -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/corpus_${corpus_name}.kex
                    -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
        if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_corpus_kr16_run_${corpus_name}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/corpus_${corpus_name}.kex
                    -DFS_ROOT=${KRC_TEST_OUTPUT_DIR}
                    -DFS_QUOTA=1G
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_corpus_kr16_run_${corpus_name} PROPERTIES
            DEPENDS krc_corpus_kr16_link_${corpus_name})
        endif()
    endforeach()

    foreach(corpus_source IN ITEMS
            corpus/extracted/tier1/hello.cpp)
        string(REPLACE "/" "_" corpus_name "${corpus_source}")
        string(REPLACE ".c" "" corpus_name "${corpus_name}")
        string(REPLACE ".cpp" "" corpus_name "${corpus_name}")
        foreach(corpus_target IN ITEMS kr32 kr16)
            if(corpus_target STREQUAL "kr16")
                add_test(NAME krc_corpus_${corpus_target}_${corpus_name}
                    COMMAND $<TARGET_FILE:krc> -fsyntax-only --target=kr16
                            -I ${CMAKE_SOURCE_DIR}/sysroot/include
                            --sysroot ${KRC_STAGE_SYSROOT}
                            ${CMAKE_CURRENT_SOURCE_DIR}/${corpus_source})
            else()
                add_test(NAME krc_corpus_${corpus_target}_${corpus_name}
                    COMMAND $<TARGET_FILE:krc> -fsyntax-only
                            -I ${CMAKE_SOURCE_DIR}/sysroot/include
                            --sysroot ${KRC_STAGE_SYSROOT}
                            ${CMAKE_CURRENT_SOURCE_DIR}/${corpus_source})
            endif()
        endforeach()
    endforeach()

    add_test(NAME krc_link_kr16_pack_template_parameter
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/pack_template_parameter.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_pack_template_parameter.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_recursive_variadic
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/recursive_variadic.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_recursive_variadic.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_partial_specialization
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/partial_specialization.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_partial_specialization.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_variadic_class
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/variadic_class.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_class.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_namespace_template
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/namespace_template.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_namespace_template.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_new_pack_expansion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_pack_expansion.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_new_pack_expansion.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_branch_promotion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/branch_promotion.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_branch_promotion.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_gc_sections
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/gc_main.cpp
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/gc_unused_object.cpp
                -DGC_SECTIONS=ON
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_gc_sections.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_pack_expansion
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/pack_expansion.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_pack_expansion.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_file_layer
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/file_layer.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_file_layer.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_functional
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/functional.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_functional.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_call_operator_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/call_operator_reference.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_call_operator_reference.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_template_shared
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/template_shared_a.cpp
                -DSOURCE2=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/template_shared_b.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_template_shared.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_variadic_count
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/variadic_count.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_count.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_record_copy
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/record_copy.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_record_copy.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_dynamic_cast_void
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/dynamic_cast_void.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_dynamic_cast_void.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_moved_record
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/moved_record.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_moved_record.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_typeinfo_members
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/typeinfo_members.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_typeinfo_members.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_member_pointer_type
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_pointer_type.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_member_pointer_type.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_class_template_signature
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/class_template_signature.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_class_template_signature.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_record_array_lazy
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/record_array_lazy.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_record_array_lazy.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_value_parameter
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/value_parameter.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_value_parameter.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_const_overload
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_overload.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_const_overload.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_utility
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/utility.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_utility.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_gep_stride
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_gep_stride.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_gep_stride.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_records_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_records_array.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_records_array.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_std_array
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_array_kr16.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_array_kr16.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_kr16_stdio
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_stdio.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_stdio.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    add_test(NAME krc_link_dereference_operator
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/dereference_operator.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/dereference_operator_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_paren_initializer
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/paren_initializer.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/paren_initializer_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_this_subscript
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/this_subscript.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/this_subscript_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_injected_class_name
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/injected_class_name.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/injected_class_name_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_class_templates
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/class_templates.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/class_templates_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_template_functions
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/template_functions.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/template_functions_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_thread_local_storage
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/thread_local_storage.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/thread_local_storage_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_kr16_link_thread_local_storage
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/thread_local_storage.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_thread_local.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Runtime.cmake)
    add_test(NAME krc_debug_names
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_names.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
                -DEH=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/eh.s
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_names.k32
                "-DEXPECT=function Widget::Widget;function Widget::~Widget;function Widget::size;function Widget::doubled;function Counter::bump;function Counter::value;function helper"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDebugNames.cmake)

    add_test(NAME krc_link_debug_names
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_names.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_names_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_link_div_and_vsprintf
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/div_and_vsprintf.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/div_and_vsprintf_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    add_test(NAME krc_kr16_link_heap
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${CMAKE_SOURCE_DIR}/targets/kr16/runtime/crt0.s
                -DRUNTIME=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_heap.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_heap.kex
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

    # The fixture only declares the kr32::matrix functions; it links solely
    # because each call lowers to a tile intrinsic.
    add_test(NAME krc_link_matrix_intrinsics
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/matrix_intrinsics.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/matrix_intrinsics_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    # RTTI: typeid and dynamic_cast read the vtable/typeinfo layout.
    add_test(NAME krc_link_rtti
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/rtti.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/rtti_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    # type_info members: before(), raw_name(), and comparison through a reference.
    add_test(NAME krc_link_typeinfo_members
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/typeinfo_members.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/typeinfo_members_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    # Exceptions: the descriptor table, the unwinder, and catch matching.
    add_test(NAME krc_link_exceptions
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/exceptions.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/exceptions_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_exception_cleanup
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/exception_cleanup.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/exception_cleanup_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_aggregate_arguments
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/aggregate_arguments.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/aggregate_arguments_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_aggregate_values
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/aggregate_values.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/aggregate_values_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_typedef_tags
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/typedef_tags.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/typedef_tags_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_dynamic_cast_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/dynamic_cast_reference.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/dynamic_cast_reference_linked.k32
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

    # A volatile access through a pointer expression, not a name. The qualifier lives on
    # the declared pointer, so the front end has to follow the addition to find it; the
    # three tests pin the load, the store, and that a plain pointer stays unmarked.
    add_test(NAME krc_volatile_pointer_arithmetic_load
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile_pointer_arithmetic.c")
    set_tests_properties(krc_volatile_pointer_arithmetic_load PROPERTIES
        PASS_REGULAR_EXPRESSION "load i32, %ptradd[.0-9]*, volatile")

    add_test(NAME krc_volatile_pointer_arithmetic_store
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile_pointer_arithmetic.c")
    set_tests_properties(krc_volatile_pointer_arithmetic_store PROPERTIES
        PASS_REGULAR_EXPRESSION "store i32 [^,]+, %ptradd[.0-9]*, volatile")

    add_test(NAME krc_volatile_pointer_arithmetic_plain
        COMMAND $<TARGET_FILE:krc> --dump-ir
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/volatile_pointer_arithmetic_plain.c")
    set_tests_properties(krc_volatile_pointer_arithmetic_plain PROPERTIES
        FAIL_REGULAR_EXPRESSION ", volatile")

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

    add_test(NAME krc_c_emit_conflict
        COMMAND $<TARGET_FILE:krc> -S -c
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c"
                -o "${KRC_TEST_OUTPUT_DIR}/emit_conflict.out")
    set_tests_properties(krc_c_emit_conflict PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_c_emit_hello_asm_file
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/hello.c
                -DOUT=${KRC_TEST_OUTPUT_DIR}/hello_emit.s
                "-DEXPECT=CALL putchar"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckAssemblyOutput.cmake)

    add_test(NAME krc_kr16_emit_asm_file
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DTARGET=kr16
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_sum.c
                -DOUT=${KRC_TEST_OUTPUT_DIR}/kr16_emit.s
                "-DEXPECT=JMP|CALL"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckAssemblyOutput.cmake)

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

    add_test(NAME krc_link_longlong_arith
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/longlong_arith.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/longlong_arith_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    # Executed checks, registered only when a VM was configured for this
    # machine. A link-only test cannot see a wrong result in generated code.
    # Every executed test must go inside this block, above its
    # endif: a run test registered outside it runs on a host with no VM.
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        foreach(run_fixture longlong_arith longlong_conditional)
            add_test(NAME krc_run_${run_fixture}
                COMMAND ${CMAKE_COMMAND}
                        -DVM=${KRC_KR32_VM}
                        -DIMAGE=${KRC_TEST_OUTPUT_DIR}/${run_fixture}_linked.k32
                        -DEXPECT_EXIT=0
                        -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
            set_tests_properties(krc_run_${run_fixture} PROPERTIES
                DEPENDS krc_link_${run_fixture})
        endforeach()

        # The 64-bit conversion path: a wrong value still exits 0, so the
        # expected output is part of the check.
        add_test(NAME krc_run_printf_long_long
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/printf_long_long_linked.k32
                    -DEXPECT_EXIT=0
                    "-DEXPECT_OUTPUT=-123 456 1c8"
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_printf_long_long PROPERTIES
            DEPENDS krc_link_printf_long_long)

        add_test(NAME krc_run_const_member_read
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/const_member_read_linked.k32
                    -DEXPECT_EXIT=7
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_const_member_read PROPERTIES
            DEPENDS krc_link_const_member_read)

        add_test(NAME krc_run_const_mutable_member
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/const_mutable_member_linked.k32
                    -DEXPECT_EXIT=5
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_const_mutable_member PROPERTIES
            DEPENDS krc_link_const_mutable_member)

        add_test(NAME krc_run_inherit_member_arith
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/inherit_member_arith_linked.k32
                    -DEXPECT_EXIT=9
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_inherit_member_arith PROPERTIES
            DEPENDS krc_link_inherit_member_arith)

        add_test(NAME krc_run_heap_above_code
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/heap_above_code_linked.k32
                    -DEXPECT_EXIT=7
                    -DMAX_STEPS=80000000
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_heap_above_code PROPERTIES
            DEPENDS krc_link_heap_above_code)

        add_test(NAME krc_run_logical_not_pointer
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/logical_not_pointer_linked.k32
                    -DEXPECT_EXIT=1
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_logical_not_pointer PROPERTIES
            DEPENDS krc_link_logical_not_pointer)

        # A wrong tile result still links and exits 0 only if the fixture's
        # own checks pass; 42 is exact rather than arbitrary.
        add_test(NAME krc_run_matrix_intrinsics
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/matrix_intrinsics_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_matrix_intrinsics PROPERTIES
            DEPENDS krc_link_matrix_intrinsics)

        add_test(NAME krc_run_rtti
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/rtti_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_rtti PROPERTIES
            DEPENDS krc_link_rtti)

        add_test(NAME krc_run_typeinfo_members
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/typeinfo_members_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_typeinfo_members PROPERTIES
            DEPENDS krc_link_typeinfo_members)
        add_test(NAME krc_run_exceptions
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/exceptions_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_exceptions PROPERTIES
            DEPENDS krc_link_exceptions)

        add_test(NAME krc_run_exception_cleanup
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/exception_cleanup_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_exception_cleanup PROPERTIES
            DEPENDS krc_link_exception_cleanup)

        add_test(NAME krc_run_dynamic_cast_reference
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/dynamic_cast_reference_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_dynamic_cast_reference PROPERTIES
            DEPENDS krc_link_dynamic_cast_reference)

        add_test(NAME krc_run_aggregate_arguments
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/aggregate_arguments_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_aggregate_arguments PROPERTIES
            DEPENDS krc_link_aggregate_arguments)

        add_test(NAME krc_run_aggregate_values
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/aggregate_values_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_aggregate_values PROPERTIES
            DEPENDS krc_link_aggregate_values)

        add_test(NAME krc_run_destructor_scopes
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/destructor_scopes_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_destructor_scopes PROPERTIES
            DEPENDS krc_link_destructor_scopes)
        add_test(NAME krc_run_std_array
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/std_array_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_std_array PROPERTIES
            DEPENDS krc_link_std_array)
        add_test(NAME krc_run_logical_operators
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/logical_operators_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_logical_operators PROPERTIES
            DEPENDS krc_link_logical_operators)
        add_test(NAME krc_run_non_type_templates
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/non_type_templates_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_non_type_templates PROPERTIES
            DEPENDS krc_link_non_type_templates)
        add_test(NAME krc_run_dereference_operator
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/dereference_operator_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_dereference_operator PROPERTIES
            DEPENDS krc_link_dereference_operator)
        add_test(NAME krc_run_paren_initializer
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/paren_initializer_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_paren_initializer PROPERTIES
            DEPENDS krc_link_paren_initializer)
        add_test(NAME krc_run_this_subscript
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/this_subscript_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_this_subscript PROPERTIES
            DEPENDS krc_link_this_subscript)
        add_test(NAME krc_run_injected_class_name
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/injected_class_name_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_injected_class_name PROPERTIES
            DEPENDS krc_link_injected_class_name)
        add_test(NAME krc_run_class_templates
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/class_templates_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_class_templates PROPERTIES
            DEPENDS krc_link_class_templates)
        add_test(NAME krc_run_template_functions
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/template_functions_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_template_functions PROPERTIES
            DEPENDS krc_link_template_functions)
        add_test(NAME krc_run_thread_local_storage
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/thread_local_storage_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_thread_local_storage PROPERTIES
            DEPENDS krc_link_thread_local_storage)

        add_test(NAME krc_run_kr16_thread_local_storage
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_thread_local.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_thread_local_storage PROPERTIES
            DEPENDS krc_kr16_link_thread_local_storage)
        add_test(NAME krc_run_debug_names
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/debug_names_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_debug_names PROPERTIES
            DEPENDS krc_link_debug_names)
        krc_configure_fs_root(div_and_vsprintf)
        add_test(NAME krc_run_div_and_vsprintf
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/div_and_vsprintf_linked.k32
                    -DFS_ROOT=${KRC_FS_ROOT}
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_div_and_vsprintf PROPERTIES
            DEPENDS krc_link_div_and_vsprintf)
        add_test(NAME krc_run_unique_ptr
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/unique_ptr_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_unique_ptr PROPERTIES
            DEPENDS krc_link_unique_ptr)
        add_test(NAME krc_run_copy_and_move_constructors
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/copy_and_move_constructors_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_copy_and_move_constructors PROPERTIES
            DEPENDS krc_link_copy_and_move_constructors)
        add_test(NAME krc_run_rvalue_reference_binding
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/rvalue_reference_binding_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_rvalue_reference_binding PROPERTIES
            DEPENDS krc_link_rvalue_reference_binding)
        add_test(NAME krc_run_arrow_operator
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/arrow_operator_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_arrow_operator PROPERTIES
            DEPENDS krc_link_arrow_operator)
        add_test(NAME krc_run_side_effects_once
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/side_effects_once_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_side_effects_once PROPERTIES
            DEPENDS krc_link_side_effects_once)
        add_test(NAME krc_run_return_call_once
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/return_call_once_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_return_call_once PROPERTIES
            DEPENDS krc_link_return_call_once)
        add_test(NAME krc_run_typedef_tags
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/typedef_tags_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_typedef_tags PROPERTIES
            DEPENDS krc_link_typedef_tags)
    endif()

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
        # The destructor call is the behaviour under test. It used to assert on the
        # name of the value that fed it, `own.dtor`, which was an identity bitcast's
        # result and is folded at the lowering now (ADR 0050).
        PASS_REGULAR_EXPRESSION "@Derived_dtor")

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

    add_test(NAME krc_link_auto_temporary
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/auto_temporary.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/auto_temporary_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_not
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_not.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_not_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_negate
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_negate.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_negate_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_lifetime_reference
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/lifetime_reference.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/lifetime_reference_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_reference_argument
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_reference_argument.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_reference_argument_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_switch
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_switch.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_switch_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_temporary_while
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/temporary_while.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/temporary_while_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_return_braced_two
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/return_braced_two.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/return_braced_two_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_aggregate_return
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/aggregate_return.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/aggregate_return_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_init.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_records
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_records.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_records_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_deduced
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_deduced.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_deduced_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_deduced_records
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_deduced_records.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_deduced_records_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_scalar_init
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_scalar_init.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_scalar_init_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_scalar_value
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_scalar_value.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_scalar_value_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_empty
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_empty.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_empty_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_partial
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_partial.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_partial_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_move_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_move_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_move_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_move_assignment
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_move_assignment.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_move_assignment_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_forward
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_forward.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_forward_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_swap
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_swap.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_swap_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_swap_record
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_swap_record.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_swap_record_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_minmax
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_minmax.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_minmax_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_std_minmax_float
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/std_minmax_float.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/std_minmax_float_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_new_array_default_ctor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/new_array_default_ctor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/new_array_default_ctor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_placement_constructor
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/placement_constructor.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/placement_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_single_initializer_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/single_initializer_call.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/single_initializer_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_qualified_type
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/qualified_type.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/qualified_type_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_thread_local
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/thread_local.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/thread_local_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_thread_local_local
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/thread_local_local.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/thread_local_local_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    # A pointer element in a pack is still rejected: make_tuple(p, 1) infers int* as int and the
# IR verifier refuses the store. Kept as a rejection test so that fixing it breaks this test
# and the fixer is told to turn it into a passing one. It has to compile rather than run
# -fsyntax-only, because the verifier is a later stage and syntax-only exits zero.
# The IP16 profile rejects types it does not have, and these are the two corpus sources that
# hit it. Asserted rather than described: a change that starts accepting either type fails
# here, and so does a source that quietly stops being rejected.
#
# This replaces a note that named three files. `types.c` and `libc-test.c` are not in the
# extracted corpus at all, and `tier1/varargs.c` neither uses `double` nor is rejected --
# it compiles for KR16. The two tests below were derived by compiling every extracted source
# for KR16 and keeping the ones the profile refused.
add_test(NAME krc_cxx_kr16_rejects_double
    COMMAND $<TARGET_FILE:krc> -c --target=kr16
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier1/floats.c"
            -o ${KRC_TEST_OUTPUT_DIR}/kr16_rejects_double.kro)
set_tests_properties(krc_cxx_kr16_rejects_double PROPERTIES
    PASS_REGULAR_EXPRESSION "'double' is not supported by the kr16 profile")

add_test(NAME krc_cxx_kr16_rejects_longlong
    COMMAND $<TARGET_FILE:krc> -c --target=kr16
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier2/cxx_tls.cpp"
            -o ${KRC_TEST_OUTPUT_DIR}/kr16_rejects_longlong.kro)
set_tests_properties(krc_cxx_kr16_rejects_longlong PROPERTIES
    PASS_REGULAR_EXPRESSION "'long long' is not supported by the kr16 profile")

add_test(NAME krc_cxx_make_tuple_pointer_rejected
    COMMAND $<TARGET_FILE:krc> -c --target=kr32
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/make_tuple_pointer.cpp"
            -o ${KRC_TEST_OUTPUT_DIR}/make_tuple_pointer_rejected.kro)
set_tests_properties(krc_cxx_make_tuple_pointer_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "store type does not match its pointer")

add_test(NAME krc_cxx_value_parameter_deduced
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/value_parameter_deduced.cpp")
    # The regex is the assertion: WILL_FAIL would invert its result as well.
    set_tests_properties(krc_cxx_value_parameter_deduced PROPERTIES
        PASS_REGULAR_EXPRESSION "cannot deduce the value template parameter")
    add_test(NAME krc_cxx_static_assert_fail
        COMMAND $<TARGET_FILE:krc> -fsyntax-only
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/static_assert_fail.cpp")
    set_tests_properties(krc_cxx_static_assert_fail PROPERTIES WILL_FAIL TRUE)

    add_test(NAME krc_cxx_deleted_function
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/deleted_function_call.cpp
                "-DEXPECT=use of deleted function"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_private_member_access
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/private_member_access.cpp
                "-DEXPECT='S::value' is private"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_protected_member_access
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/protected_member_access.cpp
                "-DEXPECT='Base::shared' is protected"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_out_of_line_private_access
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_private_access.cpp
                "-DEXPECT='Secret::value' is private"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_out_of_line_protected_access
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/out_of_line_protected_access.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/out_of_line_protected_access_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_friend_class_access
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/friend_class_access.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/friend_class_access_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_in_class_private_access
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/in_class_private_access.cpp
                "-DEXPECT='Secret::value' is private"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_kr16_long_long_rejected
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DTARGET=kr16
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_long_long.c
                "-DEXPECT='long long' is not supported by the kr16 profile"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_kr16_float_rejected
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DTARGET=kr16
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_float.c
                "-DEXPECT='double' is not supported by the kr16 profile"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_longlong_conditional
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/longlong_conditional.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/longlong_conditional_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_const_member_read
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_member_read.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/const_member_read_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_const_mutable_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_mutable_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/const_mutable_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_const_method_ref_denied
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_method_ref_denied.cpp
                "-DEXPECT=call to non-const member function"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_heap_above_code
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/heap_above_code.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/heap_above_code_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_struct_return_assignment
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct_return_assignment.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/struct_return_assignment.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_explicit_constructor_copy_list
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_constructor_copy_list.cpp
                "-DEXPECT=is explicit; use direct initialisation"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_explicit_constructor_direct_list
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_constructor_direct_list.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/explicit_direct_list_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_explicit_constructor_copy_init
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_constructor_copy_init.cpp
                "-DEXPECT=is explicit; use direct initialisation"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_explicit_constructor_direct
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_constructor_direct.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/explicit_constructor_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_deleted_constructor_copy_variable
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/deleted_constructor_copy_variable.cpp
                "-DEXPECT=call to deleted constructor"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_deleted_constructor_copy_init
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/deleted_constructor_copy_init.cpp
                "-DEXPECT=call to deleted constructor"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_deleted_constructor_call
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/deleted_constructor_call.cpp
                "-DEXPECT=call to deleted constructor"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_deleted_default_constructor
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/deleted_default_constructor.cpp
                "-DEXPECT=call to deleted constructor"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_constructor_definition
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/constructor_definition.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/constructor_definition_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_abstract_instantiation
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/abstract_instantiation.cpp
                "-DEXPECT=cannot instantiate abstract class"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_logical_not_pointer
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/logical_not_pointer.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/logical_not_pointer_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_inherit_member_arith
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/inherit_member_arith.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/inherit_member_arith_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_link_abstract_derived
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/abstract_derived_instantiation.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/abstract_derived_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_final_class_derivation
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/final_class_derivation.cpp
                "-DEXPECT=cannot derive from final class"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_final_member_override
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/final_member_override.cpp
                "-DEXPECT=overrides a final member"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_override_missing_base
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/override_missing_base.cpp
                "-DEXPECT=is marked override but overrides no base member"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_override_member
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/override_member.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/override_member_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_const_method_denied
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_method_call_denied.cpp
                "-DEXPECT=call to non-const member function"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_typeid_without_typeinfo
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/typeid_without_typeinfo.cpp
                "-DEXPECT=typeid requires <typeinfo>"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_throw_scalar
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/throw_scalar.cpp
                "-DEXPECT=throw requires a class type"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_link_const_method_call
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_method_call.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/const_method_call_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

    add_test(NAME krc_cxx_const_member_assign
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_member_assign.cpp
                "-DEXPECT=assignment to a member of a const object"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

    add_test(NAME krc_cxx_const_pointer_member_assign
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/const_pointer_member_assign.cpp
                "-DEXPECT=assignment to a member of a const object"
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDiagnostic.cmake)

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

    add_test(NAME krc_optimization_levels
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier0/functions.c
                -DOUT_DEFAULT=${KRC_TEST_OUTPUT_DIR}/level_default.s
                -DOUT_O0=${KRC_TEST_OUTPUT_DIR}/level_o0.s
                -DOUT_O0_AGAIN=${KRC_TEST_OUTPUT_DIR}/level_o0b.s
                -DOUT_O1=${KRC_TEST_OUTPUT_DIR}/level_o1.s
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckLevelIdentity.cmake)

    add_test(NAME krc_determinism_object
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/struct.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism1.kro
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism2.kro
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckObjectDeterminism.cmake)

    add_test(NAME krc_determinism_longlong
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/longlong_arith.c
                -DOUT1=${KRC_TEST_OUTPUT_DIR}/determinism_longlong1.kro
                -DOUT2=${KRC_TEST_OUTPUT_DIR}/determinism_longlong2.kro
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
    set_tests_properties(krld_version PROPERTIES PASS_REGULAR_EXPRESSION "krld 0\\.2\\.0")
endif()

# A record's layout must be one number everywhere it is measured: sizeof of a type
# and of an expression of it, the stride of new T[n], and the class a by-value
# aggregate is passed in. These three fixtures pin the same source on both targets,
# which is what catches a layout rule that agrees with itself and nothing else.
foreach(record_fixture
        small_record_size
        small_aggregate_argument
        array_new_subword_record)
    add_test(NAME krc_link_${record_fixture}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/${record_fixture}.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/${record_fixture}_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_${record_fixture}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/${record_fixture}_linked.k32
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_${record_fixture} PROPERTIES
            DEPENDS krc_link_${record_fixture})
    endif()

    add_test(NAME krc_link_kr16_${record_fixture}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/${record_fixture}.cpp
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_${record_fixture}.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_${record_fixture}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_${record_fixture}.kex
                    -DEXPECT_EXIT=42
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_${record_fixture} PROPERTIES
            DEPENDS krc_link_kr16_${record_fixture})
    endif()
endforeach()

# <tuple>: the named API -- tuple_size, tuple_head, tuple_tail, and the members.
add_test(NAME krc_link_tuple_header
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/tuple_header.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tuple_header_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_tuple_header
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tuple_header_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_tuple_header PROPERTIES
        DEPENDS krc_link_tuple_header)
endif()

add_test(NAME krc_link_kr16_tuple_header
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/tuple_header.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tuple_header.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_tuple_header
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tuple_header.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_tuple_header PROPERTIES
        DEPENDS krc_link_kr16_tuple_header)
endif()

# <tuple>: the element constructor. Its failure mode is silent -- the head is
# initialised and the tail is not -- so it needs to run, not only link.
add_test(NAME krc_link_tuple_element_constructor
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/tuple_element_constructor.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tuple_element_constructor_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_tuple_element_constructor
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tuple_element_constructor_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_tuple_element_constructor PROPERTIES
        DEPENDS krc_link_tuple_element_constructor)
endif()

add_test(NAME krc_link_kr16_tuple_element_constructor
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/tuple_element_constructor.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tuple_element_constructor.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_tuple_element_constructor
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tuple_element_constructor.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_tuple_element_constructor PROPERTIES
        DEPENDS krc_link_kr16_tuple_element_constructor)
endif()

# A call that returns a reference, passed as an argument to another call. The outer
# call used to be dropped entirely, leaving a null operand the IR verifier rejects.
add_test(NAME krc_link_reference_return_argument
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_return_argument.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/reference_return_argument_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_reference_return_argument
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/reference_return_argument_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_reference_return_argument PROPERTIES
        DEPENDS krc_link_reference_return_argument)
endif()

add_test(NAME krc_link_kr16_reference_return_argument
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/reference_return_argument.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_reference_return_argument.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_reference_return_argument
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_reference_return_argument.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_reference_return_argument PROPERTIES
        DEPENDS krc_link_kr16_reference_return_argument)
endif()

# Tier 0 -- smoke: hello, arithmetic, functions. The tier definition in
# plan.md 5.2 says these build **and run** on both targets, so each is linked and
# executed rather than only syntax-checked. hello's output is compared as well as
# its exit status, because a smoke test that checked only the status would pass on
# a garbled string.
foreach(tier0_source IN ITEMS hello arithmetic functions)
    set(tier0_expect_output "")
    if(tier0_source STREQUAL "hello")
        set(tier0_expect_output "Hello, KR32!")
    endif()

    add_test(NAME krc_link_tier0_${tier0_source}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier0/${tier0_source}.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier0_${tier0_source}_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier0_${tier0_source}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier0_${tier0_source}_linked.k32
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier0_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier0_${tier0_source} PROPERTIES
            DEPENDS krc_link_tier0_${tier0_source})
    endif()

    add_test(NAME krc_link_kr16_tier0_${tier0_source}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier0/${tier0_source}.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tier0_${tier0_source}.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_tier0_${tier0_source}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tier0_${tier0_source}.kex
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier0_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_tier0_${tier0_source} PROPERTIES
            DEPENDS krc_link_kr16_tier0_${tier0_source})
    endif()
endforeach()

# The same three smoke programs built at -O1. ADR 0049 requires the suite to run
# at both levels rather than only at the default, and these are the checks that
# execute: a text pass that removed a live branch fails here and nowhere else,
# because every other test compiles at -O0. The runtime is linked as the staged
# -O0 object in both halves, so the only difference from the tests above is the
# level the program itself is compiled at.
foreach(tier0_source IN ITEMS hello arithmetic functions)
    set(tier0_expect_output "")
    if(tier0_source STREQUAL "hello")
        set(tier0_expect_output "Hello, KR32!")
    endif()

    add_test(NAME krc_link_tier0_${tier0_source}_o1
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DLEVEL=-O1
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier0/${tier0_source}.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier0_${tier0_source}_o1_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier0_${tier0_source}_o1
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier0_${tier0_source}_o1_linked.k32
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier0_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier0_${tier0_source}_o1 PROPERTIES
            DEPENDS krc_link_tier0_${tier0_source}_o1)
    endif()

    add_test(NAME krc_link_kr16_tier0_${tier0_source}_o1
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DLEVEL=-O1
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier0/${tier0_source}.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tier0_${tier0_source}_o1.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_tier0_${tier0_source}_o1
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tier0_${tier0_source}_o1.kex
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier0_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_tier0_${tier0_source}_o1 PROPERTIES
            DEPENDS krc_link_kr16_tier0_${tier0_source}_o1)
    endif()
endforeach()

# Tier 1 -- the C core: structs, pointers, arrays, control flow, preprocessor,
# varargs and printf (plan.md 5.2). Each program covers one feature area and is
# linked and executed on both targets. printf compares its text as well as its exit
# status, because a conversion that prints nothing still exits 0.
foreach(tier1_source IN ITEMS structs pointers arrays control preprocessor varargs printf strings)
    set(tier1_expect_output "")
    if(tier1_source STREQUAL "printf")
        set(tier1_expect_output "signed 42.*negative -17.*mixed 1 b c 4")
    endif()
    if(tier1_source STREQUAL "strings")
        # The string and memory routines, executed rather than only linked. This file
        # was recorded as unable to link because libcrt supposedly lacked memmove, and
        # the runtime has provided it all along.
        set(tier1_expect_output "memmove: ababcd")
    endif()

    add_test(NAME krc_link_tier1_${tier1_source}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier1/${tier1_source}.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier1_${tier1_source}_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier1_${tier1_source}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier1_${tier1_source}_linked.k32
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier1_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier1_${tier1_source} PROPERTIES
            DEPENDS krc_link_tier1_${tier1_source})
    endif()

    add_test(NAME krc_link_kr16_tier1_${tier1_source}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier1/${tier1_source}.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tier1_${tier1_source}.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_tier1_${tier1_source}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tier1_${tier1_source}.kex
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier1_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_tier1_${tier1_source} PROPERTIES
            DEPENDS krc_link_kr16_tier1_${tier1_source})
    endif()
endforeach()

# Tier 1 executed at -O1 as well. ADR 0049 requires the suite at both levels, and
# the twelve tier-0 twins proved their worth by catching a wrong-value allocator
# bug that no other test could see; eight more feature areas — structs, pointers,
# arrays, control flow, the preprocessor, varargs, printf and the string routines
# — are the ones a code-generation change is most likely to break silently. The
# runtime is the staged -O0 object in both halves, so the only difference from the
# tests above is the level the program itself is compiled at.
foreach(tier1_source IN ITEMS structs pointers arrays control preprocessor varargs printf strings)
    set(tier1_expect_output "")
    if(tier1_source STREQUAL "printf")
        set(tier1_expect_output "signed 42.*negative -17.*mixed 1 b c 4")
    endif()
    if(tier1_source STREQUAL "strings")
        set(tier1_expect_output "memmove: ababcd")
    endif()

    add_test(NAME krc_link_tier1_${tier1_source}_o1
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DLEVEL=-O1
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier1/${tier1_source}.c
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier1_${tier1_source}_o1_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier1_${tier1_source}_o1
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier1_${tier1_source}_o1_linked.k32
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier1_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier1_${tier1_source}_o1 PROPERTIES
            DEPENDS krc_link_tier1_${tier1_source}_o1)
    endif()

    add_test(NAME krc_link_kr16_tier1_${tier1_source}_o1
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DLEVEL=-O1
                -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
                -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
                -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
                -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier1/${tier1_source}.c
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_tier1_${tier1_source}_o1.kex
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
    if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
        add_test(NAME krc_run_kr16_tier1_${tier1_source}_o1
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR16_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_tier1_${tier1_source}_o1.kex
                    -DEXPECT_EXIT=0
                    -DEXPECT_OUTPUT=${tier1_expect_output}
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
        set_tests_properties(krc_run_kr16_tier1_${tier1_source}_o1 PROPERTIES
            DEPENDS krc_link_kr16_tier1_${tier1_source}_o1)
    endif()
endforeach()

# Tier 2 -- the C++ core: classes, single inheritance, virtual dispatch. KR32-only,
# because the plan's tier table gives Tier 2 to KR32 for v1.0 and these fixtures need
# the C++ ABI that KR16 defers.
foreach(tier2_source IN ITEMS classes inheritance virtual new_delete templates lambdas constexpr2)
    add_test(NAME krc_link_tier2_${tier2_source}
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier2/${tier2_source}.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier2_${tier2_source}_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier2_${tier2_source}
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier2_${tier2_source}_linked.k32
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier2_${tier2_source} PROPERTIES
            DEPENDS krc_link_tier2_${tier2_source})
    endif()
endforeach()

# The same tier-2 programs at -O1. ADR 0049 requires the suite at both levels, and
# these were the last ones outside it: they exercise the vtable, the EH region table
# and the C++ temporary paths, which is where a pass that moved a value across a
# landing pad or a call would show. Running them is the measurement that decides
# whether -O1 is trustworthy for C++; it is not an assumption.
foreach(tier2_source IN ITEMS classes inheritance virtual new_delete templates lambdas constexpr2)
    add_test(NAME krc_link_tier2_${tier2_source}_o1
        COMMAND ${CMAKE_COMMAND}
                -DKRC=$<TARGET_FILE:krc>
                -DKRLD=$<TARGET_FILE:krld>
                -DLEVEL=-O1
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier2/${tier2_source}.cpp
                -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
                -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
                -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/tier2_${tier2_source}_o1_linked.k32
                -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
    if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
        add_test(NAME krc_run_tier2_${tier2_source}_o1
            COMMAND ${CMAKE_COMMAND}
                    -DVM=${KRC_KR32_VM}
                    -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier2_${tier2_source}_o1_linked.k32
                    -DEXPECT_EXIT=0
                    -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
        set_tests_properties(krc_run_tier2_${tier2_source}_o1 PROPERTIES
            DEPENDS krc_link_tier2_${tier2_source}_o1)
    endif()
endforeach()

# A member call on a temporary. This was a diagnostic fixture until the temporary path
# stopped requiring a declared zero-argument constructor; it is a behaviour fixture now,
# because the expectation it encoded *was* the defect.
add_test(NAME krc_link_member_call_on_temporary
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/member_call_on_temporary.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/member_call_on_temporary_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_member_call_on_temporary
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/member_call_on_temporary_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_member_call_on_temporary PROPERTIES
        DEPENDS krc_link_member_call_on_temporary)
endif()

# C++ debug parity. The other debug fixtures link and stop; this one reads the records
# back and requires the class's methods and type to be named, which is what "parity for
# C and C++" means in the M8 checklist.
add_test(NAME krc_link_debug_cxx_class
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_cxx_class.cpp
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
add_test(NAME krc_debug_cxx_class
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_linked.k32)
set_tests_properties(krc_debug_cxx_class PROPERTIES
    PASS_REGULAR_EXPRESSION "function Counter::add"
    DEPENDS krc_link_debug_cxx_class)

# A class's field layout must survive into the image, not only its method names. The
# member record is what a debugger reads to show `value_` inside a `Counter`.
add_test(NAME krc_debug_cxx_members
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_linked.k32)
set_tests_properties(krc_debug_cxx_members PROPERTIES
    PASS_REGULAR_EXPRESSION "member value_ offset=0 type=i32"
    DEPENDS krc_link_debug_cxx_class)

# A local's record: name, frame offset, type and size. This is what a debugger reads to
# show `counter` while stopped in main.
add_test(NAME krc_debug_cxx_variables
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_linked.k32)
set_tests_properties(krc_debug_cxx_variables PROPERTIES
    PASS_REGULAR_EXPRESSION "variable main\\.counter offset=8 type=Counter"
    DEPENDS krc_link_debug_cxx_class)

# A line record maps an address to a source line. Line records are the largest part of
# an image and were the only kind the dump never printed, so nothing asserted them.
add_test(NAME krc_debug_cxx_lines
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_linked.k32)
set_tests_properties(krc_debug_cxx_lines PROPERTIES
    PASS_REGULAR_EXPRESSION "source=0 line=15"
    DEPENDS krc_link_debug_cxx_class)

# FileOpen.cpp opens the host's native file dialog, so it links and cannot be run
# automatically: the dialog blocks waiting for input, which was measured rather than
# assumed. It reached the syntax-only list because it was recorded as failing on an
# implicit declaration of sys_open_dialog; that function is declared in stdio.h and
# implemented in fileio.s, and the file links.
add_test(NAME krc_link_FileOpen
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/corpus/extracted/tier2/FileOpen.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/FileOpen_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)

# The C half of "the debugger works for C and C++". krc_link_debug_struct only linked,
# so nothing had ever asserted that a C struct's fields and a C local reach KDBG.
add_test(NAME krc_debug_c_struct
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_struct_linked.k32)
set_tests_properties(krc_debug_c_struct PROPERTIES
    PASS_REGULAR_EXPRESSION "member b offset=4 type=i32"
    DEPENDS krc_link_debug_struct)
add_test(NAME krc_debug_c_variable
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_struct_linked.k32)
set_tests_properties(krc_debug_c_variable PROPERTIES
    PASS_REGULAR_EXPRESSION "variable main\\.pair offset=8 type=Pair"
    DEPENDS krc_link_debug_struct)

# Everything above reads the debug data through `--dump-symbols`; none of it
# drives the adapter a debugger actually talks to, so a DAP server that answered
# no breakpoints would have passed the suite. This session runs the real thing:
# break on `return total;`, and assert the breakpoint verifies, execution stops
# there, the frame is `main` at that line, and `total` reads the 42 that
# `compute(41)` returned. The image is built at the default level; the `-O1`
# twin belongs to docs/plan-o1-debugger.md W3.
add_test(NAME krc_link_debug_dap
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_dap.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_dap_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_debug_dap_session
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/debug_dap_linked.k32
                -DBREAK_LINE=17
                -DEXPECT_FUNCTION=main
                -DEXPECT_SOURCE=tests/fixtures/c/debug_dap.c
                -DEXPECT_VARIABLE=total
                -DEXPECT_VALUE=0x0000002A
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDapSession.cmake)
    set_tests_properties(krc_debug_dap_session PROPERTIES
        DEPENDS krc_link_debug_dap)
endif()

# docs/symbols.md states that a `Line` record exists only while an instruction is at
# the line, and that a line whose code a pass removed is absent rather than mapped
# onto a neighbour. At -O1 that is observable: E5 eliminates the single-`br` block a
# `continue;` or `break;` lowers to, so both jump lines lose their address while the
# statements around them keep theirs. The -O0 twin pins that the level, not the
# fixture, is what removes them.
add_test(NAME krc_link_debug_folded_lines_o0
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_folded_lines.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_folded_lines_o0_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
add_test(NAME krc_link_debug_folded_lines_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DLEVEL=-O1
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_folded_lines.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_folded_lines_o1_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
add_test(NAME krc_debug_o0_jump_only_lines
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DIMAGE=${KRC_TEST_OUTPUT_DIR}/debug_folded_lines_o0_linked.k32
            -DSOURCE=debug_folded_lines.c
            -DPRESENT_LINES=17,19,25,27
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckLineRecords.cmake)
set_tests_properties(krc_debug_o0_jump_only_lines PROPERTIES
    DEPENDS krc_link_debug_folded_lines_o0)
add_test(NAME krc_debug_o1_jump_only_lines
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DIMAGE=${KRC_TEST_OUTPUT_DIR}/debug_folded_lines_o1_linked.k32
            -DSOURCE=debug_folded_lines.c
            -DABSENT_LINES=17,25
            -DPRESENT_LINES=19,27
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckLineRecords.cmake)
set_tests_properties(krc_debug_o1_jump_only_lines PROPERTIES
    DEPENDS krc_link_debug_folded_lines_o1)

# The same assertions on images compiled at -O1 (docs/plan-o1-debugger.md W3a).
# Optimisation must not cost the debugger its records: function, member, variable, type
# and line data have to survive it. Each twin links its own image at -O1 rather than
# reusing the default-level one, because the optimised image is the subject.
#
# The names twin is here and the paths twin is not: `krld --source-root` rewrites source
# paths after code generation, so a level cannot change that answer, and a twin would
# assert nothing new. The name twin can fail, because level-specific code can drop a
# function record.
add_test(NAME krc_link_debug_cxx_class_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DLEVEL=-O1
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_cxx_class.cpp
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_o1_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
add_test(NAME krc_debug_cxx_class_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_o1_linked.k32)
set_tests_properties(krc_debug_cxx_class_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "function Counter::add"
    DEPENDS krc_link_debug_cxx_class_o1)
add_test(NAME krc_debug_cxx_members_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_o1_linked.k32)
set_tests_properties(krc_debug_cxx_members_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "member value_ offset=0 type=i32"
    DEPENDS krc_link_debug_cxx_class_o1)
add_test(NAME krc_debug_cxx_variables_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_o1_linked.k32)
set_tests_properties(krc_debug_cxx_variables_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "variable main\\.counter offset=8 type=Counter"
    DEPENDS krc_link_debug_cxx_class_o1)
add_test(NAME krc_debug_cxx_lines_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_cxx_class_o1_linked.k32)
set_tests_properties(krc_debug_cxx_lines_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "source=0 line=15"
    DEPENDS krc_link_debug_cxx_class_o1)
add_test(NAME krc_link_debug_struct_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DLEVEL=-O1
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_struct.c
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_struct_o1_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
add_test(NAME krc_debug_c_struct_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_struct_o1_linked.k32)
set_tests_properties(krc_debug_c_struct_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "member b offset=4 type=i32"
    DEPENDS krc_link_debug_struct_o1)
add_test(NAME krc_debug_c_variable_o1
    COMMAND $<TARGET_FILE:krc> --dump-symbols
            ${KRC_TEST_OUTPUT_DIR}/debug_struct_o1_linked.k32)
set_tests_properties(krc_debug_c_variable_o1 PROPERTIES
    PASS_REGULAR_EXPRESSION "variable main\\.pair offset=8 type=Pair"
    DEPENDS krc_link_debug_struct_o1)
add_test(NAME krc_debug_names_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DLEVEL=-O1
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_names.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DIO=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/fileio.s
            -DEH=${CMAKE_SOURCE_DIR}/targets/kr32/runtime/eh.s
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_names_o1.k32
            "-DEXPECT=function Widget::Widget;function Widget::~Widget;function Widget::size;function Widget::doubled;function Counter::bump;function Counter::value;function helper"
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckDebugNames.cmake)

# The two invariants a debugger's answer depends on (W3b), over a corpus image at -O1:
# every line address inside some function, and every variable inside the frame its
# function claims. The image is the tier-1 printf program linked with the staged
# runtime, which the -O1 execution test already builds -- 10,081 lines and 693
# variables, so a defect has somewhere to show up.
add_test(NAME krc_debug_invariants_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DIMAGE=${KRC_TEST_OUTPUT_DIR}/tier1_printf_o1_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckDebugInvariants.cmake)
set_tests_properties(krc_debug_invariants_o1 PROPERTIES
    DEPENDS krc_link_tier1_printf_o1)

# The session from krc_debug_dap_session, on an -O1 image. This is the question the
# plan exists to answer -- whether an optimised image is debuggable at all, not merely
# loadable -- and it is the only test that drives a breakpoint into one. W3's record
# assertions cannot stand in for it: they check what the image says about itself, and
# this checks what the debugger gets back.
add_test(NAME krc_link_debug_dap_o1
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DLEVEL=-O1
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/debug_dap.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/debug_dap_o1_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_debug_dap_session_o1
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/debug_dap_o1_linked.k32
                -DBREAK_LINE=17
                -DEXPECT_FUNCTION=main
                -DEXPECT_SOURCE=tests/fixtures/c/debug_dap.c
                -DEXPECT_VARIABLE=total
                -DEXPECT_VALUE=0x0000002A
                -P ${CMAKE_SOURCE_DIR}/cmake/CheckDapSession.cmake)
    set_tests_properties(krc_debug_dap_session_o1 PROPERTIES
        DEPENDS krc_link_debug_dap_o1)
endif()

# Copy construction of a class that declares a destructor and an explicit constructor.
# This shape selected the wrong constructor (arity alone matched `explicit Counter(int)`),
# and once the copy path was taken it skipped destructor registration, so two objects ran
# one destructor. Both are pinned here.
add_test(NAME krc_link_copy_with_destructor
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_with_destructor.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/copy_with_destructor_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_copy_with_destructor
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/copy_with_destructor_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_copy_with_destructor PROPERTIES
        DEPENDS krc_link_copy_with_destructor)
endif()

add_test(NAME krc_link_kr16_copy_with_destructor
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/copy_with_destructor.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_copy_with_destructor.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_copy_with_destructor
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_copy_with_destructor.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_copy_with_destructor PROPERTIES
        DEPENDS krc_link_kr16_copy_with_destructor)
endif()

# An empty tuple as a local. Its record is the empty primary, which declares no
# constructor of its own; the call used to bind to the partial specialisation's pattern
# constructor, which is only ever declared, so the image failed with
# `undefined symbol 'tuple_ctor_0_value'`.
add_test(NAME krc_link_empty_tuple_local
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/empty_tuple_local.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/empty_tuple_local_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_empty_tuple_local
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/empty_tuple_local_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_empty_tuple_local PROPERTIES
        DEPENDS krc_link_empty_tuple_local)
endif()

add_test(NAME krc_link_kr16_empty_tuple_local
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/empty_tuple_local.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_empty_tuple_local.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_empty_tuple_local
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_empty_tuple_local.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_empty_tuple_local PROPERTIES
        DEPENDS krc_link_kr16_empty_tuple_local)
endif()

# A character literal has the type char in C++ and a character constant has the type int in
# C. Both are asserted at compile time and checked at run time, because the typing change
# that made `make_tuple(41, 'a')` deduce `tuple<int, char>` is otherwise exercised only
# indirectly by the tier2 fixture. The C case asserts against sizeof(int) rather than a
# literal: int is two bytes on KR16 and four on KR32.
# Per-element parameter widths. Every expanded parameter of a pack function template used
# to take the head element's type, so a value wider than the head was narrowed silently.
# These cases have to run: a compile-only test cannot see a narrowed value.
# A value template parameter deduced from an argument. These have to run: the previous
# behaviour was a compile error, so only an executed image proves the binding has the right
# value rather than merely the right shape.
# The four shapes the value-parameter deduction has to handle: by value, through a pointer,
# across two parameters, and alongside a pack. Run, not merely compiled -- a wrong binding is
# a wrong number, which only an executed image can see.
add_test(NAME krc_link_nontype_deduction_shapes
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nontype_deduction_shapes.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nontype_deduction_shapes_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_nontype_deduction_shapes
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/nontype_deduction_shapes_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_nontype_deduction_shapes PROPERTIES
        DEPENDS krc_link_nontype_deduction_shapes)
endif()

add_test(NAME krc_link_kr16_nontype_deduction_shapes
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nontype_deduction_shapes.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_nontype_deduction_shapes.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_nontype_deduction_shapes
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_nontype_deduction_shapes.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_nontype_deduction_shapes PROPERTIES
        DEPENDS krc_link_kr16_nontype_deduction_shapes)
endif()

add_test(NAME krc_link_nontype_deduction
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nontype_deduction.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nontype_deduction_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_nontype_deduction
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/nontype_deduction_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_nontype_deduction PROPERTIES
        DEPENDS krc_link_nontype_deduction)
endif()

add_test(NAME krc_link_kr16_nontype_deduction
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nontype_deduction.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_nontype_deduction.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_nontype_deduction
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_nontype_deduction.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_nontype_deduction PROPERTIES
        DEPENDS krc_link_kr16_nontype_deduction)
endif()

add_test(NAME krc_link_make_tuple_widths
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/make_tuple_widths.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/make_tuple_widths_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_make_tuple_widths
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/make_tuple_widths_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_make_tuple_widths PROPERTIES
        DEPENDS krc_link_make_tuple_widths)
endif()

add_test(NAME krc_link_kr16_make_tuple_widths
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/make_tuple_widths.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_make_tuple_widths.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_make_tuple_widths
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_make_tuple_widths.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_make_tuple_widths PROPERTIES
        DEPENDS krc_link_kr16_make_tuple_widths)
endif()

add_test(NAME krc_link_char_literal_type
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/char_literal_type.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/char_literal_type_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_char_literal_type
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/char_literal_type_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_char_literal_type PROPERTIES
        DEPENDS krc_link_char_literal_type)
endif()

add_test(NAME krc_link_kr16_char_literal_type
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/char_literal_type.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_char_literal_type.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_char_literal_type
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_char_literal_type.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_char_literal_type PROPERTIES
        DEPENDS krc_link_kr16_char_literal_type)
endif()

add_test(NAME krc_link_char_literal_type_c
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/char_literal_type_c.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/char_literal_type_c_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_char_literal_type_c
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/char_literal_type_c_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_char_literal_type_c PROPERTIES
        DEPENDS krc_link_char_literal_type_c)
endif()

add_test(NAME krc_link_kr16_char_literal_type_c
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/char_literal_type_c.c
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_char_literal_type_c.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_char_literal_type_c
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_char_literal_type_c.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_char_literal_type_c PROPERTIES
        DEPENDS krc_link_kr16_char_literal_type_c)
endif()

# `make_tuple` returns a tuple by value from a function template whose signature carries
# the class template's pack. It works because the parser no longer emits a space before
# the ellipsis in an expression template-id, the pack's element types are bound, and a
# constructor call records the type it resolved to for code generation.
add_test(NAME krc_link_make_tuple
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/make_tuple.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/make_tuple_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_make_tuple
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/make_tuple_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_make_tuple PROPERTIES
        DEPENDS krc_link_make_tuple)
endif()

add_test(NAME krc_link_kr16_make_tuple
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/make_tuple.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_make_tuple.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_make_tuple
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_make_tuple.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_make_tuple PROPERTIES
        DEPENDS krc_link_kr16_make_tuple)
endif()
# The target macro names the target being compiled for. It was once recorded here as
# missing because a probe used the uppercase spelling; the descriptor defines "__kr32__"
# and "__kr16__". This fixture fails to compile if neither is defined, so a descriptor
# that loses its macro cannot pass silently.
add_test(NAME krc_link_target_macro
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/target_macro.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/target_macro_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_target_macro
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/target_macro_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_target_macro PROPERTIES
        DEPENDS krc_link_target_macro)
endif()

add_test(NAME krc_link_kr16_target_macro
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/target_macro.c
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_target_macro.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_target_macro
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_target_macro.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_target_macro PROPERTIES
        DEPENDS krc_link_kr16_target_macro)
endif()

# A nested function-like macro invocation expands its argument before substitution, so
# DOUBLE(DOUBLE(10)) is 40. Stringize and paste keep the raw argument, which is the part
# of substitution that must not pre-expand.
add_test(NAME krc_link_nested_macro
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_macro.c
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/nested_macro_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_nested_macro
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/nested_macro_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_nested_macro PROPERTIES
        DEPENDS krc_link_nested_macro)
endif()

add_test(NAME krc_link_kr16_nested_macro
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/nested_macro.c
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_nested_macro.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_nested_macro
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_nested_macro.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_nested_macro PROPERTIES
        DEPENDS krc_link_kr16_nested_macro)
endif()

# An explicit specialization is instantiated for the type it specialises, and a type
# without one still gets the primary. Three guards each rejected specializations before
# this: registration, the arity check, and the binding loop that crashed the compiler.
add_test(NAME krc_link_explicit_specialization
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_specialization.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/explicit_specialization_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_explicit_specialization
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/explicit_specialization_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_explicit_specialization PROPERTIES
        DEPENDS krc_link_explicit_specialization)
endif()

add_test(NAME krc_link_kr16_explicit_specialization
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/explicit_specialization.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_explicit_specialization.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_explicit_specialization
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_explicit_specialization.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_explicit_specialization PROPERTIES
        DEPENDS krc_link_kr16_explicit_specialization)
endif()

# A member call on a temporary of a variadic class template. The constructor style names
# its class with an identifier carrying template-id labels; ignoring them returned the
# pattern's type, so the instance's methods were never collected.
add_test(NAME krc_link_variadic_temporary
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/variadic_temporary.cpp
            -DLIBC=${KRC_STAGE_RUNTIME}/libkrcrt.kro
            -DRUNTIME=${KRC_STAGE_RUNTIME}/crt0.s
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/variadic_temporary_linked.k32
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkRuntime.cmake)
if(KRC_KR32_VM AND EXISTS "${KRC_KR32_VM}")
    add_test(NAME krc_run_variadic_temporary
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR32_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/variadic_temporary_linked.k32
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr32Image.cmake)
    set_tests_properties(krc_run_variadic_temporary PROPERTIES
        DEPENDS krc_link_variadic_temporary)
endif()

add_test(NAME krc_link_kr16_variadic_temporary
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/variadic_temporary.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_temporary.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_variadic_temporary
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_variadic_temporary.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_variadic_temporary PROPERTIES
        DEPENDS krc_link_kr16_variadic_temporary)
endif()

# The determinism tests compile the same input twice and compare the bytes, so they
# cannot run alongside another test that is also invoking the compiler.
set_tests_properties(
    krc_determinism_object
    krc_determinism_longlong
    krc_determinism_kr16_image
    krc_determinism_kr16_object
    krc_determinism_kr16_link
    PROPERTIES RUN_SERIAL TRUE)

# Aggregate returns on KR16 (ADR 0048). The destination travels as a hidden stack argument
# because no general-purpose register is free there.
add_test(NAME krc_link_kr16_aggregate_return
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_aggregate_return.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_aggregate_return.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_aggregate_return
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_aggregate_return.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_aggregate_return PROPERTIES
        DEPENDS krc_link_kr16_aggregate_return)
endif()

# make_unique on KR16, which needed aggregate returns to work.
add_test(NAME krc_link_kr16_make_unique
    COMMAND ${CMAKE_COMMAND}
            -DKRC=$<TARGET_FILE:krc>
            -DKRLD=$<TARGET_FILE:krld>
            -DCRT0=${KRC_STAGE_RUNTIME_KR16}/crt0.s
            -DHOST=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_host.kro
            -DFILEIO=${KRC_STAGE_RUNTIME_KR16}/libkrcrt_fileio.kro
            -DLIBCRT=${KRC_STAGE_RUNTIME_KR16}/libkrcrt.kro
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/c/kr16_make_unique.cpp
            -DOUTPUT=${KRC_TEST_OUTPUT_DIR}/kr16_make_unique.kex
            -P ${CMAKE_SOURCE_DIR}/cmake/LinkKr16Host.cmake)
if(KRC_KR16_VM AND EXISTS "${KRC_KR16_VM}")
    add_test(NAME krc_run_kr16_make_unique
        COMMAND ${CMAKE_COMMAND}
                -DVM=${KRC_KR16_VM}
                -DIMAGE=${KRC_TEST_OUTPUT_DIR}/kr16_make_unique.kex
                -DEXPECT_EXIT=42
                -P ${CMAKE_SOURCE_DIR}/cmake/RunKr16Image.cmake)
    set_tests_properties(krc_run_kr16_make_unique PROPERTIES
        DEPENDS krc_link_kr16_make_unique)
endif()
