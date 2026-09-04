#pragma once

#define PLUG_NAME "RVC Realtime"
#define PLUG_MFR "RVC Project"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'Rvcr'
#define PLUG_MFR_ID 'Rvcp'
#define PLUG_URL_STR "https://github.com/iPlug2/iPlug2"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 RVC-BOSS"
#define PLUG_CLASS_NAME RVCRealtime

#define BUNDLE_NAME "RVCRealtime"
#define BUNDLE_MFR "RVCProject"
#define BUNDLE_DOMAIN "org"
#define SHARED_RESOURCES_SUBPATH "RVCRealtime"

#define PLUG_CHANNEL_IO "1-1 1-2 2-2"
#define PLUG_LATENCY 12480
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 780
#define PLUG_HEIGHT 630
#define PLUG_FPS 30
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1

#define VST3_SUBCATEGORY "Fx"
#define ROBOTO_FN "Roboto-Regular.ttf"

// AUv2 (.component) target. factoryFunction/NSPrincipalClass in
// resources/RVCRealtime-AU-Info.plist must match these symbol names exactly.
#define AUV2_ENTRY RVCRealtime_Entry
#define AUV2_ENTRY_STR "RVCRealtime_Entry"
#define AUV2_FACTORY RVCRealtime_Factory
#define AUV2_VIEW_CLASS RVCRealtime_View
#define AUV2_VIEW_CLASS_STR "RVCRealtime_View"

// Required by iPlug2's APP (standalone) host, used for the macOS human GUI
// check ahead of the real AU target.
#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define RVC_WORKER_RELATIVE_PATH "worker\\rvc_worker.py"
#define RVC_VST2_RESOURCES_DIR "RVCRealtime.resources"
#define RVC_DEFAULT_ROOT ""
#define RVC_DEFAULT_PYTHON ""
#define RVC_DEFAULT_MODEL ""
#define RVC_DEFAULT_INDEX ""
