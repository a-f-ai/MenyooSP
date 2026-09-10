#!/usr/bin/env bash
# Builds and runs the host-side tests for the HTTP bridge.
#
# CommandQueue.cpp logs through Menyoo's FileLogger, which pulls in Windows
# headers, so we assemble a small tree where that one include is replaced by a
# stub and compile against it. Nothing else about the source is changed.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$here/../source"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

mkdir -p "$build/Http" "$build/Util"
cp "$source_root/Http/CommandQueue.h" "$source_root/Http/CommandQueue.cpp" "$build/Http/"
cp "$here/CommandQueueTests.cpp" "$build/Http/"

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
"$compiler" -std=c++20 -pthread -Wall -Wextra -O1 \
    -I"$build/Http" \
    "$build/Http/CommandQueue.cpp" "$build/Http/CommandQueueTests.cpp" \
    -o "$build/tests"

"$build/tests"
