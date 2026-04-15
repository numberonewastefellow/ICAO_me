cmake_minimum_required(VERSION 3.16)
project(ofiq_api LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-DNDEBUG>
)

# ----------------------------------------------------------------------------
# OFIQ paths (set by Dockerfile.api: -DOFIQ_INCLUDE_DIR=... -DOFIQ_LIB_DIR=...)
# ----------------------------------------------------------------------------
if(NOT DEFINED OFIQ_INCLUDE_DIR)
    set(OFIQ_INCLUDE_DIR "/opt/ofiq/include")
endif()
if(NOT DEFINED OFIQ_LIB_DIR)
    set(OFIQ_LIB_DIR "/opt/ofiq/Release/lib")
endif()
if(NOT DEFINED VENDOR_DIR)
    set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
endif()

message(STATUS "OFIQ_INCLUDE_DIR = ${OFIQ_INCLUDE_DIR}")
message(STATUS "OFIQ_LIB_DIR     = ${OFIQ_LIB_DIR}")
message(STATUS "VENDOR_DIR       = ${VENDOR_DIR}")

find_library(OFIQ_LIBRARY
    NAMES ofiq_lib
    PATHS ${OFIQ_LIB_DIR}
    NO_DEFAULT_PATH REQUIRED)

# ONNX Runtime ships as libonnxruntime.so.1.18.1 (with a version suffix that
# CMake's find_library cannot match). It is loaded transitively via
# libofiq_lib.so's DT_NEEDED, so we do not need to link it explicitly.
# We still teach the linker where to find it for the runtime path search.
link_directories(${OFIQ_LIB_DIR})

# ----------------------------------------------------------------------------
# Server target
# ----------------------------------------------------------------------------
add_executable(ofiq_server
    src/main.cc
    src/server.cc
    src/worker_pool.cc
    src/ofiq_runner.cc
    src/verdict.cc
    src/metrics.cc
    src/log.cc
)

target_include_directories(ofiq_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OFIQ_INCLUDE_DIR}
    ${VENDOR_DIR}
)

target_link_libraries(ofiq_server PRIVATE
    ${OFIQ_LIBRARY}
    pthread
    dl
)

# embed default index.html as a string literal at build time
set(INDEX_HTML_PATH "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
set(INDEX_HTML_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/index_html.h")

add_custom_command(
    OUTPUT ${INDEX_HTML_GENERATED}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${INDEX_HTML_PATH}
        -DOUTPUT=${INDEX_HTML_GENERATED}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake
    DEPENDS ${INDEX_HTML_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_html.cmake
    VERBATIM
)
add_custom_target(generate_index_html DEPENDS ${INDEX_HTML_GENERATED})
add_dependencies(ofiq_server generate_index_html)
target_include_directories(ofiq_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ----------------------------------------------------------------------------
# Tests (CTest)
# ----------------------------------------------------------------------------
enable_testing()

add_executable(verdict_test
    tests/verdict_test.cc
    src/verdict.cc
    src/log.cc
)
target_include_directories(verdict_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OFIQ_INCLUDE_DIR}
    ${VENDOR_DIR}
)
add_test(NAME verdict_test COMMAND verdict_test)

add_executable(image_decode_test
    tests/image_decode_test.cc
    src/ofiq_runner.cc
    src/log.cc
)
target_include_directories(image_decode_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OFIQ_INCLUDE_DIR}
    ${VENDOR_DIR}
)
target_link_libraries(image_decode_test PRIVATE ${OFIQ_LIBRARY} pthread dl)
add_test(NAME image_decode_test COMMAND image_decode_test)
