# src/providers — TTS & Translation Providers

## Purpose

All TTS synthesis and text translation implementations. Each provider lives in its own subdirectory under `tts/` or `translate/`. Shared auth helpers in `shared/`. Audio generators in `generators/`.

## Ownership

### TTS Providers (`tts/`)

See `tts/AGENTS.md` for detailed ownership and per-provider specifics.

`tts/` contains: sapi, piper, azure, google, elevenlabs, aws, openai, supertonic, edge.

### Translation Providers (`translate/`)

See `translate/AGENTS.md` for detailed ownership and per-provider specifics.

`translate/` contains: openai, google, libretranslate, aws, azure.

### Shared Auth (`shared/`)

See `shared/AGENTS.md` for details.

`shared/` contains: google, aws, azure.

### Generators (`generators/`)

- **tone.*** — Sine-wave tone PCM generation
- **noise.*** — White/pink/chirp/harsh/jam noise PCM generation

## Local Contracts

- Each TTS provider exposes `static bool SynthesizeToQueue(...)` signature from `tts_pipeline.cpp`
- PCM output: 16kHz mono 16-bit signed int, pushed as PCMChunks to a shared pointer queue
- All cloud providers use `httplib.h` for HTTP/WS (vendored in `tools/httplib.h`)
- Translation providers expose a consistent async pattern with a polling callback
- Providers read config via `ConfigReader` singleton — no direct file access

## Work Guidance

- Keep provider implementation self-contained; shared logic goes in `shared/`
- Cloud providers must handle network errors gracefully and return false on failure
- Providers must not leak API keys in logs or error messages

## Verification

No test framework. Each provider verified manually from DCS via `HoundTTS.Transmit()`.

## Child DOX Index

- `tts/` — TTS synthesis providers
- `translate/` — Text translation providers
- `shared/` — Shared auth helpers (google, aws, azure)
