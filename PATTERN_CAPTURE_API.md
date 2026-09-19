# Pattern capture API handoff

## Goal

Capture a local scene pattern—peds, props and vehicles plus the relationships
needed to reproduce it—with one game-thread request. The endpoint is read-only.
It must return facts; it must not guess missing source data or silently omit
fields.

The current `GET /world/nearby` returns only id/type/model label, position and
bounds. A seven-slot start tableau therefore required `GET /world/player`, one
`GET /world/nearby`, then a manual join against Spooner XML for rotations,
physics flags, pedestal tints, ped animations, vehicle customization, loaded-map
provenance and hero↔vehicle alignment.

## Endpoint

`POST /world/pattern-snapshot`

The request is explicit; there are no implicit defaults.

```json
{
  "scope": "spooner",
  "origin": {"kind": "player"},
  "radius": 45.0,
  "types": ["ped", "vehicle", "prop"],
  "maxEntities": 400,
  "frame": {"kind": "world"},
  "supportProbe": {
    "enabled": true,
    "maxDrop": 3.0,
    "sample": "center-and-four-corners"
  },
  "include": {
    "geometry": true,
    "physics": true,
    "attachments": true,
    "pedState": true,
    "vehicleCustomization": true,
    "propCustomization": true,
    "sourceProvenance": true
  }
}
```

For pattern capture, callers MUST send `scope:"spooner"`. It selects entities
owned by Spooner EntityDb or tracked by the map loader, excluding the current
player and the player's current vehicle even when Spooner owns them. Selection
is applied after the requested radius/type filter and before `maxEntities`.
The response carries `scope` and `excluded:{count,entities:[{id,type,state,
reason}]}`. Each excluded candidate has `state:"out-of-scope"` and reason
`player`, `player_current_vehicle`, or `not_spooner_owned`. This is explicit
selection, not partial capture: every selected entity must still satisfy all
requested include fields or the entire request fails.

`scope:"world"` retains the original all-world selection. Omitting `scope`
retains that existing request's behavior for compatibility; it does not infer
Spooner scope or retry. Unknown scope values fail with `400`. In world scope,
an untracked player has no Spooner-authored `dynamic` metadata and exact physics
capture returns `409`; it is never replaced with `true` or `!IS_ENTITY_STATIC`.

`origin` is a discriminated union:

- `{"kind":"player"}`;
- `{"kind":"position","position":{"x":1,"y":2,"z":3}}`.

`frame` is a discriminated union:

- `{"kind":"world"}`;
- `{"kind":"entity","entityId":123}`;
- `{"kind":"transform","position":...,"rotation":...}`.

Every requested include flag is required. Missing or unknown fields are `400`,
not defaults. Radius must be `(0, 500]`; `maxEntities` must be `[1, 400]`.

## Response

```json
{
  "origin": {"x": 2118.2, "y": -5097.3, "z": 590.6},
  "frame": {
    "position": {"x": 0, "y": 0, "z": 0},
    "rotation": {"pitch": 0, "roll": 0, "yaw": 0}
  },
  "loadedMaps": [
    {"name": "2026-4", "path": "menyooStuff/Spooner/2026-4.xml"}
  ],
  "count": 28,
  "truncated": false,
  "entities": [],
  "supportEdges": []
}
```

Each entity has this common shape:

```json
{
  "id": 123,
  "type": "ped",
  "spoonerName": "hero_slot_04",
  "model": {"hash": "0xfdcbb669", "internalName": "SpidermanPs4", "displayName": "Spider-Man PS4"},
  "transform": {
    "position": {"x": 2107.03, "y": -5080.20, "z": 599.18},
    "rotation": {"pitch": 0, "roll": 0, "yaw": -88.56}
  },
  "relativeTransform": {
    "position": {"x": 2107.03, "y": -5080.20, "z": 599.18},
    "rotation": {"pitch": 0, "roll": 0, "yaw": -88.56}
  },
  "geometry": {
    "localBounds": {"min": {"x": 0, "y": 0, "z": 0}, "max": {"x": 0, "y": 0, "z": 0}},
    "worldBounds": {"min": {"x": 0, "y": 0, "z": 0}, "max": {"x": 0, "y": 0, "z": 0}},
    "size": {"x": 0, "y": 0, "z": 0},
    "restZOffset": 0.0
  },
  "physics": {"dynamic": true, "frozen": false, "gravity": true, "collision": true},
  "attachment": {"state": "unattached"},
  "source": {"state": "tracked", "mapName": "2026-4", "mapPath": "menyooStuff/Spooner/2026-4.xml", "placementIndex": 428},
  "ped": {}
}
```

`attachment` is either `{"state":"unattached"}` or
`{"state":"attached","parentId":...,"bone":...,"offset":...,"rotation":...}`.
`source` is either the tracked record above or
`{"state":"untracked","reason":"not_created_by_spooner_loader"}`. Do not
substitute a nearest XML placement.

Type-specific fields:

- `ped`: `still`, `canRagdoll`, `scenario` as
  `{"active":false}` or `{"active":true,"name":"..."}`, and `animation` as
  `{"active":false}` or
  `{"active":true,"dict":"...","name":"...","flags":...,"speed":...,"phase":...}`.
- `vehicle`: indexed primary/secondary/pearl/rim/interior/dashboard/xenon,
  primary and secondary custom-colour flags plus RGB, native livery, mod-slot
  livery, wheel type, every mod, neon enabled sides/RGB and tyre-smoke RGB.
- `prop`: `textureVariation`.

Each support edge is factual probe output, not semantic inference:

```json
{
  "supportedEntityId": 123,
  "state": "hit",
  "supportEntityId": 456,
  "contactPoint": {"x": 1, "y": 2, "z": 3},
  "verticalGap": 0.005,
  "xyOverlapArea": 0.42,
  "supportedFootprintRatio": 0.91,
  "probeSamples": [
    {"sample":"center","hit":true,"entityId":456,"point":{"x":1,"y":2,"z":3}}
  ]
}
```

No hit is explicit:
`{"supportedEntityId":123,"state":"none","probeSamples":[...]}`.

The response order is deterministic: type `prop,ped,vehicle`, then source
placement index for tracked entities, then entity id. `truncated=true` is never
a successful capture: return `409` with `observedCount` and `maxEntities`
instead, so callers cannot unknowingly save a partial pattern.

## Provenance implementation invariant

The Spooner loader must register `entity handle -> {mapName,mapPath,
placementIndex}` when each placement is created and remove it when the entity is
deleted. Pattern capture reads that registry. Nearest-position XML matching is
not an acceptable fallback.

## Errors

- `400`: malformed union, omitted include flag, invalid radius/limit/type.
- `404`: explicit frame entity does not exist.
- `409`: entity count exceeds `maxEntities`, source registry says a tracked
  entity belongs to an unloaded/stale map, or one game-frame snapshot cannot be
  made consistently.
- `503`: game fiber is unavailable; no partial payload.

No retry is built into the endpoint.

## Tests and acceptance

1. Unit: request validation rejects every missing required field and invalid
   discriminated-union combination.
2. Unit: relative transforms round-trip from world frame and an entity frame.
3. Unit: support edges correctly report full, partial and absent support from
   deterministic test bounds/probe hits.
4. Unit: tracked/untracked provenance is explicit; stale registry is `409`.
5. Unit: ped animation/scenario and vehicle customization serialize every
   field without zero/default substitution.
6. Integration: load `2026-4`, call the endpoint once around the start, and
   assert 14 pedestal props, seven hero peds and seven motorcycles; exact
   placement indices; seven distinct animation records; slot-4
   `SpidermanPs4` aligned to `bati2` livery `1` with lateral error about
   `0.00733 m`.
7. Integration: response JSON alone is sufficient to reconstruct all 28
   entities in a translated rigid frame with max hero↔bike lateral alignment
   error `<= 0.27051 m`; no XML read is used in the test.
8. Performance: one request for 100 entities completes inside the existing
   4-second route deadline without queue growth; 400 entities either completes
   inside an explicitly measured deadline or returns a non-partial error.

Acceptance is one HTTP request plus deterministic local grouping. It replaces
the old flow of player lookup, nearby scan, source-map search and manual
cross-file joins. Automatic semantic names such as “hero slot” are out of
scope: the endpoint exports facts and stable relative geometry only.
