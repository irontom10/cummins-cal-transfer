cmake_minimum_required(VERSION 3.22.1)
project(calibrationtransfer C)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(caltransfer SHARED
    src/main/cpp/native_bridge.c
    src/main/cpp/rp1210_transport_android.c

    ../../core/ct_platform.c
    ../../core/config_store.c
    ../../core/j1939_transport.c
    ../../core/clip_transfer.c
    ../../core/echo_transfer.c
    ../../core/clip_crypto.c
    ../../core/clip_cal.c
    ../../core/ccal_crc.c
)

target_include_directories(caltransfer PRIVATE
    src/main/cpp
    ../../core
)

find_library(log-lib log)
find_library(dl-lib dl)

target_link_libraries(caltransfer
    ${log-lib}
    ${dl-lib}
)
