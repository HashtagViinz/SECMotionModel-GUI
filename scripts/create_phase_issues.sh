#!/usr/bin/env bash
#
# Creates one GitHub issue per implementation phase (0-6) in the GUI
# repository. Idempotent: issues already present (matched by title) are
# skipped, labels are created/updated with --force.
#
# Usage:
#   GUI_REPO=owner/name scripts/create_phase_issues.sh
#
set -euo pipefail

REPO="${GUI_REPO:-HashtagViinz/SECMotionModel-GUI}"

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated (run 'gh auth login' or set GH_TOKEN)" >&2
  exit 1
fi

echo "phase issues -> $REPO"

for label in \
  phase-0 phase-1 phase-2 phase-3 phase-4 phase-5 phase-6 \
  from-core from-gui needs-core needs-gui needs-info blocked in-progress \
  bug enhancement gui simulation; do
  gh label create "$label" --repo "$REPO" --force >/dev/null 2>&1 || true
done

issue_exists() {
  local title="$1"
  local count
  count="$(gh issue list --repo "$REPO" --state all --search "$title in:title" --json number --jq 'length')"
  [ "$count" != "0" ]
}

create_issue() {
  local title="$1"
  local labels="$2"
  local body="$3"
  if issue_exists "$title"; then
    echo "skip (exists): $title"
    return
  fi
  gh issue create --repo "$REPO" --title "$title" --label "$labels" --body "$body"
}

create_issue \
  "[Fase 0] Bootstrap repo, submodule GUI-integration e skill gh-issues" \
  "phase-0,enhancement" \
  $'## Obiettivo\n\nCreare la repository `SECMotionModel-GUI`, collegarla al core come submodule sul branch `GUI-integration` e predisporre le skill di comunicazione tra agenti.\n\n## Deliverable\n\n- repository pubblica e branch `main`;\n- submodule `gui/` nel branch `GUI-integration` della repo core;\n- licenza GPL-3.0-or-later e `THIRD_PARTY.md`;\n- skill `.opencode/skills/gh-issues/SKILL.md` presente in **entrambe** le repo;\n- `scripts/create_phase_issues.sh` eseguibile;\n- label GitHub per le fasi.\n\n## Gate\n\n`git submodule status` pulito; skill presente in entrambe le repo; issue di fase create.'

create_issue \
  "[Fase 1] Build spike: WebGPU statico + screenshot headless + Wine/AppImage" \
  "phase-1,enhancement" \
  $'## Obiettivo\n\nProvare che la catena di build e il runtime WebGPU funzionano su tutte le piattaforme prima di costruire la GUI completa.\n\n## Deliverable\n\n- CMake con WebGPU-Cpp + WebGPU-distribution pinnati e wgpu-native `v24.0.3.1` statico;\n- app headless che renderizza offscreen, legge il frame e scrive PNG;\n- protocollo JSONL minimo (`open`, `seek`, `screenshot`, `state`, `quit`);\n- `Dockerfile.windows` con cross-compilazione MinGW e verifica degli import statici;\n- smoke test Wine + lavapipe (`--backend vulkan`);\n- AppImage skeleton per Linux.\n\n## Gate\n\n- `.exe` statico senza `wgpu_native.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`;\n- PNG valido generato da Wine headless;\n- AppImage eseguibile con `--headless`;\n- nessun blocco al primo `workflow_dispatch`.'

create_issue \
  "[Fase 2] Contratto dati: schema v1, exporter Python e trace task" \
  "phase-2,enhancement" \
  $'## Obiettivo\n\nDefinire il formato scena e produrlo dal core Python, compreso il movimento dei task tra satelliti.\n\n## Deliverable\n\n- `schema/scene-v1.schema.json`, `tasks-v1.schema.json`, `command-v1.schema.json`;\n- `tools/export_gui_scene.py` nel core: frame ITRS/ECEF, link ISL, AP, life, vessel e observer;\n- `--trace-jsonl PATH` in `naval_main.py` con eventi `spawn`, `hop_start`, `hop_end`, `compute`, `deliver`, `fail`;\n- exporter che unisce config + trace in `scene/manifest.json`, `frames/*.json`, `tasks.jsonl`;\n- esempi minimali e test dello schema.\n\n## Gate\n\nExport reale dalle configurazioni navali valido contro lo schema; task animabili e riferimenti link coerenti.'

create_issue \
  "[Fase 3] Renderer MVP: globo, satelliti, ISL, cupola, camera e HUD" \
  "phase-3,enhancement" \
  $'## Obiettivo\n\nRendering real-time della scena navale con la stessa lettura visiva delle GIF esistenti.\n\n## Deliverable\n\n- globo terrestre texturizzato e atmosfera;\n- satelliti colorati per stato (AP, life, buffer, corridoio);\n- link ISL con LOD;\n- cupola di visibilità e buffer zone;\n- rotta great-circle vessel-observer;\n- camera orbit / top-down vessel / satellite chase / free;\n- HUD con scenario, tempo, FPS, telemetria e metriche.\n\n## Gate\n\nScreenshot headless Linux e Windows; vista default paragonabile alle GIF; 457 satelliti senza cali evidenti.'

create_issue \
  "[Fase 4] GUI completa: timeline, layer, task animati, command bus e recorder" \
  "phase-4,enhancement" \
  $'## Obiettivo\n\nCompletare interazione e equivalenza GUI/JSONL.\n\n## Deliverable\n\n- timeline con play/pausa, seek, step e speed;\n- pannello layer con shortcut;\n- selezione entità e pannello telemetria;\n- animazione task (spawn, hop, compute, complete/fail) con durata visiva minima;\n- `CommandBus` unico per widget GUI e JSONL;\n- `--record-script` e replay headless con round-trip screenshot;\n- command palette JSON (`Ctrl+K`).\n\n## Gate\n\nOgni comando JSONL ha un controllo GUI documentato; replay GUI→JSONL→screenshot coerente.'

create_issue \
  "[Fase 5] Packaging e CI: .exe e .AppImage con 2 job" \
  "phase-5,enhancement" \
  $'## Obiettivo\n\nProdurre gli artefatti distribuibili e il workflow a due job (Windows e Linux; macOS fuori scope).\n\n## Deliverable\n\n- `Dockerfile.windows` finale con stage build/test/export;\n- `scripts/package_appimage.sh`;\n- workflow GitHub Actions con job `windows` e `linux` e artifact `SECMotionModel-GUI-*`;\n- `SHA256SUMS` per ogni artefatto;\n- trigger `workflow_dispatch` iniziale, tag `v*` dopo il preflight.\n\n## Gate\n\nWorkflow verde; `.exe` e `.AppImage` scaricati e smoke-testati; import statici verificati.'

create_issue \
  "[Fase 6] Documentazione, skill e release" \
  "phase-6,enhancement" \
  $'## Obiettivo\n\nRendere la GUI utilizzabile da utenti e agenti e chiudere la prima release.\n\n## Deliverable\n\n- `examples/README.example.md` completo e testato;\n- `docs/commands.md`, `docs/scene-format.md`, `docs/testing.md`;\n- skill `windows-testing` e `gui-automation`;\n- `gh-issues` allineata tra le due repo;\n- sezione GUI nel README del core;\n- release `v0.1.0` con note e checksum.\n\n## Gate\n\nTutti i criteri di accettazione del piano soddisfatti; issue di fase chiuse con link alle evidenze.'

echo "done."
