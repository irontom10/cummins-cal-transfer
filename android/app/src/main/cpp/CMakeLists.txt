cmake_minimum_required(VERSION 3.22.1)
project(caltransfer C)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../../src/c")

add_library(caltransfer SHARED
    android_jni.c
    android_j1939_transport.c
    "${CORE_DIR}/clip_transfer.c"
    "${CORE_DIR}/echo_transfer.c"
    "${CORE_DIR}/clip_crypto.c"
    "${CORE_DIR}/clip_cal.c"
    "${CORE_DIR}/ccal_crc.c"
)

target_include_directories(caltransfer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CORE_DIR}"
)

target_compile_definitions(caltransfer PRIVATE _POSIX_C_SOURCE=200809L)

find_library(log-lib log)
target_link_libraries(caltransfer "${log-lib}")
