# Runs `krc -S` and prints the generated assembly so a row's regular expression
# sees it, now that the driver writes a file named for the target (G8) instead of
# streaming to stdout.
#
#   cmake -DKRC=<krc> -DSOURCE=<source> [-DTARGET=kr32] -P emit_asm.cmake
#
# The generated file is <stem>.<target>.s in the working directory, which is
# where the driver puts it. Nothing is weakened: the same assembly is printed and
# the same expressions match.
cmake_minimum_required(VERSION 3.25)

foreach(required KRC SOURCE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "pass -D${required}=<value>")
    endif()
endforeach()
if(NOT DEFINED TARGET)
    set(TARGET kr32)
endif()

execute_process(
    COMMAND "${KRC}" -S "--target=${TARGET}" "${SOURCE}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE discarded
    ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "krc -S failed (${status}): ${errors}${discarded}")
endif()

get_filename_component(stem "${SOURCE}" NAME_WE)
set(generated "${stem}.${TARGET}.s")
if(NOT EXISTS "${generated}")
    message(FATAL_ERROR "krc -S reported success but wrote no ${generated}")
endif()
file(READ "${generated}" assembly)
message(STATUS "${assembly}")
