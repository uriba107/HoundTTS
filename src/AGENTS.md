# src — HoundTTS C++ Source Code

## Purpose

Native C++17 DLL (`HoundTTS.dll`) that exposes TTS and translation functions to DCS World via Lua bridge (`luaopen_HoundTTS`). Synthesises speech from multiple providers and transmits it over SRS.

## Ownership

- `dllmain.cpp` — Lua module table, `require("HoundTTS")` entry point, `l_init` bootstraps config/logger
- `lua_tts.cpp/h` — Lua TTS bindings: `textToSpeech`, `startNoise`, `startTone`, `updateSession`, `killAllSessions`, `clearPCMCache`, `onMissionEnd`, `getCacheStats`
- `lua_translate.cpp/h` — Lua Translation bindings: `translateAsync`, `getTranslationResult`
- `lua_helpers.h` — Shared Lua stack helpers
- `tts_pipeline.cpp/h` — `TTSPipeline::Produce/ProduceNoise/ProduceTone`: dispatches to providers and pushes PCM to queue
- `config_reader.cpp/h` — `ConfigReader` singleton: reads `HoundTTS-credentials.ini`
- `provider.h` — `TtsProvider` / `TranslateProvider` enums and parsers (single source of truth)
- `backend.h` — `TTSRequest`, `TransmitParams`, `ITTSBackend` interface
- `audio_queue.h` — PCM accumulation buffer
- `session.h` — `Session` struct with position/alive tracking
- `speech_time.h` — Speech duration estimation
- `process_launcher.cpp/h` — Subprocess launcher (legacy piper.exe fallback)
- `utils.cpp/h` — Logging (`Logger`), HTTP helpers, `NormalizeProviderToken`
- `version.rc.in` — Windows version resource template (CMake `@ONLY`)

## Local Contracts

- All providers enumerated in `provider.h` — add new TTS/translation providers here first
- `ITTSBackend` interface in `backend.h` — backends receive 16kHz mono PCM + `TransmitParams`
- `TTSPipeline::Produce` must always call `queue->MarkDone()` before returning (even on error)
- Lua bindings in `lua_tts.cpp` parse the Lua table and call through to providers/backends
- PCM format: 16kHz mono 16-bit signed int (int16_t), pushed as `PCMChunk` structs
- C++17, static CRT (`/MT`), self-contained DLL with no VCRUNTIME dependency
- **No `ITTSProvider` interface.** Providers are static classes — each exposes `SynthesizeToQueue(...)` with a provider-specific signature. Dispatch is a `TtsProvider` enum (`src/provider.h`) + if/else chain in `tts_pipeline.cpp`.
- SapiTTS is **synchronous** — returns `std::vector<int16_t>` (inline, no thread). All other providers are **asynchronous** — accept `PCMQueue&` and run in a detached thread.
- Threads are **always detached**, never joined — fire-and-forget throughout all 20+ spawn sites.
- Session IDs are 22-char Base57 SRS GUIDs (`Utils::GenerateSRSGuid`, `src/utils.cpp`).
- PCM flow: Provider → `PaddedPCMQueue` → `CachingPCMQueue` → `PCMQueue` → SRS backend transmits.
- `CachingPCMQueue` (`pcm_cache.h`) tees PCM to both the downstream queue and an accumulator for cache insert on `Finalize()`.
- `PaddedPCMQueue` (`pcm_queue.h`) injects 200ms silence before/after speech (PTT lead-in/tail).
- Credentials INI is read once at `l_init()` — credentials never appear in Lua or DCS logs.

## Work Guidance

- Keep `provider.h` as the single source of truth for provider names/aliases
- Thread safety: TTS requests run on background threads; `ConfigReader` is read-only after init
- All credentials are read from INI by the DLL, never exposed in Lua or DCS logs
- Follow existing provider pattern when adding new TTS providers: enum + parser in `provider.h`, Synthesize static in `providers/tts/<name>/`, dispatch in `tts_pipeline.cpp`

## Verification

No test framework. Build with CMake + MSVC on Windows. Verify via `HoundTTS.TestTone()` in DCS mission script.

## Child DOX Index

- `providers/` — TTS and translation provider implementations
- `backends/` — Audio transmission backends (SRS, PCM cache/queue)
- `codecs/` — Audio codec implementations (Opus, MP3)
