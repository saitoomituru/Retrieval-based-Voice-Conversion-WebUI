cmake_minimum_required(VERSION 3.14)

project(RVCRealtime VERSION 0.1.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RVC_BUILD_SMOKE_TEST "Build the standalone worker smoke test" ON)

set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Keep plugin artifacts inside this project" FORCE)
set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/iPlug2" CACHE PATH "iPlug2 root directory")

# Windows keeps the existing VST2/VST3 target set. macOS defaults to the
# standalone APP format only: it is the fastest way to get a launchable
# window for a human GUI check (issue #5's AU target is a separate step
# that still needs the VST/AU SDKs prepared below).
if(WIN32)
    set(RVC_FORMATS_DEFAULT VST2 VST3)
else()
    set(RVC_FORMATS_DEFAULT APP)
endif()
set(RVC_FORMATS "${RVC_FORMATS_DEFAULT}" CACHE STRING "iPlug2 plugin formats to build for this platform")

if("VST2" IN_LIST RVC_FORMATS OR "VST3" IN_LIST RVC_FORMATS)
    if(NOT EXISTS "${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK/base/source/baseiids.cpp"
       OR NOT EXISTS "${IPLUG2_DIR}/Dependencies/IPlug/VST2_SDK/aeffectx.h")
        message(FATAL_ERROR
            "Prepared VST SDK files are missing for RVC_FORMATS=${RVC_FORMATS}. Run "
            "scripts/prepare-dependencies.ps1 or use scripts/build.ps1, which runs it "
            "automatically.")
    endif()
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# WorkerClient_win.cpp is the real IPC bridge (Windows only). Every other
# platform links WorkerClient_stub_mac.cpp, a compile-only placeholder that
# lets the UI build and run without a functioning worker — see
# src/WorkerClient_stub_mac.cpp for what it deliberately does not do.
if(WIN32)
    set(RVC_WORKER_CLIENT_SOURCE src/WorkerClient_win.cpp)
else()
    set(RVC_WORKER_CLIENT_SOURCE src/WorkerClient_stub_mac.cpp)
endif()

set(RVC_PLUGIN_SOURCES
    src/RVCRealtime.cpp
    src/RVCRealtime.h
    ${RVC_WORKER_CLIENT_SOURCE}
    src/WorkerClient.hpp
    src/SpscRing.hpp
    src/RvcParameters.hpp
    config.h
    resources/resource.h)
if(WIN32)
    list(APPEND RVC_PLUGIN_SOURCES resources/main.rc)
endif()

iplug_add_plugin(${PROJECT_NAME}
    SOURCES
        ${RVC_PLUGIN_SOURCES}
    RESOURCES
        resources/fonts/Roboto-Regular.ttf
    FORMATS
        ${RVC_FORMATS}
    UI IGRAPHICS
    DEFINES
        UNICODE
        _UNICODE
        NOMINMAX)

if(TARGET RVCRealtime-vst2)
    target_include_directories(RVCRealtime-vst2 BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2-compat"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2sdk/include")
endif()

if(MSVC)
    foreach(target RVCRealtime-vst2 RVCRealtime-vst3)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        endif()
    endforeach()
endif()

if(RVC_BUILD_SMOKE_TEST AND WIN32)
    # Both smoke tests below exercise the real Windows IPC bridge / VST2 compat
    # shim, neither of which macOS has yet (RVC_WORKER_CLIENT_SOURCE is the stub
    # there) — so this whole block is Windows-only.
    add_executable(rvc-worker-smoke
        tools/worker_smoke.cpp
        src/WorkerClient_win.cpp
        src/WorkerClient.hpp
        src/SpscRing.hpp
        src/RvcParameters.hpp
        config.h)
    target_include_directories(rvc-worker-smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(rvc-worker-smoke PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX)
    if(MSVC)
        target_compile_options(rvc-worker-smoke PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_custom_command(TARGET rvc-worker-smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:rvc-worker-smoke>/RVCRealtime.resources/worker"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/worker/rvc_worker.py"
            "$<TARGET_FILE_DIR:rvc-worker-smoke>/RVCRealtime.resources/worker/rvc_worker.py")

    add_executable(rvc-vst2-smoke tools/vst2_smoke.cpp)
    target_include_directories(rvc-vst2-smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2-compat"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2sdk/include")
    target_compile_definitions(rvc-vst2-smoke PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX)
    if(MSVC)
        target_compile_options(rvc-vst2-smoke PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
