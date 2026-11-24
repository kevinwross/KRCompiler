function(krc_apply_warnings target)
    if(MSVC)
        # /MP compiles the target's source files in parallel. The Visual Studio
        # generator otherwise compiles them one at a time regardless of how many cores
        # the build is given, so splitting a large translation unit bought nothing here.
        #
        # /Brepro removes the link timestamp, which is otherwise the one thing that
        # makes two builds of the same commit differ. The release verifier compares
        # two packages by hash, so a timestamp here turns a reproducible release into
        # a failing check with no other symptom.
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /MP /Brepro)
        target_link_options(${target} PRIVATE /Brepro)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
