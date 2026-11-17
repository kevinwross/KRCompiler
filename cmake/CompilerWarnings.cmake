function(krc_apply_warnings target)
    if(MSVC)
        # /MP compiles the target's source files in parallel. The Visual Studio
        # generator otherwise compiles them one at a time regardless of how many cores
        # the build is given, so splitting a large translation unit bought nothing here.
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /MP)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
