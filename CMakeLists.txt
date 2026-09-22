cmake_minimum_required(VERSION 3.20)
project(media_key_detector_windows_plugin LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(/EHsc /permissive- /utf-8)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/SmtcManager.cpp
    src/MediaController.cpp
    src/MetadataWatcher.cpp
    src/MpvManager.cpp
    src/minhook/hook.c
    src/minhook/buffer.c
    src/minhook/trampoline.c
    src/minhook/hde/hde64.c
    exports.def
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/minhook
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    windowsapp
    user32
    shell32
    ole32
    shlwapi
    advapi32
    windowscodecs
    propsys
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "media_key_detector_windows_plugin"
)
