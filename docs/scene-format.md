# Scene format v1/v2

A GUI scene is a directory:

```text
scene/
  manifest.json      or scene.json
  frames/
    000000.json
    000001.json
  tasks.jsonl        optional, task pulses
  loads.jsonl        optional, v2 per-satellite load
  traffic.jsonl      optional, v2 forward events
```

It can be opened directly, or through a single `.secsim` descriptor that lists
the paths of these files (see below). JSON Schemas live in `schema/`. The
manifest version field is `1` or `2`; the GUI rejects other major versions. A
v2 manifest can add live telemetry, while v1 scenes keep working unchanged.

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
  "tasks": "tasks.jsonl",
  "loads": "loads.jsonl",
  "traffic": "traffic.jsonl",
  "load": { "metric": "max", "cpu_capacity": 5, "net_capacity": 5 },
  "default_layers": { "load": true, "traffic": true, "tasks": false }
}
```

`duration_s` is optional and defaults to `frame_count * interval_s`. `loads`,
`traffic`, `load` and `default_layers` are optional v2 fields.

`default_layers` may contain any GUI layer name (`globe`, `atmosphere`, `grid`,
`isl`, `dome`, `route`, `satellites`, `load`, `traffic`, `tasks`). Unknown
names are stored but have no effect. The v1/v2 scenes that predate the textured
globe keep working: `globe` and `atmosphere` default to on, and the existing
`load`/`traffic` flags are unchanged.

## Single-file simulation descriptor (.secsim)

A scene can also be opened through one small JSON file with the `.secsim`
extension. It is a descriptor, not a copy of the scene: it points at the
manifest and sidecar files that the GUI already uses, so there is no directory
to select in the Import panel.

```json
{
  "format": "secgui-simulation",
  "version": 1,
  "name": "Terrestrial / optimal",
  "manifest": "../cache/terrestrial/manifest.json",
  "tasks": "../cache/terrestrial/tasks.jsonl",
  "loads": "../cache/terrestrial/loads.jsonl",
  "traffic": "../cache/terrestrial/traffic.jsonl",
  "source": { "config": "data/configurations_AP_optimal.json" }
}
```

- `manifest` is required and points to a normal v1/v2 scene manifest.
- `tasks`, `loads` and `traffic` are optional overrides. When absent, the
  manifest values are used.
- Relative paths are resolved against the directory that contains the
  `.secsim` file; absolute paths are also accepted. The frame pattern remains
  relative to the manifest.
- `name` and `source` are informational and shown by `state`.
- The GUI rejects nested simulation files and unsupported versions.

Open it exactly like a scene:

```bash
secmotion-gui --scene result.secsim
secmotion-gui --validate-only --scene result.secsim
```

The core exporters write `simulation.secsim` inside the scene directory by
default; use `--bundle PATH` to place the descriptor elsewhere.

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

The core exporter merges a trace with `--trace PATH`; simulation extensions
that emit the same events can also write it directly.

## Load samples (v2)

`loads.jsonl`, one sample per line, sorted by time. Each sample carries the
satellites **present in that orbital frame**; satellites outside the corridor
are absent, with no carry-forward.

```jsonl
{"t":217.0,"frame":108,"satellites":[{"id":"STARLINK-34100","cpu_queue":0,"cpu_service":2,"net_queue":0,"net_service":0,"completed":5,"energy_j":99352.3,"energy_reserved_j":2876.8}]}
```

The displayed metric is:

```text
max((cpu_queue + cpu_service) / cpu_capacity,
    (net_queue + net_service) / net_capacity)
```

`load.cpu_capacity` and `load.net_capacity` default to 5. The GUI uses the most
recent sample at or before the current time.

## Traffic events (v2)

`traffic.jsonl`, one real forward per line, sorted by `t0`:

```jsonl
{"t0":99.78,"t1":99.79,"from":"STARLINK-4425","to":"STARLINK-6122","task":205,"size_mb":2.09,"bandwidth_mbps":2321.0,"duration_s":0.002,"kind":"isl","algo":"GREEDY"}
```

`kind` is `isl` or `downlink`. The GUI colors recently used links and draws a
moving wave from `from` to `to` for each active forward; the visual duration is
stretched to at least 0.35 s.

## Producing a scene from the simulator

The core repository owns the exporter; the GUI never runs it. The exporter
converts Skyfield GCRS positions to ITRS (`frame_xyz(itrs)`), de-duplicates ISL
edges, keeps AP/life metadata and merges the optional task trace.

Terrestrial configurations:

```bash
cd SECMotionModel
python tools/export_gui_scene.py \
  --config data/configurations_AP_optimal.json \
  --out /tmp/secgui/terrestrial
```

A normal `main.py` run can export itself at the end:

```bash
python main.py --gui-export
# writes <result>/gui_scene/simulation.secsim
```

or an existing run can be exported without re-running it:

```bash
python3 tools/export_gui_scene.py \
  --run-dir result/.../r_algo_GREEDY \
  --out /tmp/secgui/run
```

`--run-dir` reads `gui_run.json` and, when present, `gui_state.csv`,
`gui_links.csv`, `tasks.jsonl` and `simulation_dataset.json`. This covers both
the extension runs and the standard `main.py` queue-monitoring dataset. Missing
sidecars simply produce a scene without that layer.

For a simulation that already writes GUI frames but uses a different internal
model, package the scene directory directly:

```bash
python3 tools/package_gui_scene.py \
  --scene /path/to/gui_scene \
  --out /path/to/simulation.secsim
```

The GUI contract is only the files listed in this document; unknown fields are
ignored and missing optional layers stay off, so new simulation variants do not
require GUI changes as long as they emit valid frames.

## Validation

- `secmotion-gui --validate-only --scene ...` checks a scene or `.secsim` file
  without a GPU.
- `python -m jsonschema -i examples/minimal/scene.json schema/scene-v1.schema.json`
  validates the manifest in CI; use `schema/simulation-v1.schema.json` for a
  `.secsim` descriptor.
- The GUI rejects non-finite numbers, duplicate satellite ids, dangling
  neighbor references and frames whose radius is implausible.
