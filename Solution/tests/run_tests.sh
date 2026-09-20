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

cp "$source_root/Http/GameFiberHeartbeat.h" "$source_root/Http/GameFiberHeartbeat.cpp" "$build/Http/"
cp "$here/GameFiberHeartbeatTests.cpp" "$build/Http/"

cp "$source_root/Http/PlayerInput.h" "$build/Http/"
cp "$here/PlayerInputTests.cpp" "$build/Http/"

cp "$source_root/Http/PlayerCommand.h" "$source_root/Http/PlayerCommand.cpp" \
   "$source_root/Http/ApiError.h" "$source_root/Http/Json.h" "$build/Http/"
cp "$here/PlayerCommandTests.cpp" "$build/Http/"

cp "$source_root/Submenus/Spooner/CameraPath.h" \
   "$source_root/Submenus/Spooner/CameraPath.cpp" "$build/Submenus/Spooner/"
cp "$here/CameraPathTests.cpp" "$build/Submenus/Spooner/"
cp "$source_root/Util/GTAmath.h" "$source_root/Util/GTAmath.cpp" "$build/Util/"

cp "$source_root/Util/NameEncoding.h" "$source_root/Util/NameEncoding.cpp" "$build/Util/"
cp "$here/MapRepairTests.cpp" "$build/Util/"

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

pugi_include="$source_root/../external"
pugi_source="$source_root/../external/pugixml/src/pugixml.cpp"

compiler="${CXX:-c++}"
# Menyoo's own sources are not warning-clean under -Wall; the tests are about
# behaviour, so the noise is turned off rather than chased.
flags=(-std=c++20 -O1 -w)

echo "--- HTTP routing boundary ---"
python3 "$here/HttpRoutingTests.py"

echo "--- keyboard chord event sequence ---"
"$compiler" "${flags[@]}" -I"$source_root/Util" "$here/KeyboardChordTests.cpp" -o "$build/chord_tests"
"$build/chord_tests"

echo "--- menu close and reopen state ---"
"$compiler" "${flags[@]}" -I"$source_root/Menu" "$here/MenuStateTests.cpp" -o "$build/menu_tests"
"$build/menu_tests"

echo "--- pattern snapshot ---"
"$compiler" "${flags[@]}" -pthread -I"$source_root/Http" -I"$source_root/../external" \
    "$source_root/Http/PatternSnapshot.cpp" "$here/PatternSnapshotTests.cpp" -o "$build/pattern_tests"
"$build/pattern_tests"

echo "--- spiderman bike action ---"
"$compiler" "${flags[@]}" -I"$source_root/Http" -I"$source_root/../external" \
    "$source_root/Http/SpidermanBike.cpp" "$here/SpidermanBikeTests.cpp" -o "$build/bike_tests"
"$build/bike_tests"

echo

echo "--- command queue ---"
"$compiler" "${flags[@]}" -pthread \
    -I"$build/Http" \
    "$build/Http/CommandQueue.cpp" "$build/Http/CommandQueueTests.cpp" \
    -o "$build/queue_tests"
"$build/queue_tests"

echo
echo "--- game fiber heartbeat ---"
"$compiler" "${flags[@]}" -pthread \
    -I"$build/Http" \
    "$build/Http/GameFiberHeartbeat.cpp" "$build/Http/GameFiberHeartbeatTests.cpp" \
    -o "$build/heartbeat_tests"
"$build/heartbeat_tests"

echo
echo "--- player input validation ---"
"$compiler" "${flags[@]}" \
    -I"$build/Http" \
    "$build/Http/PlayerInputTests.cpp" \
    -o "$build/player_input_tests"
"$build/player_input_tests"

echo
echo "--- player command HTTP boundary ---"
"$compiler" "${flags[@]}" \
    -I"$build/Http" -I"$source_root/../external" \
    "$build/Http/PlayerCommand.cpp" "$build/Http/PlayerCommandTests.cpp" \
    -o "$build/player_command_tests"
"$build/player_command_tests"

echo
echo "--- map name repair ---"
"$compiler" "${flags[@]}" \
    -I"$build/Util" \
    "$build/Util/NameEncoding.cpp" "$build/Util/MapRepairTests.cpp" \
    -o "$build/maprepair_tests"
"$build/maprepair_tests"

echo
echo "--- camera path ---"
"$compiler" "${flags[@]}" \
    -I"$build/Submenus/Spooner" \
    "$build/Submenus/Spooner/CameraPath.cpp" \
    "$build/Submenus/Spooner/CameraPathTests.cpp" \
    "$build/Util/GTAmath.cpp" \
    -o "$build/campath_tests"
"$build/campath_tests"
