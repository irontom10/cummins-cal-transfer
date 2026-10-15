cmake_minimum_required(VERSION 3.21)
project(CalibrationTransferRaylib LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "This build currently uses the Windows RP1210 backend.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "This target requires MSVC with x86 tools.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure for x86 (-A Win32): vendor RP1210 DLLs are 32-bit.")
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(CORE "${REPO_ROOT}/src/core")
set(WIN_NATIVE "${REPO_ROOT}/src/windows/native")

find_package(raylib 5.5 QUIET)
if(NOT TARGET raylib)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(CalibrationTransferRaylib WIN32
    main.c
    native_bridge.c
    "${WIN_NATIVE}/rp1210scan.c"
    "${WIN_NATIVE}/rp1210_transport.c"
    "${CORE}/j1939_transport.c"
    "${CORE}/ct_platform.c"
    "${CORE}/clip_transfer.c"
    "${CORE}/echo_transfer.c"
    "${CORE}/clip_crypto.c"
    "${CORE}/clip_cal.c"
    "${CORE}/xcal_crc.c"
    "${CORE}/config_store.c"
)
set_target_properties(CalibrationTransferRaylib PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    OUTPUT_NAME "CalibrationTransferRaylib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")
target_include_directories(CalibrationTransferRaylib PRIVATE
    "${CORE}" "${WIN_NATIVE}" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(CalibrationTransferRaylib PRIVATE
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(CalibrationTransferRaylib PRIVATE
    raylib kernel32 user32 gdi32 comdlg32 winmm)
