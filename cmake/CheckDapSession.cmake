# Drives the VM's Debug Adapter Protocol server over a scripted session and
# asserts what a user of the debugger would notice: the breakpoint is verified,
# execution stops at it, the top frame names the expected function at the expected
# line, and a named local reads back the value the program computed.
#
# The session is a request stream on the server's stdin rather than an interactive
# dialogue. The server reads one request, answers it, and only then reads the next,
# so a complete request file produces a complete ordered response stream. Framing
# is a `Content-Length` header, a blank line, and then the payload.
#
# The image travels on the command line and `launch` carries no arguments, because
# the server falls back to that path; this keeps a filesystem path out of the JSON,
# where a backslash would need escaping and `string(LENGTH)` would stop measuring
# bytes. Every payload is therefore ASCII, which is what makes that length correct.
#
# Required: VM, IMAGE, BREAK_LINE, EXPECT_FUNCTION, EXPECT_SOURCE, EXPECT_VARIABLE,
# EXPECT_VALUE. EXPECT_VALUE is the transcript's spelling of the value: the
# `variables` response formats a word as `0x` and eight upper-case hex digits.
# EXPECT_SOURCE is the path KDBG recorded, which `krld --source-root` makes relative
# to the repository root.
#
# EXPECT_SOURCE exists because the server resolves a requested breakpoint by line
# number across the whole image and takes the first match, ignoring the file. The
# image links the program before the runtime, so the program's records come first --
# but that is link order rather than a property of the request, and without this
# assertion a stop in a runtime line that happens to carry the same number would
# still pass.

if(NOT DEFINED VM OR NOT DEFINED IMAGE OR NOT DEFINED BREAK_LINE OR
   NOT DEFINED EXPECT_FUNCTION OR NOT DEFINED EXPECT_SOURCE OR
   NOT DEFINED EXPECT_VARIABLE OR NOT DEFINED EXPECT_VALUE)
    message(FATAL_ERROR
            "CheckDapSession.cmake requires VM, IMAGE, BREAK_LINE, EXPECT_FUNCTION, "
            "EXPECT_SOURCE, EXPECT_VARIABLE, EXPECT_VALUE")
endif()
if(NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "CheckDapSession.cmake: no image at ${IMAGE}")
endif()

# The server matches a requested breakpoint by line number alone and ignores the
# `source` object a real client also sends, so only the line is carried here.
set(payloads
    "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"kr32\"}}"
    "{\"seq\":2,\"type\":\"request\",\"command\":\"launch\"}"
    "{\"seq\":3,\"type\":\"request\",\"command\":\"setBreakpoints\",\"arguments\":{\"breakpoints\":[{\"line\":${BREAK_LINE}}]}}"
    "{\"seq\":4,\"type\":\"request\",\"command\":\"configurationDone\"}"
    "{\"seq\":5,\"type\":\"request\",\"command\":\"continue\",\"arguments\":{\"threadId\":1}}"
    "{\"seq\":6,\"type\":\"request\",\"command\":\"stackTrace\",\"arguments\":{\"threadId\":1}}"
    "{\"seq\":7,\"type\":\"request\",\"command\":\"scopes\",\"arguments\":{\"frameId\":1}}"
    "{\"seq\":8,\"type\":\"request\",\"command\":\"variables\",\"arguments\":{\"variablesReference\":1}}"
    "{\"seq\":9,\"type\":\"request\",\"command\":\"disconnect\"}")

# `\n` alone, not `\r\n`: CMake writes this file in text mode, so on Windows each
# LF becomes CRLF and on Linux it stays LF. Writing `\r\n` would produce `\r\r\n` on
# Windows, which this server tolerates only because it strips a trailing CR from
# every header line -- a stricter adapter would reject it. The server accepts both
# line endings, so this form is correct on either host.
set(stream "")
foreach(payload IN LISTS payloads)
    string(LENGTH "${payload}" payload_length)
    string(APPEND stream "Content-Length: ${payload_length}\n\n${payload}")
endforeach()

set(requests "${IMAGE}.dap-requests.txt")
file(WRITE "${requests}" "${stream}")

execute_process(COMMAND "${VM}" dap "${IMAGE}"
                INPUT_FILE "${requests}"
                OUTPUT_VARIABLE transcript
                ERROR_VARIABLE server_error
                RESULT_VARIABLE session_result)
if(NOT session_result EQUAL 0)
    message(FATAL_ERROR
            "the DAP server exited with ${session_result}\n${transcript}${server_error}")
endif()

# Fails with the whole transcript, because a missing response and a wrong value
# are indistinguishable from the assertion alone.
function(require_dap_text text needle description)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
                "the debug session did not report ${description}\n"
                "expected: ${needle}\ntranscript:\n${text}")
    endif()
endfunction()

# Response bodies serialize object keys in sorted order, so inside one object the
# line precedes the name and a variable's name precedes its value. Those orders are
# what let a substring assert that the *same* frame and the *same* variable carry
# both facts, rather than two unrelated facts somewhere in the transcript.
require_dap_text("${transcript}"
                 "\"line\":${BREAK_LINE},\"verified\":true"
                 "line ${BREAK_LINE} as a verified breakpoint")

# A stop whose reason is `breakpoint` says `continue` reached the requested line;
# the session's entry stop reports reason `entry` and is not a hit.
require_dap_text("${transcript}" "\"reason\":\"breakpoint\"" "a stop at the breakpoint")

string(FIND "${transcript}" "\"stackFrames\":" frames_position)
if(frames_position EQUAL -1)
    message(FATAL_ERROR "the debug session did not report a stack trace\n${transcript}")
endif()
string(SUBSTRING "${transcript}" ${frames_position} -1 frames_text)
require_dap_text("${frames_text}"
                 "\"line\":${BREAK_LINE},\"name\":\"${EXPECT_FUNCTION}\""
                 "a stop frame at line ${BREAK_LINE} in ${EXPECT_FUNCTION}")
# The `name` and `source` keys are adjacent in the same frame object, so this one
# substring proves the frame is in EXPECT_SOURCE and not in another file whose line
# numbering happens to collide -- the failure the server's line-only matching allows.
require_dap_text("${frames_text}"
                 "\"name\":\"${EXPECT_FUNCTION}\",\"source\":{\"name\":\"source\",\"path\":\"${EXPECT_SOURCE}\"}"
                 "a stop frame in ${EXPECT_SOURCE}")

string(FIND "${transcript}" "\"variables\":[" variables_position)
if(variables_position EQUAL -1)
    message(FATAL_ERROR "the debug session did not report the frame's variables\n${transcript}")
endif()
string(SUBSTRING "${transcript}" ${variables_position} -1 variables_text)
require_dap_text("${variables_text}"
                 "\"name\":\"${EXPECT_VARIABLE}\""
                 "${EXPECT_VARIABLE} among the frame's variables")
require_dap_text("${variables_text}"
                 "\"value\":\"${EXPECT_VALUE}\""
                 "${EXPECT_VARIABLE} reading ${EXPECT_VALUE}")
