# src/providers/tts — TTS Synthesis Providers

## Purpose

All TTS synthesis implementations. Each provider is a flat directory with 1–4 files exposing a static `SynthesizeToQueue(...)`.

## Ownership

| Provider | Type | Notes |
|----------|------|-------|
| sapi/ | Windows SAPI 5.4 (`ISpVoice`), offline, synchronous | MSVC-only |
| piper/ | In-process `piper.dll` (ONNX), offline | Voice pool + registry, GPLv3 |
| supertonic/ | In-process `supertonic.dll` (ONNX), offline | Multilingual, bundled styles |
| edge/ | Microsoft Edge Read Aloud WebSocket | Free cloud, reverse-engineered, no API key |
| azure/ | Azure Cognitive Services Speech REST | Requires key + region |
| google/ | Google Cloud TTS REST | Requires service-account JSON |
| elevenlabs/ | ElevenLabs WebSocket | Free tier: 1 concurrent WS |
| aws/ | Amazon Polly REST | Also handles `"amazon"` alias |
| openai/ | OpenAI / LocalAI / Kitten REST | `/v1/audio/speech` auto-appended |

## Local Contracts

- Signature: `static bool SynthesizeToQueue(text, ..., PCMQueue&)` — returns false on failure, always calls `queue.MarkDone()`
- PCM: 16kHz mono 16-bit signed int (`int16_t`), pushed as `PCMChunk` structs
- Async (detached thread) except SapiTTS which is inline + returns `std::vector<int16_t>`
- Config via `ConfigReader` singleton — no direct file IO
- Guard members with `std::lock_guard<std::mutex>` in every getter

## Provider Lua aliases

| Lua alias | Routes to | Notes |
|-----------|-----------|-------|
| `"sapi"` / `"win"` | `SapiTTS` | Windows SAPI 5.4, MSVC-only, synchronous |
| `"piper"` | `PiperTTS` | Bundled GPLv3 DLL, lazy-init, voice registry with fallback |
| `"supertonic"` | `SupertonicTTS` | Bundled ONNX DLL, ISO 639-1 culture (region stripped) |
| `"edge"` / `"edgetts"` | `EdgeTTS` | Free — reverse-engineered Bing WebSocket (unofficial, may break) |
| `"openai"` | `OpenAITTS` | `/v1/audio/speech` always appended to endpoint |
| `"google"` / `"gcloud"` | `GoogleTTS` | Requires service-account JSON |
| `"aws"` / `"polly"` | `AwsTTS` | Amazon Polly, also handles `"amazon"` alias |
| `"azure"` | `AzureTTS` | Azure Cognitive Services Speech |
| `"elevenlabs"` | `ElevenLabsTTS` | Free tier: max 1 concurrent WebSocket |

- Edge TTS: voice defaults derived from culture+gender (`{culture}-AriaNeural` / `{culture}-GuyNeural`).

## Work Guidance

### Adding a new TTS provider

**Checklist** (5 min, 8 files to touch):

1. `src/provider.h` — add `Foo` to `TtsProvider` enum, add parse + name
2. `src/providers/tts/foo/foo_tts.h + .cpp` — implement `SynthesizeToQueue`
3. `src/config_reader.h + .cpp` — add getter + member + INI parse (if credentials needed)
4. `src/tts_pipeline.cpp` — add `#include` + `else if` dispatch branch
5. `CMakeLists.txt` — add source files
6. `README.md` — add provider section + update routing table
7. `dcs/Config/HoundTTS-credentials.ini.example` — add INI section (if credentials needed)
8. `this file` — add Ownership row + Lua alias row

See `ADDING_A_PROVIDER.md` for detailed instructions with code skeletons.

## Verification

No test framework. Build via CMake + MSVC on Windows. Test from DCS via `HoundTTS.Transmit()`.

## Child DOX Index

- `ADDING_A_PROVIDER.md` — step-by-step guide with code skeletons
