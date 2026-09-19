#!/usr/bin/env python3
from pathlib import Path
import re


source = (Path(__file__).parents[1] / "source/Http/HttpServer.cpp").read_text()
route = re.search(
    r'server\.Get\("/maps".*?\n\s*\}\);',
    source,
    flags=re.DOTALL,
)

assert route is not None, "GET /maps route is missing"
body = route.group(0)
assert "direct(response" in body, "GET /maps must execute directly on the HTTP thread"
assert "Handle(request, response" not in body, "GET /maps must not enter CommandQueue"

print("  ok   GET /maps executes directly without CommandQueue")

health = re.search(
    r'server\.Get\("/health".*?\n\s*\}\);',
    source,
    flags=re.DOTALL,
)
assert health is not None, "GET /health route is missing"
health_body = health.group(0)
assert "Heartbeat().Snapshot" in health_body, "/health must read the game-fiber heartbeat"
assert 'snapshot.stalled ? 503 : 200' in health_body, "/health must return 503 for a stalled fiber"
assert '"stage"' in health_body, "/health must expose the last semantic game-fiber stage"
assert '"runningCommands"' in health_body, "/health must expose commands already executing"

print("  ok   GET /health reports game-fiber liveness and command lifecycle")
