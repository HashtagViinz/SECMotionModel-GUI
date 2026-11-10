---
name: GitHub Issues bridge
description: Use when communicating between SECMotionModel (core) and SECMotionModel-GUI through GitHub issues with the gh CLI: reading inboxes, creating linked issues, commenting with evidence, attaching screenshots and logs as links, labels, and closure workflow. Use before starting any cross-repo request, bug report or feature hand-off.
---

# GitHub Issues bridge

Agents working in the core repository and agents working in the GUI repository
communicate through GitHub issues. There is no shared chat channel: issues are
the source of truth and the history.

This file exists in **both** repositories and must stay identical:

- core: `.opencode/skills/gh-issues/SKILL.md`
- GUI:  `.opencode/skills/gh-issues/SKILL.md`

Change one, copy it to the other.

## Configuration

```bash
CORE_REPO="${CORE_REPO:-casalicchio/SECMotionModel}"
GUI_REPO="${GUI_REPO:-casalicchio/SECMotionModel-GUI}"
```

Requirements:

1. `gh auth status` must succeed. If it does not, stop and tell the user to run
   `gh auth login` or to export `GH_TOKEN`. Never write a token into a file.
2. Never print tokens, `GH_TOKEN`, `Authorization` headers or full `.netrc`.
3. Redact logs before attaching them.

## Inbox check (do this before starting cross-repo work)

```bash
# Issues that the other repository opened for me
gh issue list --repo "$GUI_REPO" --state open --label from-core \
  --json number,title,labels,updatedAt,url

gh issue list --repo "$CORE_REPO" --state open --label from-gui \
  --json number,title,labels,updatedAt,url

# Everything open, newest first
gh issue list --repo "$GUI_REPO" --state open --limit 50 \
  --json number,title,labels,updatedAt,url
```

## Read a thread

```bash
gh issue view 12 --repo "$GUI_REPO" --comments
```

## Create an issue for the other repository

Title convention:

- core → GUI: `[from-core] <summary>`
- GUI → core: `[from-gui] <summary>`

```bash
gh issue create --repo "$GUI_REPO" \
  --title "[from-core] Scene exporter emits non-ITRS positions" \
  --label from-core,bug,needs-gui \
  --body-file /tmp/issue.md
```

Body template:

```markdown
## Context
What I was doing, with commit/version/tag.

## Expected
What should happen.

## Actual
What happens instead, with exact command.

## Reproduction
```bash
# minimal commands
```

## Evidence
- commit: <permalink>
- screenshot: <public link>
- log: <link, sanitized>
- scene/script: <path or link>

## Requested action
What I need from the other repository.

## Cross-repo
Sibling issue: <full URL>
```

## Reply and update

```bash
gh issue comment 12 --repo "$GUI_REPO" --body "Fixed by <commit>. Screenshot: <link>"
gh issue edit 12 --repo "$GUI_REPO" --add-label in-progress
gh issue close 12 --repo "$GUI_REPO" --comment "Resolved by ..."
gh issue reopen 12 --repo "$GUI_REPO" --comment "Regression: ..."
```

Update the **same** issue when the situation changes. Do not open duplicates.

## Labels

Create them once per repository (idempotent):

```bash
for label in from-core from-gui needs-core needs-gui needs-info blocked \
             in-progress bug enhancement gui simulation \
             phase-0 phase-1 phase-2 phase-3 phase-4 phase-5 phase-6; do
  gh label create "$label" --repo "$GUI_REPO" --force >/dev/null 2>&1 || true
done
```

## Media evidence

Prefer **links**; never attach binaries to git history unless small.

### Public repositories: `agent-media` prerelease

```bash
gh release view agent-media --repo "$GUI_REPO" >/dev/null 2>&1 || \
  gh release create agent-media --repo "$GUI_REPO" --prerelease \
    --title "Agent media" --notes "Screenshots and logs attached to agent issues."

gh release upload agent-media shot.png --repo "$GUI_REPO" --clobber
# Public URL:
# https://github.com/<owner>/<repo>/releases/download/agent-media/shot.png
```

Use `scripts/gh_attach_media.sh` in the GUI repository when available.

### Private repositories: `agent-media` branch

```bash
git switch -c agent-media 2>/dev/null || git switch agent-media
mkdir -p issue-12 && cp shot.png issue-12/
git add issue-12/shot.png && git commit -m "media: issue 12 screenshot"
git push -u origin agent-media
# Link: https://github.com/<owner>/<repo>/blob/agent-media/issue-12/shot.png
```

### Text logs

```bash
gh gist create --public=false sanitized.log
```

### CI artifacts

```bash
gh run list --repo "$GUI_REPO" --limit 10
gh run download <run-id> --repo "$GUI_REPO" --name secmotion-gui-linux-x86_64
```

Artifact links require authentication: download and re-upload to `agent-media`
before linking from an issue.

## Etiquette

- One issue per topic. Cross-link related issues instead of merging them.
- Always include the exact command and the environment (OS, backend, commit).
- Attach evidence before asking someone else to reproduce.
- Do not close an issue opened by the other repository without a comment
  explaining the resolution.
- Poll inboxes at the start of a task, not in tight loops.

## Phase history

`scripts/create_phase_issues.sh` creates one issue per implementation phase
(0–6) and is idempotent. Run it once after `gh auth login`.
