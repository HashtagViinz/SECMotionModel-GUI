# Scene format v1

A GUI scene is a directory:

```text
scene/
  manifest.json      or scene.json
  frames/
    000000.json
    000001.json
  tasks.jsonl        optional
```

JSON Schemas live in `schema/`. The version field is `1`; the GUI rejects other
major versions.

## Manifest

```json
{
  "version": 1,
  "t0": "2026-11-02T21:32:33.681068Z",
  "interval_s": 2.0,
  "duration_s": 300.0,
  "earth_radius_km": 6371.0,
  "phi_max_deg": 40,
  "phi_buffer_deg": 15,
  "terminal": { "name": "V-ATLANTIC", "lat": 33.373586, "lon": -47.955604 },
  "observer": { "name": "OBS", "lat": 41.9028, "lon": 12.4964 },
  "frames": { "count": 150, "pattern": "frames/%06d.json" },
  "tasks": "tasks.jsonl"
}
```

`duration_s` is optional and defaults to `frame_count * interval_s`.

## Frame

Positions are Earth-fixed ECEF/ITRS kilometres (`ecef_km`). The terminal and the
observer therefore keep their geographic coordinates while satellites move.

```json
{
  "index": 0,
  "t": 0.0,
  "vessel": { "name": "V-ATLANTIC", "lat": 33.37, "lon": -47.95, "ecef_km": [3563.4, -3949.7, 3504.8] },
  "observer": { "name": "OBS", "lat": 41.90, "lon": 12.49, "ecef_km": [4628.5, 1025.3, 4255.5] },
  "satellites": [
    {
      "id": "STARLINK-5707",
      "ecef_km": [4000.0, -4200.0, 3500.0],
      "is_ap": true,
      "elev_deg": 55.6,
      "az_deg": 128.4,
      "range_km": 577.8,
      "life_remaining_s": 117,
      "terminal_ref": "V-ATLANTIC",
      "neighbors": [ { "id": "STARLINK-33957", "latency_ms": 0.59 } ]
    }
  ]
}
```

- `neighbors` are undirected ISL edges; the GUI de-duplicates them.
- `is_ap` marks the access point for `terminal_ref` (`null` for corridor-only
  satellites).
- `life_remaining_s` is the time until LOS; `null` when unknown.

## Tasks

One JSON object per line, sorted or sorted-on-load by `t`:

```jsonl
{"t":0.18,"event":"spawn","task":1,"node":"V-ATLANTIC","type":"CPU_and_Data_Intensive","size_mb":13.4}
{"t":0.18,"event":"hop_start","task":1,"from":"V-ATLANTIC","to":"STARLINK-37163","algo":"GREEDY","duration_s":0.25}
{"t":0.43,"event":"hop_end","task":1,"from":"V-ATLANTIC","to":"STARLINK-37163"}
{"t":0.43,"event":"compute","task":1,"node":"STARLINK-37163"}
{"t":1.5,"event":"complete","task":1,"node":"V-ATLANTIC","rtt_s":1.5}
```

Supported events: `spawn`, `hop_start`, `hop_end`, `compute`, `deliver`,
`complete`, `fail`. `fail` carries `reason` (`TTL_EXPIRED`, `Insufficient Energy
for CPU`, ...).

## Producing a scene from the simulator

The core repository owns the exporter:

```bash
cd SECMotionModel
python tools/export_gui_scene.py \
  --config data/naval_configurations_AP_optimal.json \
  --trace result/naval/IN_NETWORK/optimal/trace.jsonl \
  --out ../SECMotionModel-GUI/examples/generated/naval \
  --start 0 --end 150
```

The exporter converts Skyfield GCRS positions to ITRS (`frame_xyz(itrs)`),
de-duplicates ISL edges, keeps AP/life metadata and merges the SimPy task trace.

## Validation

- `secmotion-gui --validate-only --scene ...` checks a scene without a GPU.
- `python -m jsonschema -i examples/minimal/scene.json schema/scene-v1.schema.json`
  validates the manifest in CI.
- The GUI rejects non-finite numbers, duplicate satellite ids, dangling
  neighbor references and frames whose radius is implausible.
