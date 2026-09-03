cmake_minimum_required(VERSION 3.14)

project(RVCRealtime VERSION 0.1.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RVC_BUILD_SMOKE_TEST "Build the standalone worker smoke test" ON)
option(RVC_MAC_LEGACY_EMBEDDED_WORKER
    "Use the legacy macOS Python spawn/shared-file worker instead of localhost RSVC"
    OFF)

set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Keep plugin artifacts inside this project" FORCE)
set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/iPlug2" CACHE PATH "iPlug2 root directory")

# Windows keeps the existing VST2/VST3 target set. macOS builds the standalone
# APP (human GUI check, no DAW/auval needed) plus AU (AUv2 .component, issue #5:
# uses Apple's system AudioUnit/CoreAudioKit frameworks, no extra SDK to fetch).
if(WIN32)
    set(RVC_FORMATS_DEFAULT VST2 VST3)
else()
    set(RVC_FORMATS_DEFAULT APP AU)
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

# Windows retains its established shared-memory worker. macOS defaults to the
# localhost RSVC thin head; the in-AU Python spawn bridge remains available only
# as an explicit recovery option while the migration is being validated.
if(WIN32)
    set(RVC_WORKER_CLIENT_SOURCE src/WorkerClient_win.cpp)
elseif(APPLE)
    if(RVC_MAC_LEGACY_EMBEDDED_WORKER)
        set(RVC_WORKER_CLIENT_SOURCE src/WorkerClient_mac.cpp)
        add_compile_definitions(RVC_MAC_LEGACY_EMBEDDED_WORKER=1)
    else()
        set(RVC_WORKER_CLIENT_SOURCE src/WorkerClient_stream_mac.cpp)
    endif()
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

# The RSVC thin head owns neither Python nor its worker script. Only legacy
# macOS builds retain the bundled script. Windows packaging remains separate.
set(RVC_PLUGIN_RESOURCES resources/fonts/Roboto-Regular.ttf)
if(APPLE AND RVC_MAC_LEGACY_EMBEDDED_WORKER)
    list(APPEND RVC_PLUGIN_RESOURCES worker/rvc_worker.py)
endif()

iplug_add_plugin(${PROJECT_NAME}
    SOURCES
        ${RVC_PLUGIN_SOURCES}
    RESOURCES
        ${RVC_PLUGIN_RESOURCES}
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

if(RVC_BUILD_SMOKE_TEST AND (WIN32 OR APPLE))
    # rvc-worker-smoke exercises the real IPC bridge end-to-end (issue #3/#6) on
    # whichever platform has one; it's meaningless against WorkerClient_stub_mac.
    # It isn't a bundle, so WorkerClient_mac.cpp's BundleResourcePath lookup
    # can't find rvc_worker.py here — point it at the source tree's copy via
    # RVC_WORKER_SCRIPT (see workerScriptPath()'s override) when running it.
    add_executable(rvc-worker-smoke
        tools/worker_smoke.cpp
        ${RVC_WORKER_CLIENT_SOURCE}
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
    if(APPLE AND RVC_MAC_LEGACY_EMBEDDED_WORKER)
        # WorkerClient_mac.cpp includes IPlugPaths.h (BundleResourcePath is
        # skipped at preprocessing time here since AU_API/APP_API aren't defined
        # for this plain executable, but the header still needs to resolve).
        target_include_directories(rvc-worker-smoke PRIVATE
            "${IPLUG2_DIR}/IPlug"
            "${IPLUG2_DIR}/WDL")
    endif()
    if(WIN32)
        add_custom_command(TARGET rvc-worker-smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:rvc-worker-smoke>/RVCRealtime.resources/worker"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/worker/rvc_worker.py"
                "$<TARGET_FILE_DIR:rvc-worker-smoke>/RVCRealtime.resources/worker/rvc_worker.py")
    endif()

    # VST2 is a Windows-only legacy format for this fork; the compat shim under
    # third_party/vst2-compat targets MSVC only, so keep this smoke test off macOS.
    if(WIN32)
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
endif()
