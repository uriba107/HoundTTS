# HoundTTS — Agent Guide

## Project

Native C++ DLL for DCS World that replaces DCS-SimpleTextToSpeech. Exposes TTS, noise/tone generation, and translation to Lua mission scripts. Transmits directly into SRS (SimpleRadioStandalone) over TCP/UDP.

## Coding guidelines

Follow [`PONYTAIL.md`](PONYTAIL.md) — lazy senior dev mode. Read before writing code.

## Build (Windows-only, MSVC)

```powershell
.\build-docker.ps1                        # MSVC via Docker Windows containers (default)
.\build-docker.ps1 -NoCache               # force full rebuild
```

- Build requires Docker Desktop in **Windows containers** mode, not Linux containers.
- `.\build-docker.ps1` defaults to `-Windows $true` (MSVC/Dockerfile.windows).
- Dependency versions pinned in `deps.env` — bump there, rebuild.
- SAPI provider **requires** MSVC build; MinGW returns silence.
- Output: 4 zip packages in `release/` (base, piper-engine, piper-voices, supertonic-engine).
- Runtime: Windows 10 1903+ / Windows 11 / Windows Server 2019/2022 only.
- Build log: `build.log`; test/debug log: `HoundTTS.log` in DCS Saved Games folder.

## CI

`.github/workflows/build-windows.yml` — GitHub Actions `windows-2022` runner:

- Builds via Docker (Windows containers, Hyper-V isolation).
- Tags push (`v*`) create a draft GitHub Release with all 4 zip assets.
- DEPS layer cached on `ghcr.io` per-branch.

## Architecture

| Layer                 | Path                                | Role                                                                          |
| --------------------- | ----------------------------------- | ----------------------------------------------------------------------------- |
| DLL entry             | `src/dllmain.cpp`                   | Registers Lua C functions via `luaopen_HoundTTS` (line 48)                    |
| Lua bindings          | `src/lua_tts.cpp`                   | `textToSpeech`, `startNoise`, `startTone`, `updateSession`, etc.              |
| Lua bindings          | `src/lua_translate.cpp`             | `translateAsync`, `getTranslationResult`                                      |
| Pipeline              | `src/tts_pipeline.cpp`              | TTS orchestration: optional translate → synthesize PCM → backend transmits    |
| Backend               | `src/backends/srs/`                 | SRS protocol (TCP handshake + UDP audio)                                      |
| PCM queue             | `src/backends/pcm_queue.h`          | Thread-safe 16kHz mono int16 blocking queue (producer→consumer)               |
| PCM cache             | `src/backends/pcm_cache.cpp`        | LRU + TTL PCM cache (16kHz mono, keyed by FNV-1a of TTS params)              |
| TTS providers         | `src/providers/tts/<name>/`         | piper, supertonic, sapi, azure, google, elevenlabs, aws, openai, edge |
| Translation providers | `src/providers/translate/<name>/`   | openai, google, libretranslate, aws, azure                                    |
| Config                | `src/config_reader.cpp`             | Reads `HoundTTS-credentials.ini` from `Config/`                               |

Entrypoint for Lua: `luaopen_HoundTTS` (`dllmain.cpp:48`) — called when DCS `require("HoundTTS")` runs the DLL.

## Code conventions

- C++17, static CRT (`/MT`), self-contained DLL with no VCRUNTIME dependency.
- **No `ITTSProvider` interface.** Providers are static classes — each exposes `SynthesizeToQueue(...)` with a provider-specific signature. Dispatch is a `TtsProvider` enum (`src/provider.h`) + if/else chain in `tts_pipeline.cpp:175-376`.
- SapiTTS is **synchronous** — returns `std::vector<int16_t>` (inline, no thread). All other providers are **asynchronous** — accept `PCMQueue&` and run in a detached thread.
- Backends implement `ITTSBackend` interface (`src/backend.h`).
- Threads are **always detached**, never joined — fire-and-forget throughout all 20+ spawn sites.
- Session IDs are 22-char Base57 SRS GUIDs (`Utils::GenerateSRSGuid`, `src/utils.cpp:14`).
- PCM flows: Provider → `PaddedPCMQueue` → `CachingPCMQueue` → `PCMQueue` → SRS backend transmits.
- `CachingPCMQueue` (`pcm_cache.h:130`) tees PCM to both the downstream queue and an accumulator for cache insert on `Finalize()`.
- `PaddedPCMQueue` (`pcm_queue.h:88`) injects 200ms silence before/after speech (PTT lead-in/tail).
- All providers produce 16kHz mono 16-bit PCM; Opus-encoded by SRS backend before UDP send.
- Credentials INI is read once at `l_init()` — credentials never appear in Lua or DCS logs.
- Config example files (`Config/HoundTTS.lua.example`, `Config/HoundTTS-credentials.ini.example`): users copy without `.example` suffix. **Never overwrite** live configs during updates.

## Provider quirks

| Lua alias              | Routes to              | Notes                                                                 |
| ---------------------- | ---------------------- | --------------------------------------------------------------------- |
| `"sapi"` / `"win"`     | `SapiTTS`              | Windows SAPI 5.4, MSVC-only, synchronous                              |
| `"piper"`              | `PiperTTS`             | Bundled GPLv3 DLL, lazy-init, voice registry with fallback             |
| `"supertonic"`         | `SupertonicTTS`        | Bundled ONNX DLL, ISO 639-1 culture (region stripped)                 |
| `"edge"` / `"edgetts"` | `EdgeTTS`              | Free — reverse-engineered Bing WebSocket (unofficial, may break)       |
| `"openai"`             | `OpenAITTS`            | `/v1/audio/speech` always appended to endpoint                          |
| `"google"` / `"gcloud"`| `GoogleTTS`            | Requires service-account JSON                                          |
| `"aws"` / `"polly"`    | `AwsTTS`               | Amazon Polly, also handles `"amazon"` alias                            |
| `"azure"`              | `AzureTTS`             | Azure Cognitive Services Speech                                        |
| `"elevenlabs"`         | `ElevenLabsTTS`        | Free tier: max 1 concurrent WebSocket                                  |

- Edge TTS: voice defaults derived from culture+gender (`{culture}-AriaNeural` / `{culture}-GuyNeural`).

## Version scheme

`x.y.z.BUILD` where BUILD = last digit of year + zero-padded day-of-year (e.g., `0.2.0.6085` = June 8, 2026). Set via `-DHOUNDTTS_VERSION_OVERRIDE=x.y.z` in CMake (CI injects from git tag `v*`).

## Testing

- **No test framework** — only manual/integration testing through DCS.
- Test by building, loading DLL into DCS Saved Games, and running `HoundTTS.TestTone()` from a mission trigger.

## Deployment

Install by extracting zip packages into `%USERPROFILE%\Saved Games\DCS\`. Two load paths:

1. **Auto** (DCSServerBot/DCS-gRPC desanitized env) — hook script `Scripts/Hooks/HoundTTS-hook.lua` loads automatically.
2. **Manual** (vanilla DCS) — add `dofile(lfs.writedir()..[[Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua]])` to `MissionScripting.lua` **before** the `sanitizeModule` block.
