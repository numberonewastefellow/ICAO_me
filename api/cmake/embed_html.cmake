file(READ ${INPUT} CONTENT HEX)
string(REGEX MATCHALL ".." BYTES "${CONTENT}")
list(LENGTH BYTES LEN)

set(BODY "")
set(IDX 0)
foreach(B IN LISTS BYTES)
    if(IDX GREATER 0)
        string(APPEND BODY ",")
        math(EXPR LB "${IDX} % 16")
        if(LB EQUAL 0)
            string(APPEND BODY "\n    ")
        endif()
    endif()
    string(APPEND BODY "0x${B}")
    math(EXPR IDX "${IDX} + 1")
endforeach()

file(WRITE ${OUTPUT}
"// auto-generated from web/index.html — do not edit
#pragma once
#include <cstddef>
namespace ofiq_api {
inline constexpr unsigned char kIndexHtml[] = {
    ${BODY}
};
inline constexpr std::size_t kIndexHtmlLen = ${LEN};
} // namespace ofiq_api
")
