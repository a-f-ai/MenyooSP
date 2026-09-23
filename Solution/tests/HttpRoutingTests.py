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

entity_api = (Path(__file__).parents[1] / "source/Http/EntityApi.cpp").read_text(encoding="utf-8-sig")
physics_guard = "if (type == EntityType::PROP && request.dynamic)"
assert physics_guard in entity_api, "dynamic prop creation must have an explicit physics activation guard"
guard_body = entity_api.split(physics_guard, 1)[1].split("}", 1)[0]
assert "ACTIVATE_PHYSICS(out.handle.Handle())" in guard_body, "dynamic props must enter simulation after being unfrozen"
print("  ok   dynamic props explicitly activate physics after creation")

file_management = (Path(__file__).parents[1] / "source/Submenus/Spooner/FileManagement.cpp").read_text(encoding="utf-8-sig")
file_management_header = (Path(__file__).parents[1] / "source/Submenus/Spooner/FileManagement.h").read_text(encoding="utf-8-sig")
routine = (Path(__file__).parents[1] / "source/Menu/Routine.cpp").read_text(encoding="utf-8-sig")

assert 'MapLoadCrashJournal.jsonl' in file_management, "map loads must use a dedicated persistent crash journal"
assert "fflush(" in file_management and "_commit(" in file_management, "every journal record must be forced to disk"
assert '"BEGIN"' in file_management and '"DONE"' in file_management, "journal must bracket risky operations"
assert "sourcePlacement" in file_management and 'nodeEntity.child("HashName")' in file_management, "placement records must identify index and name"
assert 'nodeEntity.child("ModelHash")' in file_management and 'nodeEntity.child("Type")' in file_management, "placement records must identify model and type"
for field in ("GET_FRAME_COUNT()", "GET_GAME_TIMER()", "GET_NUMBER_OF_STREAMING_REQUESTS()", "Databases::EntityDb.size()"):
    assert field in file_management, f"journal state is missing {field}"
for stage in ("load", "placements", "attachments", "teleport", "completion"):
    assert f'"{stage}"' in file_management, f"map load journal is missing the {stage} stage"
assert "TickMapLoadCrashJournal" in file_management_header, "post-load journal tick must be public to the game loop"
assert "FileManagement::TickMapLoadCrashJournal()" in routine, "post-load journal must continue from the game-thread loop"
assert '"POST_LOAD"' in file_management, "post-load journal must record ongoing scene state until the next load"

print("  ok   map loads write durable per-placement and post-load crash journal records")
