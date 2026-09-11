#!/usr/bin/env bash
# Builds and runs the host-side tests: the pieces of the plugin that hold no
# natives and can therefore be checked without a game or a Windows SDK.
#
# The sources are copied into a tree with the same shape as Solution/source so
# their relative includes resolve unchanged. The one substitution is
# Util/FileLogger.h, which reaches Windows headers; everything else is the real
# code.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$here/../source"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

mkdir -p "$build/Http" "$build/Util" "$build/Submenus/Spooner"

cp "$source_root/Http/CommandQueue.h" "$source_root/Http/CommandQueue.cpp" "$build/Http/"
cp "$here/CommandQueueTests.cpp" "$build/Http/"

cp "$source_root/Submenus/Spooner/CameraPath.h" \
   "$source_root/Submenus/Spooner/CameraPath.cpp" "$build/Submenus/Spooner/"
cp "$here/CameraPathTests.cpp" "$build/Submenus/Spooner/"
cp "$source_root/Util/GTAmath.h" "$source_root/Util/GTAmath.cpp" "$build/Util/"

cat > "$build/Util/FileLogger.h" <<'STUB'
#pragma once
#include <cstdio>
#include <string>
namespace ige { enum class LogType { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR }; }
inline void addlog(ige::LogType, const std::string& message)
{
    std::printf("       [log] %s\n", message.c_str());
}
STUB

compiler="${CXX:-c++}"
# Menyoo's own sources are not warning-clean under -Wall; the tests are about
# behaviour, so the noise is turned off rather than chased.
flags=(-std=c++20 -O1 -w)

echo "--- command queue ---"
"$compiler" "${flags[@]}" -pthread \
    -I"$build/Http" \
    "$build/Http/CommandQueue.cpp" "$build/Http/CommandQueueTests.cpp" \
    -o "$build/queue_tests"
"$build/queue_tests"

echo
echo "--- camera path ---"
"$compiler" "${flags[@]}" \
    -I"$build/Submenus/Spooner" \
    "$build/Submenus/Spooner/CameraPath.cpp" \
    "$build/Submenus/Spooner/CameraPathTests.cpp" \
    "$build/Util/GTAmath.cpp" \
    -o "$build/campath_tests"
"$build/campath_tests"
