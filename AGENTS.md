# DOX framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why

## Project

Native C++17 DLL for DCS World replacing DCS-SimpleTextToSpeech. Exposes TTS, noise/tone generation, and translation to Lua mission scripts over SRS. Windows-only (10 1903+ / 11 / Server 2019/2022).

## Build (Windows-only, MSVC via Docker)

```powershell
.\build-docker.ps1                        # MSVC via Docker Windows containers (default)
.\build-docker.ps1 -NoCache               # force full rebuild
```

- Docker Desktop in **Windows containers** mode (not Linux).
- Dependency versions pinned in `deps.env` — bump there, rebuild.
- SAPI requires MSVC build; MinGW returns silence.
- Output: 4 zip packages in `release/` (base, piper-engine, piper-voices, supertonic-engine).
- Build log: `build.log`; runtime log: `HoundTTS.log` in Saved Games.

## CI

`.github/workflows/build-windows.yml` — GitHub Actions `windows-2022` runner:

- Builds via Docker (Windows containers, Hyper-V isolation).
- Tags push (`v*`) create a draft GitHub Release with all 4 zip assets.
- DEPS layer cached on `ghcr.io` per-branch.

## Architecture

| Layer                 | Path                                | Role                                                                          |
| --------------------- | ----------------------------------- | ----------------------------------------------------------------------------- |
| DLL entry             | `src/dllmain.cpp`                   | Registers Lua C functions via `luaopen_HoundTTS`                              |
| Lua bindings          | `src/lua_tts.cpp`                   | `textToSpeech`, `startNoise`, `startTone`, `updateSession`, etc.              |
| Lua bindings          | `src/lua_translate.cpp`             | `translateAsync`, `getTranslationResult`                                      |
| Pipeline              | `src/tts_pipeline.cpp`              | TTS orchestration: optional translate → synthesize PCM → backend transmits    |
| Backend               | `src/backends/srs/`                 | SRS protocol (TCP handshake + UDP audio)                                      |
| PCM queue             | `src/backends/pcm_queue.h`          | Thread-safe 16kHz mono int16 blocking queue (producer→consumer)               |
| PCM cache             | `src/backends/pcm_cache.cpp`        | LRU + TTL PCM cache (16kHz mono, keyed by FNV-1a of TTS params)              |
| TTS providers         | `src/providers/tts/<name>/`         | piper, supertonic, sapi, azure, google, elevenlabs, aws, openai, edge, kitten |
| Translation providers | `src/providers/translate/<name>/`   | openai, google, libretranslate, aws, azure                                    |
| Config                | `src/config_reader.cpp`             | Reads `HoundTTS-credentials.ini` from `Config/`                               |
| Lua API (mission)     | `dcs/Mods/Services/HoundTTS/Scripts/HoundTTS-mission.lua` | User-facing `Transmit`, `TextToSpeech`, `Translate`, etc.     |

## Version scheme

`x.y.z.BUILD` where BUILD = last digit of year + zero-padded day-of-year (e.g., `0.2.0.6159` = June 8, 2026). Set via `-DHOUNDTTS_VERSION_OVERRIDE=x.y.z` in CMake (CI injects from git tag `v*`).

## Deployment

Two load paths into DCS:

1. **Auto** (desanitized env — DCSServerBot/DCS-gRPC) — hook script `Scripts/Hooks/HoundTTS-hook.lua` loads automatically.
2. **Manual** (vanilla DCS) — add `dofile(lfs.writedir()..[[Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua]])` to `MissionScripting.lua` **before** the `sanitizeModule` block.

## User Preferences

When the user requests a durable behavior change, record it here or in the relevant child AGENTS.md

## Child DOX Index

| Path | Scope |
|------|-------|
| `src/` | C++ source code — DLL core, Lua bindings, pipeline, config, backends, codecs |
| `src/providers/` | All TTS and translation provider implementations (tts/, translate/, shared/, generators/) |
| `dcs/` | DCS World deployment — Lua scripts, config templates, hooks |
| `tools/` | Build scripts, dependency patches, vendored httplib.h, supertonic build |