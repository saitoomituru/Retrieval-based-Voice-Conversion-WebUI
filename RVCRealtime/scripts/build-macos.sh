#!/usr/bin/env bash
# macOS build for RVCRealtime. Builds the APP (standalone, human GUI
# check) and AU (AUv2 .component) targets; VST2/VST3 stay Windows-only for this
# implementation (see CMakeLists.txt RVC_FORMATS). Windows packaging (scripts/build.ps1) is
# untouched by this script.
#
# Usage: scripts/build-macos.sh [--config Debug|Release] [--targets "t1 t2"]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-macos"
IPLUG2_DIR="${ROOT}/third_party/iPlug2"

CONFIG="Debug"
TARGETS="RVCRealtime-app RVCRealtime-au"
while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONFIG="$2"; shift 2 ;;
    --targets) TARGETS="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Install via 'brew install cmake' or https://cmake.org/download/." >&2
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode command line tools not found. Install via 'xcode-select --install'." >&2
  exit 1
fi

if [ ! -f "${IPLUG2_DIR}/iPlug2.cmake" ]; then
  echo "Fetching third_party/iPlug2 submodule..."
  git -C "${ROOT}/.." submodule update --init RVCRealtime/third_party/iPlug2
fi

if [ ! -d "${IPLUG2_DIR}/Dependencies/IGraphics/NanoVG" ]; then
  echo "Fetching iPlug2 prebuilt macOS dependencies (IGraphics/NanoVG etc.)..."
  ( cd "${IPLUG2_DIR}/Dependencies" && bash download-prebuilt-libs.sh mac )
fi

echo "Configuring (${BUILD_DIR})..."
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Xcode

echo "Building: ${TARGETS} (${CONFIG})"
for target in ${TARGETS}; do
  cmake --build "${BUILD_DIR}" --target "${target}" --config "${CONFIG}"
done

for artifact in \
  "${BUILD_DIR}/out/${CONFIG}/RVCRealtime.app" \
  "${BUILD_DIR}/out/${CONFIG}/RVCRealtime.component"; do
  if [ -e "${artifact}" ] && ! codesign --verify --deep --strict "${artifact}" >/dev/null 2>&1; then
    echo "Applying local ad-hoc signature: ${artifact}"
    codesign --force --deep --sign - "${artifact}"
  fi
done

echo "Built artifacts under: ${BUILD_DIR}/out/${CONFIG}"
