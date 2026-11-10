function(krc_add_cli_determinism_test target)
    add_test(
        NAME ${target}_cli_determinism
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:${target}>
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckCliDeterminism.cmake"
    )
endfunction()
