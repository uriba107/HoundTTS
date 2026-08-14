# HoundTTS

A native C++ DLL replacement for [DCS-SimpleTextToSpeech](https://github.com/ciribob/DCS-SimpleTextToSpeech) that integrates with DCS World as a Lua extension.

## Benefits over the Lua script

- **No PowerShell overhead** — connects natively to SRS via direct TCP/UDP protocol
- **No focus stealing** — all TTS synthesis runs in background threads, no visible windows
- **Parallel calls** — each TTS request is fire-and-forget, no blocking
- **Multiple TTS providers** — Piper (offline, bundled), Supertonic (offline, multilingual ONNX), SAPI (Windows system voices, no API key), Edge (free cloud neural TTS, no API key), Azure, Google, ElevenLabs, OpenAI (and compatible APIs like LocalAI, Kitten TTS Server)
- **Translation** — translate text via OpenAI chat models with military aviation context awareness (preserves brevity codes like FOX 3, BULLSEYE, etc.)
- **Credentials stay out of Lua** — API keys are read directly by the DLL from an INI file, never exposed in mission scripts or DCS logs
- **Auto-detects SRS path** from the Windows registry — no manual path configuration needed

## Requirements

### Runtime (machine running DCS)

| Requirement                                                                     | Notes                                                                                                                                   |
| ------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| **Windows 10 (1903+), Windows 11, Windows Server 2019, or Windows Server 2022** | Minimum OS enforced by the Win10 SDK 19041 PE header stamp                                                                              |
| **SimpleRadioStandalone (SRS)**                                                 | Required for all transmission. See [SRS requirements](https://github.com/ciribob/DCS-SimpleRadioStandalone) for its own .NET dependency |
| **.NET**                                                                        | Not required by HoundTTS.dll itself — pure native C++                                                                                   |

## Installation

### 1. Copy files

Download the latest release from [GitHub releases](https://github.com/uriba107/HoundTTS/releases) and extract it into your DCS Saved Games folder (e.g. `%USERPROFILE%\Saved Games\DCS\`).

Each release includes four archives:

- **`HoundTTS-windows.zip`** — the base package. Contains the DLL, all Lua scripts, and config examples. **This is all you need to get started** (with cloud or SAPI providers).
- **`HoundTTS-piper-engine-windows.zip`** — the Piper TTS engine (~16 MB). Contains `piper.dll` (built from [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl), GPLv3), `onnxruntime.dll`, and `espeak-ng-data/`. Install this in addition to the base package if you want to use Piper TTS. The DLL keeps voice models loaded between calls, eliminating per-call cold-start latency. Only needs re-downloading when piper or onnxruntime is updated.
- **`HoundTTS-piper-voices-windows.zip`** — two bundled English voice models (~120 MB). Download once, keep across updates. You can also skip this and download your own voices from [HuggingFace](https://huggingface.co/rhasspy/piper-voices) instead.
- **`HoundTTS-supertonic-engine-windows.zip`** — the Supertonic TTS engine. Contains `supertonic.dll`, `onnxruntime.dll`, and bundled ONNX models with voice styles. Install this in addition to the base package if you want to use Supertonic TTS.

> **Config files:** copy each `.example` file to the same name without `.example` and edit it. These files are never overwritten by updates — your live settings are safe.

```
Saved Games\DCS\
├── Config\
│   ├── HoundTTS.lua.example          ← copy to HoundTTS.lua and edit
│   └── HoundTTS-credentials.ini.example  ← copy to HoundTTS-credentials.ini and fill in
├── Mods\Services\HoundTTS\
│   ├── bin\
│   │   ├── HoundTTS.dll
│   │   ├── piper\                    ← from piper-engine
│   │   │   ├── piper.dll             ← in-process TTS engine (GPLv3)
│   │   │   ├── onnxruntime.dll
│   │   │   ├── espeak-ng-data\
│   │   │   └── COPYING               ← GPLv3 license for piper.dll
│   │   └── supertonic\               ← from supertonic-engine
│   │       ├── supertonic.dll        ← in-process TTS engine
│   │       ├── onnxruntime.dll
│   │       ├── models\
│   │       │   ├── duration_predictor.onnx
│   │       │   ├── text_encoder.onnx
│   │       │   ├── tts.json
│   │       │   ├── unicode_indexer.json
│   │       │   ├── vector_estimator.onnx
│   │       │   └── vocoder.onnx
│   │       └── voice_styles\
│   │           ├── F1.json
│   │           ├── F2.json
│   │           ├── F3.json
│   │           ├── F4.json
│   │           ├── F5.json
│   │           ├── M1.json
│   │           ├── M2.json
│   │           ├── M3.json
│   │           ├── M4.json
│   │           └── M5.json
│   ├── voices\                       ← from piper-voices (or bring your own)
│   │   ├── en_US-lessac-low.onnx
│   │   ├── en_US-lessac-low.onnx.json
│   │   ├── en_US-ryan-low.onnx
│   │   └── en_US-ryan-low.onnx.json
│   └── Scripts\
│       ├── HoundTTS.lua
│       └── HoundTTS-mission.lua
└── Scripts\Hooks\
    └── HoundTTS-hook.lua
```

### 2. Prepare DCS

**HoundTTS detects the environment automatically** and picks the right load path:

#### Option A — Desanitized server (recommended, zero extra steps)

If your server runs **DCSServerBot**, **DCS-gRPC**, or anything else that desanitizes `MissionScripting.lua` (i.e. `require`, `lfs`, `io`, `package` are available in the mission env), HoundTTS loads itself automatically on every mission start via its hook script. Nothing else to do.

> DCSServerBot desanitizes the env by default. If you have it installed and its `desanitize` option is not set to `false`, you are already covered.

#### Option B — Vanilla DCS (sanitized, manual step required)

On a stock DCS install with no desanitization, add one line to `MissionScripting.lua` (in the DCS World install folder at `Scripts\MissionScripting.lua`):

```diff
  --Initialization script for the Mission lua Environment (SSE)

  dofile('Scripts/ScriptingSystem.lua')
+ dofile(lfs.writedir()..[[Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua]])

  --Sanitize Mission Scripting environment
```

The line **must** appear before the `sanitizeModule` block so that `require`, `package`, and `lfs` are still available when the script runs.

> If neither path is active, HoundTTS logs a warning in `dcs.log` with instructions.

## TTS Providers

All providers are selected per-call via the `provider` field in `HoundTTS.Transmit`. The default is controlled by `HoundTTS.DEFAULT_PROVIDER` (default: `"piper"`).

### SAPI (Windows system voices)

Uses the Windows Speech API 5.4 (`ISpVoice`) — the same engine used by Narrator and other Windows TTS. **No internet or API key required.** Uses whatever voices are installed on the Windows machine running DCS.

> **Aliases:** `"sapi"` and `"win"` are interchangeable.

> **Requires the MSVC build** (`.\build-docker.ps1`). The MinGW build returns silence for SAPI.

```lua
-- Use the default system voice
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "sapi" }
)

-- Select voice by name (case-insensitive substring match)
HoundTTS.Transmit("Wind 270 at 10",
    { freqs = "127.5", coalition = 2 },
    { provider = "sapi", voice = "David" }
)

-- Select by gender and culture
HoundTTS.Transmit("Bogey, 2 o'clock",
    { freqs = "251.0", coalition = 2 },
    { provider = "sapi", gender = "female", culture = "en-GB" }
)
```

Voice selection priority: `voice` name match → `culture` + `gender` attribute query → system default.

Supported `culture` values: `en-US`, `en-GB`, `en-AU`, `en-CA`, `fr-FR`, `de-DE`, `es-ES`, `it-IT`, `ru-RU`, `zh-CN`, `ja-JP`.  
Additional voices can be installed via **Windows Settings → Time & Language → Speech → Add voices**.

### Piper (offline, bundled)

**No internet or API key required.** Synthesizes speech in-process via `piper.dll` (built from [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl), GPLv3). Voice models stay loaded between calls — no per-call cold-start. The engine is included in `HoundTTS-piper-engine-windows.zip` and two bundled voice models in `HoundTTS-piper-voices-windows.zip`.

```lua
HoundTTS.Transmit("Bogey, bullseye 270 for 15",
    { freqs = "251.0", coalition = 2, name = "GCI" },
    { provider = "piper", voice = "en_US-lessac-low" }
)
```

**Bundled voices:**

| Model                   | Gender | Sample rate |
| ----------------------- | ------ | ----------- |
| `en_US-lessac-low.onnx` | Male   | 16 kHz      |
| `en_US-ryan-low.onnx`   | Male   | 16 kHz      |

Browse all available voices at **[rhasspy.github.io/piper-samples](https://rhasspy.github.io/piper-samples/)**.  
Download additional models from [HuggingFace rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) and place them in the `voices\` folder (or set `voice_path` in the credentials INI).

### Supertonic (offline, multilingual ONNX)

**No internet or API key required.** Synthesizes speech in-process via `supertonic.dll` (built from [supertone-inc/supertonic](https://github.com/supertone-inc/supertonic)). Supports **English, Korean, German, Japanese, and more** with high-quality neural voices. Voice styles stay loaded between calls — no per-call cold-start. The engine is included in `HoundTTS-supertonic-engine-windows.zip` with bundled ONNX models and voice styles.

```lua
HoundTTS.Transmit("Bogey, bullseye 270 for 15",
    { freqs = "251.0", coalition = 2, name = "GCI" },
    { provider = "supertonic", voice = "M1", culture = "en", speed = 1.05 }
)
```

**Bundled voice styles:**

| Style | Gender |
| ----- | ------ |
| `M1`  | Male   |
| `M2`  | Male   |
| `M3`  | Male   |
| `M4`  | Male   |
| `M5`  | Male   |
| `F1`  | Female |
| `F2`  | Female |
| `F3`  | Female |
| `F4`  | Female |
| `F5`  | Female |

Create custom voice styles at **[supertonic.supertone.ai/voice-builder](https://supertonic.supertone.ai/voice-builder)** and place the JSON files in the `bin\supertonic\voice_styles\` folder (or set `voice_style_path` in the credentials INI).

Supported languages: English (`en`), Korean (`ko`), German (`de`), Japanese (`ja`), and more. Set the `culture` parameter to the ISO 639-1 language code.

### Edge TTS (free cloud, no API key)

**No API key, no account, no configuration required.** Uses Microsoft Edge's built-in Read Aloud TTS service — the same high-quality neural voices as Azure Cognitive Services, but free and without any signup.

> **⚠️ Unofficial:** This uses a reverse-engineered Microsoft endpoint. It may stop working if Microsoft changes the API. If it breaks, switch to another provider. No SLA or guarantees.
> **Aliases:** `"edge"` and `"edgetts"` are interchangeable.

```lua
HoundTTS.Transmit("Bogey, bullseye 270 for 15, angels 25",
    { freqs = "251.0", coalition = 2, name = "GCI" },
    { provider = "edge", voice = "en-US-AriaNeural", speed = 1.0 }
)
```

Voice defaults are chosen from `culture` + `gender` if `voice` is not set:

- **female** → `{culture}-AriaNeural` (e.g. `en-US-AriaNeural`)
- **male** → `{culture}-GuyNeural` (e.g. `en-US-GuyNeural`)

Browse all **322 available voices** (142 locales) in [docs/edge-tts-voices.md](docs/edge-tts-voices.md).

### Azure Cognitive Services

Requires an Azure Speech subscription key and region set in `HoundTTS-credentials.ini`.

```lua
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "azure", voice = "en-US-JennyNeural", culture = "en-US" }
)
```

### Google Cloud TTS

Requires a **Google Cloud service-account JSON** file. Set the path in `HoundTTS-credentials.ini`.

> **Aliases:** `"google"` and `"gcloud"` are interchangeable.

To create one:

1. Google Cloud Console → **IAM & Admin → Service Accounts → Create Service Account**
2. Grant the role **Cloud Text-to-Speech API User**
3. **Keys → Add Key → JSON** — save the downloaded file
4. Set `credentials_file` in `HoundTTS-credentials.ini` to the full path

```lua
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "google", voice = "en-US-Standard-C", culture = "en-US", gender = "female" }
)
```

### Amazon Polly (AWS)

Requires AWS access key, secret key, and region set in `HoundTTS-credentials.ini` under `[AWS]`.

> **Aliases:** `"aws"` and `"polly"` are interchangeable.

```lua
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "aws", voice = "Joanna", culture = "en-US", engine = "neural" }
)
```

### Kitten TTS (self-hosted) ⚠️ Deprecated

> **Deprecated:** the dedicated `"kitten"` provider will be removed in the next release. It currently still works — calls are silently rerouted through the OpenAI-compatible endpoint that [Kitten TTS Server](https://github.com/uriba107/Kitten-TTS-Server) exposes (`/v1/audio/speech`). **Migrate now** — see instructions below.

**Migration to `provider = "openai"`:**

1. In `HoundTTS-credentials.ini`, set the `[OpenAI]` section to point at your Kitten server:

```ini
[OpenAI]
endpoint = http://192.168.10.30:8005   ; your Kitten TTS Server URL
model    = kitten-tts
```

2. In your Lua scripts, change `provider = "kitten"` → `provider = "openai"`:

```lua
-- Before (deprecated)
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "kitten", voice = "Bella" }
)

-- After
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "openai", voice = "Bella" }
)
```

**Built-in voices:** `Bella`, `Luna`, `Rosie`, `Kiki` (female) · `Jasper`, `Bruno`, `Hugo`, `Leo` (male)

> **Speed note:** the deprecated provider defaulted to `1.1` speed. Set `speed = 1.1` explicitly in `provider_params` if you relied on this.

### OpenAI (and compatible APIs)

Supports the [OpenAI TTS API](https://platform.openai.com/docs/guides/text-to-speech) and any OpenAI-compatible endpoint such as [LocalAI](https://localai.io/features/text-to-audio/) or [Kitten TTS Server](https://github.com/uriba107/Kitten-TTS-Server). Set the endpoint URL (and optionally an API key) in `HoundTTS-credentials.ini`.

- `/v1/audio/speech` is always appended to the endpoint URL.
- Any path in the URL is kept as a prefix — useful for reverse proxies (e.g. `https://front.example.com/localai` → `/localai/v1/audio/speech`).
- Requests WAV output for maximum compatibility across backends; auto-detects Ogg/Opus if the server returns it.
- API key is optional (not needed for local deployments).

```lua
-- OpenAI cloud
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "openai", voice = "nova" }
)

-- LocalAI with pocket-tts
HoundTTS.Transmit("Wind 270 at 10",
    { freqs = "127.5", coalition = 2 },
    { provider = "openai", voice = "azelma" }
)

-- Kitten TTS Server (self-hosted)
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "openai", voice = "Bella", speed = 1.1 }
)
```

**INI configuration:**

```ini
[OpenAI]
api_key    = sk-...              ; optional for local deployments
endpoint   = https://api.openai.com  ; or http://192.168.10.10:8088 for LocalAI
                                     ; or http://192.168.10.30:8005 for Kitten TTS Server
model      = tts-1               ; or pocket-tts, qwen-tts, gpt-4o-mini-tts, kitten-tts, etc.
chat_model = gpt-4o-mini         ; chat model used by HoundTTS.Translate()
```

### ElevenLabs

Requires an ElevenLabs API key set in `HoundTTS-credentials.ini`. The `voice` field is the ElevenLabs voice ID.

```lua
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "elevenlabs", voice = "21m00Tcm4TlvDq8ikWAM" }
)
```

> **Free-tier concurrency limit:** ElevenLabs free accounts allow only **one concurrent WebSocket connection**. If two `elevenlabs` transmissions overlap (e.g. when running DCS at x2–x4 speed), the second request will be rejected and produce no audio. Use a paid plan, or ensure transmissions are spaced far enough apart that the first has finished before the next fires.

## Performance tip: long text

For lengthy transmissions (e.g. detailed ATIS reports that can run 2–3 minutes), cloud providers add noticeable latency because the entire audio must be generated server-side and downloaded before playback begins. A local provider like **Piper** or **SAPI** will start speaking almost immediately with no network round-trip, making them the fastest and most cost-effective choice for long-form text.

## Encryption

SRS encryption is supported per-transmission via `HoundTTS.Transmit`. The encryption key must match the key configured on the SRS server.

```lua
-- Global defaults (disabled by default)
HoundTTS.SRS_ENCRYPT = false
HoundTTS.SRS_ENC_KEY = 0

-- Per-call override via Transmit
HoundTTS.Transmit("Encrypted message",
    { freqs = "251.0", coalition = 2, encrypt = true, encKey = 7 },
    { provider = "piper", voice = "en_US-lessac-low" }
)
```

> `TextToSpeech` always uses the global `SRS_ENCRYPT` / `SRS_ENC_KEY` values.

## Configuration

### Option A: Lua config file (recommended for persistent settings)

Create `Saved Games\DCS\Config\HoundTTS.lua`. All fields are optional:

```lua
SRS_HOST             = "127.0.0.1"   -- SRS server IP (default: localhost)
SRS_PORT             = 5002           -- SRS server port
SRS_ENCRYPT          = false          -- global encryption default
SRS_ENC_KEY          = 0             -- global encryption key

DEFAULT_TRANSMITTER  = "srs"
DEFAULT_PROVIDER     = "sapi"       -- "piper" | "supertonic" | "sapi" | "edge" | "azure" | "google" | "elevenlabs" | "openai" | "aws" | "polly"
DEFAULT_VOICE        = ""            -- default voice/model name
DEFAULT_CULTURE      = "en-US"       -- default culture/locale
DEFAULT_GENDER       = "female"      -- "male" | "female"
```

### Option B: In mission scripts

```lua
HoundTTS.DEFAULT_PROVIDER = "sapi"
HoundTTS.DEFAULT_CULTURE  = "en-US"
HoundTTS.SRS_PORT         = 5002
-- HoundTTS.SRS_HOST = "192.168.1.10"  -- remote SRS server (optional)
```

### Option C: Credentials INI (for API keys and paths)

Copy `HoundTTS-credentials.ini.example` → `HoundTTS-credentials.ini` in the same `Config\` folder and edit it. This file is read directly by the DLL — credentials never appear in DCS logs or Lua.

```ini
[Piper]
; Path to the piper binary directory (containing piper.dll or piper.exe)
; Leave blank to use bundled bin\piper\ folder (from the piper-engine package)
path =
; Path to folder containing .onnx voice model files
; Leave blank to use bundled voices\ folder
voice_path =
; Max concurrent piper synthesizers GLOBALLY (across all voices, default 4).
; Excess requests queue instead of spawning unbounded ORT sessions.
; Each piper session runs single-threaded (upstream piper1-gpl sets
; intra-op = 1); max_concurrent caps total ORT worker threads.
max_concurrent = 4

[Google]
; Path to a Google Cloud service-account JSON file.
; Download from: Google Cloud Console → IAM & Admin → Service Accounts → Keys → Add Key → JSON
; The service account must have the "Cloud Text-to-Speech API User" role.
credentials_file =

[Azure]
; Azure Cognitive Services Speech subscription key
key =
; Azure region (e.g. eastus, westeurope)
region =

[ElevenLabs]
; ElevenLabs API key
api_key =
; Model ID (default: eleven_turbo_v2)
model_id = eleven_turbo_v2

[OpenAI]
; OpenAI API key (optional for local deployments)
api_key =
; Base URL — /v1/audio/speech is always appended
endpoint = https://api.openai.com
; Model name
model = tts-1
; Chat model for translation
chat_model = gpt-4o-mini

[General]
; PCM cache settings for synthesized audio buffers (16kHz mono PCM)
; Cache reduces latency for repeated TTS requests with identical parameters
cache_enabled = true          ; Enable/disable in-memory PCM cache (default: true)
cache_max_mb = 100            ; Maximum cache size in MB (default: 100)
cache_ttl_minutes = 5         ; Cache entry TTL in minutes, 0 = no expiration (default: 5)
```

### PCM Cache

HoundTTS includes an in-memory LRU cache for synthesized PCM audio buffers (16kHz mono). This cache reduces latency for repeated TTS requests with identical parameters (provider, message, voice, etc.). Cache entries are evicted based on least-recently-used order and optional time-to-live. Configure via the `[General]` section in `HoundTTS-credentials.ini`.

## Usage (in mission scripts)

HoundTTS exposes a **Lua API** for use within DCS mission scripts. This is the primary interface. The Lua layer wraps low-level DLL functions (`textToSpeech`, `startNoise`, `startTone`, `updateSession`, `killAllSessions`, `translateAsync`, `getTranslationResult`) and provides a mission-friendly interface with named parameter tables, async helpers, and session tracking.

**Main Lua functions:**

- **`Transmit`** — flexible TTS transmission with named parameters. Supports all TTS providers, encryption, position tracking.
- **`TextToSpeech`** — drop-in STTS replacement. Identical signature for backward compatibility.
- **`TransmitTone`** — sine-wave tone transmission (connection testing, alerts).
- **`TransmitNoise`** — noise/jamming transmission with spectral control.
- **`UpdateSession`** — track moving transmitters in real-time.
- **`KillSession`** — stop a live transmission early.
- **`TestTone`** — quick connection test (440 Hz tone).
- **`Translate`** — async text translation (OpenAI, Google, LibreTranslate, AWS, Azure).

```lua
-- Drop-in STTS replacement
HoundTTS.TextToSpeech("Hello DCS World", "251.0", "AM", 1.0, "ATC", 2)

-- Piper TTS over SRS
HoundTTS.Transmit(
    "Bogey, bullseye 270 for 15",
    { freqs = "251.0", modulations = "AM", coalition = 2, name = "GCI" },
    { provider = "piper", voice = "en_US-lessac-low" }
)

-- SAPI (Windows system voice, no API key needed)
HoundTTS.Transmit(
    "Wind 270 at 10, QNH 1013",
    { freqs = "127.5", coalition = 2, name = "ATIS" },
    { provider = "sapi", gender = "female", culture = "en-US" }
)

-- Multiple frequencies
HoundTTS.Transmit(
    "All stations, ATIS information Alpha",
    { freqs = "305.0,127.5", modulations = "AM,AM", coalition = 2, name = "ATIS" },
    { provider = "piper", voice = "en_US-ryan-low" }
)

-- With position and encryption
local point = Unit.getByName("GCI"):getPoint()
HoundTTS.Transmit(
    "Cleared hot",
    { freqs = "251.0", coalition = 2, name = "GCI", point = point,
      encrypt = true, encKey = 7 },
    { provider = "piper", voice = "en_US-lessac-low", speed = 1.1 }
)

-- Auto-track a DCS object (live position updates, auto-kill on death)
HoundTTS.Transmit(
    "GCI on station, contact me on this freq",
    { freqs = "251.0", coalition = 2, name = "GCI",
      dcsObject = Unit.getByName("GCI") },
    { provider = "piper", voice = "en_US-lessac-low" }
)

-- Connection test (bypasses TTS entirely, sends a 440 Hz tone)
HoundTTS.TestTone("251.0", "AM", 2)

-- Estimate speech time
local seconds = HoundTTS.getSpeechTime("Hello DCS World", 1, false)

-- Translate text (async — does not block DCS)
HoundTTS.Translate("Bogey, BRAA 270 for 15, angels 20, hostile",
    { provider = "openai", language = "de" },
    function(translated, err)
        if translated then
            trigger.action.outText("Translation: " .. translated, 10)
        end
    end)

-- Translate and transmit in one call (DLL-side, no DCS callback needed)
HoundTTS.Transmit(
    "Two bandits, BRA 090 for 30, angels 15",
    { freqs = "251.0", coalition = 2, name = "GCI" },
    { provider = "openai", voice = "nova" },
    { provider = "openai", language = "fr" }  -- inline translation params
)
```

### API Reference

All functions below are **Lua API** — called from mission scripts via `HoundTTS.<function>()`. Each wraps one or more DLL functions (noted in parentheses for reference). For low-level DLL access, see the [Architecture](#architecture) section.

#### Primary API

---

#### `HoundTTS.Transmit(message, transmission_params, provider_params, [translation_params])`

**Lua wrapper for:** `_dll.textToSpeech()` (with optional inline translation)

Flexible API. Not constrained by STTS compatibility. Designed for Piper TTS, encryption, position tracking, and future transmitter types. Supports optional on-the-fly translation before TTS synthesis.

**`transmission_params`** (table) — where and how to transmit:

| Field       | Type                  | Default             | Description                                                                                                                                                                                                                                                    |
| ----------- | --------------------- | ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| transmitter | string                | `"srs"`             | Transmitter type. Currently only `"srs"`                                                                                                                                                                                                                       |
| freqs       | string                | `"251.0"`           | Frequency in MHz, comma-separated                                                                                                                                                                                                                              |
| modulations | string                | `"AM"`              | `AM` or `FM`, comma-separated                                                                                                                                                                                                                                  |
| coalition   | number                | `0`                 | 0=spectator, 1=red, 2=blue                                                                                                                                                                                                                                     |
| name        | string                | `"HoundTTS"`        | Client name shown in SRS                                                                                                                                                                                                                                       |
| point       | Vec3/nil              | `nil`               | DCS position for geo-location                                                                                                                                                                                                                                  |
| dcsObject   | Unit/StaticObject/nil | `nil`               | DCS Unit or StaticObject. If `point` is not set, position is derived from this object (with vertical offset for ground/static). Session is auto-tracked: position updates while the object is alive, and the session is killed when it dies or stops existing. |
| encrypt     | boolean               | `false`             | Enable SRS encryption                                                                                                                                                                                                                                          |
| encKey      | number                | `0`                 | Encryption key (0–255, must match SRS)                                                                                                                                                                                                                         |
| host        | string                | `HoundTTS.SRS_HOST` | SRS server IP                                                                                                                                                                                                                                                  |
| port        | number                | `HoundTTS.SRS_PORT` | SRS server port                                                                                                                                                                                                                                                |

**`provider_params`** (table) — which TTS provider to use:

| Field    | Type   | Default                     | Description                                                                                                                                                        |
| -------- | ------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| provider | string | `HoundTTS.DEFAULT_PROVIDER` | `"piper"` / `"supertonic"` / `"sapi"` (`"win"`) / `"edge"` (`"edgetts"`) / `"azure"` / `"google"` (`"gcloud"`) / `"elevenlabs"` / `"openai"` / `"polly"` (`"aws"`) |
| voice    | string | `HoundTTS.DEFAULT_VOICE`    | Piper model name, Supertonic voice style name (e.g. `M1`, `F1`), SAPI voice name, Edge/Azure/Google/Polly voice name, or ElevenLabs voice ID                       |
| speaker  | string | `""`                        | Piper multi-speaker model: speaker name or numeric ID                                                                                                              |
| engine   | string | `"standard"`                | Polly engine: `"standard"` / `"neural"` / `"generative"`                                                                                                           |
| culture  | string | `HoundTTS.DEFAULT_CULTURE`  | BCP-47 locale e.g. `"en-US"`, `"en-GB"` (used by SAPI, Azure, Google)                                                                                              |
| gender   | string | `HoundTTS.DEFAULT_GENDER`   | `"male"` / `"female"` (used by SAPI, Google)                                                                                                                       |
| speed    | number | `1.0`                       | Speech rate (0.5 = half speed, 1.0 = normal, 2.0 = double speed)                                                                                                   |
| volume   | number | `1.0`                       | Output level: 0.0 = silence, 1.0 = full volume                                                                                                                     |

**`translation_params`** (table, optional) — translate before TTS:

| Field           | Type   | Default | Description                                                                                                                                                                        |
| --------------- | ------ | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| provider        | string | `""`    | Translation provider: `"openai"`, `"google"` (`"gcloud"`), `"libretranslate"` (`"libre"`), `"aws"` (`"polly"`), `"azure"`. Omit or empty to skip translation.                      |
| language        | string | `"en"`  | ISO 639-1 target language code e.g. `"de"`, `"fr"`, `"ru"`, `"he"` — see [Wikipedia: List of ISO 639 language codes](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes) |
| source_language | string | `"en"`  | ISO 639-1 source language code (default: `"en"`)                                                                                                                                   |

When provided, the message is translated **before** being sent to the TTS engine. The entire translate → TTS → SRS pipeline runs on a background thread. On translation failure, the original (untranslated) message is spoken.

**Provider routing:**

| `transmitter` | `provider`     | Engine used                                                                                                                                                                                   |
| ------------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `"srs"`       | `"piper"`      | Direct SRS + Piper TTS (offline)                                                                                                                                                              |
| `"srs"`       | `"supertonic"` | Direct SRS + Supertonic TTS (offline, ONNX). voice=style name (e.g. `M1`, `F1`); culture=ISO 639-1 code (e.g. `"en"`, `"ko"`); defaults: `HoundTTS.DEFAULT_VOICE`, `HoundTTS.DEFAULT_CULTURE` |
| `"srs"`       | `"sapi"`       | Direct SRS + Windows SAPI 5.4 (offline)                                                                                                                                                       |
| `"srs"`       | `"azure"`      | Direct SRS + Azure Cognitive Speech                                                                                                                                                           |
| `"srs"`       | `"google"`     | Direct SRS + Google Cloud TTS                                                                                                                                                                 |
| `"srs"`       | `"elevenlabs"` | Direct SRS + ElevenLabs WebSocket                                                                                                                                                             |
| `"srs"`       | `"aws"`        | Direct SRS + AWS Polly (`"polly"` alias)                                                                                                                                                      |
| `"srs"`       | `"openai"`     | Direct SRS + OpenAI / LocalAI / Kitten TTS Server HTTP REST                                                                                                                                   |
| `"srs"`       | `"edge"`       | Direct SRS + Microsoft Edge Read Aloud (free cloud, no API key). Uses same neural voices as Azure.                                                                                            |

Returns: `speechTime` (number — estimated speech duration in seconds), `sessionId` (string — for use with `UpdateSession` / `KillSession`).

---

#### `HoundTTS.Translate(message, provider_params, callback)`

**Lua wrapper for:** `_dll.translateAsync()` + `_dll.getTranslationResult()` (with polling)

Translates text asynchronously. **Async** — returns immediately, the callback fires when the translation is ready. Designed for military aviation context — standard brevity codes (FOX 1/2/3, BULLSEYE, BRAA, WINCHESTER, etc.) are preserved in their original form (OpenAI provider only).

```lua
HoundTTS.Translate("Two contacts, BULLSEYE 270 for 40",
    { provider = "openai", language = "de" },
    function(translated, err)
        if translated then
            -- use the translated text
        else
            env.error("Translation failed: " .. tostring(err))
        end
    end)
```

| Argument        | Type     | Description                                    |
| --------------- | -------- | ---------------------------------------------- |
| message         | string   | Text to translate                              |
| provider_params | table    | Provider settings (see below)                  |
| callback        | function | `function(translated, err)` — called when done |

**`provider_params`** (table):

| Field    | Type   | Default    | Description                                                                                                                                                                   |
| -------- | ------ | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| provider | string | `"openai"` | `"openai"`, `"google"` (`"gcloud"`), `"libretranslate"` (`"libre"`), `"aws"` (`"polly"`), or `"azure"`                                                                        |
| language | string | `"en"`     | ISO 639-1 two-letter code e.g. `"de"`, `"fr"`, `"ru"`, `"he"` — see [Wikipedia: List of ISO 639 language codes](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes) |

The HTTP request runs on a DLL background thread. A `timer.scheduleFunction` polls for the result every 0.5 seconds and invokes the callback — DCS is never blocked.

**OpenAI provider:** uses the chat model configured via `chat_model` in `[OpenAI]` (default: `gpt-4o-mini`), same `endpoint` and `api_key` as OpenAI TTS. Prompt-engineered to output only translated text and preserve aviation brevity codes.

**Google provider:** uses the [Cloud Translation API v2](https://cloud.google.com/translate/docs/reference/rest/v2/translate) with the same `[Google] credentials_file` service-account JSON as Google Cloud TTS. Requires the **Cloud Translation API** to be enabled on the GCP project. Pure translation — no prompt engineering.

**LibreTranslate provider:** uses any [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) instance — self-hosted or public. Fully offline when self-hosted. Configure via `[LibreTranslate]` in `HoundTTS-credentials.ini`. No Google account or OpenAI key required.

```ini
[LibreTranslate]
endpoint = http://localhost:5000  ; or https://libretranslate.com
api_key  =                        ; leave blank for instances that don't require one
```

**AWS provider:** uses [Amazon Translate](https://aws.amazon.com/translate/) with the same `[AWS]` credentials as Amazon Polly TTS. The IAM user needs the `translate:TranslateText` permission in addition to `polly:SynthesizeSpeech`.

**Azure provider:** uses [Azure AI Translator](https://azure.microsoft.com/en-us/products/ai-services/ai-translator) with the same `[Azure]` subscription key and region as Azure TTS.

**Supported languages (all three providers):** English, German, French, Spanish, Italian, Portuguese, Russian, Dutch, Polish, Swedish, Norwegian, Danish, Finnish, Greek, Romanian, Hungarian, Czech, Slovak, Bulgarian, Croatian, Slovenian, Turkish, Ukrainian, Arabic, Hebrew, Chinese, Japanese, Korean — or any ISO 639-1 code passed directly.

---

#### `HoundTTS.TransmitNoise(transmission_params, provider_params)`

**Lua wrapper for:** `_dll.startNoise()`

Starts a noise transmission (e.g., jamming). Runs for the specified number of seconds when `duration > 0`, or up to 2 hours (7200 s) when `duration` is 0 (the default). All noise sessions are hard-capped at 7200 s regardless of the requested duration. Returns a `sessionId` for position updates or early termination via `KillSession`.

**Noise types demo:** [YouTube Short showing white/pink/chirp/harsh/jam modes](https://www.youtube.com/watch?v=UlYN0XQoAgg)

```lua
-- Start a noise jammer on 251.0 MHz FM
local jammerId = HoundTTS.TransmitNoise(
    { freqs = "251.0", modulations = "FM", coalition = 2, name = "Jammer" },
    { noiseType = "jam", volume = 0.7 }
)

-- Track a moving jammer platform
local jammerUnit = Unit.getByName("JammerPlatform")
local function trackJammer(_, t)
    if not HoundTTS.UpdateSession(jammerId, { point = jammerUnit:getPoint() }) then
        return nil  -- session ended
    end
    return t + 0.5
end
timer.scheduleFunction(trackJammer, nil, timer.getTime() + 0.5)

-- Later: stop the jammer
HoundTTS.KillSession(jammerId)
```

**`transmission_params`** (table) — same as `Transmit`:

| Field       | Type     | Default             | Description                            |
| ----------- | -------- | ------------------- | -------------------------------------- |
| transmitter | string   | `"srs"`             | Transmitter type                       |
| freqs       | string   | `"251.0"`           | Frequency in MHz, comma-separated      |
| modulations | string   | `"AM"`              | `AM` or `FM`, comma-separated          |
| coalition   | number   | `0`                 | 0=spectator, 1=red, 2=blue             |
| name        | string   | `"HoundTTS-Jammer"` | Client name shown in SRS               |
| point       | Vec3/nil | `nil`               | DCS position for geo-location          |
| encrypt     | boolean  | `false`             | Enable SRS encryption                  |
| encKey      | number   | `0`                 | Encryption key (0–255, must match SRS) |
| host        | string   | `HoundTTS.SRS_HOST` | SRS server IP                          |
| port        | number   | `HoundTTS.SRS_PORT` | SRS server port                        |

**`provider_params`** (table):

| Field     | Type   | Default   | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| --------- | ------ | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --- |
| noiseType | string | `"white"` | `"white"` / `"pink"` (aliases — same overdriven 1/f pink noise, dense full-spectrum) / `"chirp"` / `"harsh"` / `"jam"` — all tuned for channel jamming. `"chirp"`: noise floor + 8 fast-swept sines with overdrive. `"harsh"`: noise floor + 8 rapid square-wave oscillators with heavy overdrive (most aggressive). `"jam"`: anti-denoise + anti-AGC mode — pink noise + ultra-short oscillators (5–35 ms) + duty-cycle AM pulses + random bursts; designed to resist both the SRS client's Speex noise reduction and its incoming-audio AGC normalization. |
| volume    | number | `1.0`     | Output level (0.0 = silence, 1.0 = full)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| duration  | number | `600`     | Duration in seconds (default: 600 = 10 minutes). 0 = runs until killed (hard-capped at 7200 s / 2 hours)                                                                                                                                                                                                                                                                                                                                                                                                                                                     |     |
| spreadKhz | number | `250`     | Total adjacent-channel spectral spread in kHz. Controls the maximum ±bandwidth around each center frequency for adjacent-channel tones and leakage. Larger values create wider spectral leakage; smaller values keep noise more tightly clustered. Must be positive to take effect and interacts with `stepKhz`, `noiseType`, `volume`, and `duration`.                                                                                                                                                                                                      |
| stepKhz   | number | `25`      | Spacing between adjacent-channel tones in kHz. Defaults to 25 kHz standard AM spacing. Larger values place leakage farther apart, while smaller values increase channel density and overlap. Must be positive to take effect.                                                                                                                                                                                                                                                                                                                                |
| seed      | number | _auto_    | RNG seed for reproducible noise (optional)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |

Returns: `sessionId` string for use with `UpdateSession` / `KillSession`.

---

#### `HoundTTS.TransmitTone(transmission_params, provider_params)`

**Lua wrapper for:** `_dll.startTone()`

Transmits a configurable sine-wave tone over SRS. Returns a `sessionId` that can be used with `UpdateSession` (for position tracking) or `KillSession` (to stop early).

```lua
-- 5-second 1000 Hz tone on 251.0 MHz AM
local sessionId = HoundTTS.TransmitTone(
    { freqs = "251.0", modulations = "AM", coalition = 2, name = "ATIS" },
    { duration = 5.0, freqHz = 1000.0, volume = 0.8 }
)

-- Track a moving unit
local unit = Unit.getByName("MyUnit")
local function trackTone(_, t)
    if not HoundTTS.UpdateSession(sessionId, { point = unit:getPoint() }) then
        return nil  -- tone finished or session killed
    end
    return t + 0.5
end
timer.scheduleFunction(trackTone, nil, timer.getTime() + 0.5)
```

**`transmission_params`** (table) — same as `Transmit`:

| Field       | Type     | Default             | Description                            |
| ----------- | -------- | ------------------- | -------------------------------------- |
| transmitter | string   | `"srs"`             | Transmitter type                       |
| freqs       | string   | `"251.0"`           | Frequency in MHz, comma-separated      |
| modulations | string   | `"AM"`              | `AM` or `FM`, comma-separated          |
| coalition   | number   | `0`                 | 0=spectator, 1=red, 2=blue             |
| name        | string   | `"HoundTTS-Tone"`   | Client name shown in SRS               |
| point       | Vec3/nil | `nil`               | DCS position for geo-location          |
| encrypt     | boolean  | `false`             | Enable SRS encryption                  |
| encKey      | number   | `0`                 | Encryption key (0–255, must match SRS) |
| host        | string   | `HoundTTS.SRS_HOST` | SRS server IP                          |
| port        | number   | `HoundTTS.SRS_PORT` | SRS server port                        |

**`provider_params`** (table):

| Field    | Type   | Default | Description                                           |
| -------- | ------ | ------- | ----------------------------------------------------- |
| duration | number | `2.0`   | Duration in seconds (hard-capped at 7200 s / 2 hours) |
| freqHz   | number | `440.0` | Tone frequency in Hz (20–20000)                       |
| volume   | number | `1.0`   | Output level (0.0 = silence, 1.0 = full)              |

Returns: `sessionId` string for use with `UpdateSession` / `KillSession`.

#### Compatibility & Utility

---

#### `HoundTTS.TextToSpeech(message, freqs, modulations, volume, name, coalition, [point], [speed], [gender], [culture], [voice], [googleTTS], [AzureCreds])`

**Lua wrapper for:** `_dll.textToSpeech()`

Drop-in replacement for STTS. Transmits directly over SRS using SAPI, Google, or Azure depending on the flags passed.

| Argument    | Type     | Default    | Description                           |
| ----------- | -------- | ---------- | ------------------------------------- |
| message     | string   | required   | Text to speak                         |
| freqs       | string   | required   | Frequency in MHz, comma-separated     |
| modulations | string   | required   | `AM` or `FM`, comma-separated         |
| volume      | number   | `1.0`      | 0.0 – 1.0                             |
| name        | string   | `HoundTTS` | Transmitter name shown in SRS         |
| coalition   | number   | `0`        | 0=spectator, 1=red, 2=blue            |
| point       | Vec3/nil | `nil`      | DCS position (e.g. `Unit:getPoint()`) |
| speed       | number   | `1`        | Speech rate                           |
| gender      | string   | `female`   | `male` / `female`                     |
| culture     | string   | `""`       | e.g. `en-US`, `en-GB`                 |
| voice       | string   | `""`       | Voice/model name                      |
| googleTTS   | boolean  | `false`    | Use Google TTS credentials            |
| AzureCreds  | string   | `nil`      | Azure TTS credentials string          |

Returns: estimated speech time in seconds.

---

#### `HoundTTS.getSpeechTime(length, [speed], [googleTTS])`

**Lua wrapper for:** `_dll.getSpeechTime()`

Returns estimated speech duration in seconds. `length` can be a number (character count) or a string (measured automatically).

---

#### `HoundTTS.TestTone([freqs], [modulations], [coalition], [duration], [volume])`

**Lua wrapper for:** `_dll.startTone()`

Sends a sine wave tone directly over SRS, bypassing the TTS engine entirely. Use this to verify the SRS connection is working before debugging TTS issues.

```lua
HoundTTS.TestTone("251.0", "AM", 2)  -- 2-second 440 Hz tone on 251.0 MHz AM, blue coalition
```

| Argument    | Type   | Default   | Description                |
| ----------- | ------ | --------- | -------------------------- |
| freqs       | string | `"251.0"` | Frequency in MHz           |
| modulations | string | `"AM"`    | `AM` or `FM`               |
| coalition   | number | `0`       | 0=spectator, 1=red, 2=blue |
| duration    | number | `2.0`     | Duration in seconds        |
| volume      | number | `1.0`     | Volume level (0.0–1.0)     |

#### Advanced (Manual Session Control)

---

#### `HoundTTS.UpdateSession(sessionId, update_params)`

**Lua wrapper for:** `_dll.updateSession()`

Updates the position of a live transmission (TTS, tone, or noise). It can be called frequently (e.g., every 0.5s from a scheduler) to track a moving unit.

```lua
local sessionId = HoundTTS.TransmitNoise(...)

-- Track a moving unit
local unit = Unit.getByName("JammerPlatform")
local function trackPosition(_, t)
    if not HoundTTS.UpdateSession(sessionId, { point = unit:getPoint() }) then
        return nil  -- session ended (transmission finished or killed)
    end
    return t + 0.5  -- check again in 0.5 seconds
end
timer.scheduleFunction(trackPosition, nil, timer.getTime() + 0.5)
```

| Argument      | Type   | Description                                                           |
| ------------- | ------ | --------------------------------------------------------------------- |
| sessionId     | string | Session ID returned by `Transmit`, `TransmitTone`, or `TransmitNoise` |
| update_params | table  | Position update parameters (see below)                                |

**`update_params`** (table):

| Field | Type   | Description                                                 |
| ----- | ------ | ----------------------------------------------------------- |
| point | Vec3   | DCS position (e.g., `Unit:getPoint()`)                      |
| lat   | number | Explicit latitude (overrides point-derived value)           |
| lon   | number | Explicit longitude (overrides point-derived value)          |
| alt   | number | Explicit altitude in meters (overrides point-derived value) |

Returns: `true` if the session is still alive, `false` if the session was found but has ended, and `nil` if the session was not found. The Lua wrapper converts `point` into explicit `lat`/`lon`/`alt` values before calling the native binding `HoundTTS.updateSession`, so the session lookup and return semantics are dictated by the native binding implementation. Use this return value to break out of position-update loops.

---

#### `HoundTTS.KillSession(sessionId)`

**Lua wrapper for:** `_dll.updateSession()` (with `alive=false` flag)

Stops a live transmission (TTS, tone, or noise) identified by `sessionId`. For noise jammers this terminates the transmission immediately.

```lua
local sessionId = HoundTTS.TransmitNoise(...)
-- ... later ...
HoundTTS.KillSession(sessionId)
```

| Argument  | Type   | Description                                                           |
| --------- | ------ | --------------------------------------------------------------------- |
| sessionId | string | Session ID returned by `Transmit`, `TransmitTone`, or `TransmitNoise` |

Returns: `true` if the session was found and killed, `false` otherwise.

## Architecture

### Lua API Layer

The **Lua API** (all `HoundTTS.*` functions) is the primary interface for mission scripts. It is defined in `HoundTTS-mission.lua` and wraps the low-level DLL functions. The Lua layer provides:

- Named parameter tables (instead of positional args)
- Async helpers (translation polling, session tracking)
- Coordinate conversion (`point` → `lat`/`lon`/`alt`)
- Auto-tracking of DCS objects (position updates on death/existence)

### DLL Layer

The **DLL** (`HoundTTS.dll`) exports low-level Lua C bindings:

- `textToSpeech()` — TTS synthesis and transmission
- `startNoise()` — noise/jamming transmission
- `startTone()` — tone transmission
- `updateSession()` — position updates and kill signals
- `killAllSessions()` — stop all transmissions
- `translateAsync()` / `getTranslationResult()` — async translation
- `getSpeechTime()` — speech duration estimation
- `clearPCMCache()` / `getCacheStats()` — cache management
- `init()` — initialization (called once at load)

### Loading Pattern

Same pattern as [DCS-gRPC](https://github.com/DCS-gRPC/rust-server): load the DLL **before** DCS sanitizes the mission scripting environment.

```
MissionScripting.lua
│
├─ dofile('Scripts/ScriptingSystem.lua')
├─ dofile(lfs.writedir()..'Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua')
│       │
│       ├─ require("HoundTTS")   ← loads HoundTTS.dll (pre-sanitization)
│       ├─ reads optional Config\HoundTTS.lua
│       └─ defines Lua API: Transmit, TextToSpeech, TransmitTone, TransmitNoise,
│           UpdateSession, KillSession, TestTone, Translate, getSpeechTime
│
└─ sanitizeModule('os','io','lfs') + remove require/package
       ↓
   Mission scripts call HoundTTS.Transmit() etc. normally
   (DLL reference captured in upvalue before sanitization)
```

**Key points:**

- DLL is loaded directly in the **mission Lua state** before sanitization
- `_dll` reference is captured as a local upvalue — survives sanitization
- `coord.LOtoLL` (mission-only global) is used at call time for position conversion
- Hook script (`HoundTTS-hook.lua` → `HoundTTS.lua`) reserved for future use

### Source tree

```
src/
├── dllmain.cpp          # Lua C bindings, MakeBackend() factory
├── backend.h            # ITTSBackend interface + TTSRequest struct
├── config_reader.*      # INI parser singleton (reads HoundTTS-credentials.ini)
├── audio_queue.h        # Thread-safe Opus frame queue (used by codecs + backends)
├── process_launcher.*   # Async CreateProcess wrapper
├── speech_time.h        # Speech time estimation
├── utils.h              # String/path/registry helpers
├── codecs/
│   ├── opus_encoder.*   # libopus streaming encoder (16kHz mono → 40ms Opus frames)
│   └── mp3_decoder.*    # minimp3 MP3 decoder (24kHz MP3 → 16kHz mono PCM, stateful)
├── backends/
│   └── srs/
│       ├── srs_backend.*  # Direct SRS backend (TCP/UDP protocol)
│       ├── srs_client.*   # SRS TCP/UDP client, 40ms multimedia timer
│       └── srs_types.h    # FreqMod, ParseFreqMods, GenerateGUID
└── providers/
    ├── shared/
    │   └── google/
    │       └── google_auth.*       # Shared Google OAuth2 JWT auth (used by TTS + Translate)
    ├── generators/
    │   ├── noise.*                 # White/chirp/harsh/jam noise generators
    │   └── tone.*                  # Sine-wave tone generator
    ├── tts/
    │   ├── azure/
    │   │   └── azure_tts.*        # Azure Cognitive Services REST API
    │   ├── elevenlabs/
    │   │   └── elevenlabs_tts.*   # ElevenLabs WebSocket streaming API
    │   ├── edge/
    │   │   ├── edge_tts.*         # Edge TTS WebSocket API (24kHz MP3 → 16kHz PCM)
    │   │   └── edge_drm.*         # Edge TTS DRM token generation
    │   ├── google/
    │   │   └── google_tts.*       # Google Cloud TTS REST API
    │   ├── kitten/
    │   │   └── kitten_tts.*       # Kitten TTS Server HTTP REST API
    │   ├── openai/
    │   │   └── openai_tts.*       # OpenAI-compatible TTS REST API (+ LocalAI)
    │   ├── piper/
    │   │   └── piper_tts.*        # In-process piper.dll synthesis (deprecated piper.exe fallback)
    │   ├── aws/
    │   │   └── aws_tts.*          # AWS Polly REST API
    │   ├── sapi/
    │   │   └── sapi_tts.*         # Windows SAPI 5.4 (ISpVoice, MSVC only)
    │   └── supertonic/
    │       └── supertonic_tts.*   # Supertonic ONNX TTS (multilingual)
    └── translate/
        ├── google/
        │   └── google_translate.*      # Google Cloud Translation API v2
        ├── libretranslate/
        │   └── libretranslate.*        # LibreTranslate REST API (self-hosted)
        ├── aws/
        │   └── aws_translate.*         # AWS Translate API
        ├── azure/
        │   └── azure_translate.*       # Azure Translator API
        └── openai/
            └── openai_chat.*           # OpenAI chat completions API (translation)
```

## Troubleshooting

### Breaking Changes

#### 0.2.x → 0.3.x (upcoming)

**Kitten TTS dedicated provider removed**

The `"kitten"` / `"kittentts"` / `"kitten_tts"` provider names will be removed. In the current release they still work and log a deprecation warning in `Logs\HoundTTS.log`.

**Action required:** migrate to `provider = "openai"` and point `[OpenAI] endpoint` at your Kitten TTS Server — see the [Kitten TTS section](#kitten-tts-self-hosted--deprecated) above for step-by-step instructions.

**`piper.exe` subprocess fallback removed**

Piper TTS now runs entirely in-process via `piper.dll` (shipped in the piper-engine package). The legacy `piper.exe` subprocess fallback is deprecated in the current release and will be removed in 0.3.x. If `piper.dll` fails to load, a deprecation warning is logged in `Logs\HoundTTS.log`.

**Action required:** ensure you are using the piper-engine package (`HoundTTS-piper-engine-windows.zip`), which includes `piper.dll`, `onnxruntime.dll`, and `espeak-ng-data/`. Remove any standalone `piper.exe` from your installation — it is no longer needed or supported.

---

#### 0.1.x → 0.2.x

**`[Polly]` renamed to `[AWS]` in `HoundTTS-credentials.ini`**

The credentials INI section for Amazon Polly has been renamed from `[Polly]` to `[AWS]` to reflect that it now covers all AWS services (Polly TTS and Amazon Translate).

**Action required:** rename the section header in your `HoundTTS-credentials.ini`:

```ini
; Before (0.1.x)
[Polly]
access_key = ...

; After (0.2.x)
[AWS]
access_key = ...
```

All keys inside the section (`access_key`, `secret_key`, `region`, `engine`) are unchanged. The Lua provider name `"polly"` continues to work as an alias — only the INI section header needs updating.

---

## Building

### Windows (Docker-based, recommended)

The primary build entry point is `build.bat`:

```bat
build.bat
```

This batch file wrapper:

- Validates Docker is installed and the daemon is running
- Invokes `build-docker.ps1` with execution policy bypass (no manual PowerShell configuration needed)
- Displays build elapsed time
- Preserves the build exit code

**Requirements:** Docker Desktop must be running in **Windows containers** mode.

**What it builds:** MSVC + Windows 10 SDK (19041) inside a Windows Server Core container. All providers are available, including SAPI 5.4.

**Output layout:**

```
dist\
├── base\               ← DLL + Lua scripts (always install this)
├── piper-engine\       ← Piper TTS engine (~16 MB, install for Piper TTS)
├── piper-voices\       ← Bundled voice models (~120 MB, or bring your own)
└── supertonic-engine\  ← Supertonic TTS engine (install for Supertonic TTS)
```

**Troubleshooting**

If you encounter the following error during the build process:

```text
re-exec error: exit status 1: output: hcsshim::ImportLayer failed in Win32: The system cannot find the path specified. (0x3)
```

This is typically caused by Windows path length limitations. To resolve this, enable long paths:

1. Run the following command in **PowerShell (as Administrator)**:
   ```PowerShell
   Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name 'LongPathsEnabled' -Value 1
   ```
2. **Restart Docker Desktop** for the changes to take effect.

**Alternative:** invoke the PowerShell script directly if you prefer:

```powershell
.\build-docker.ps1
```

## License

HoundTTS itself is released under the **MIT License** — see [LICENSE](LICENSE).

The **piper-engine** package (`HoundTTS-piper-engine-windows.zip`) contains `piper.dll`, built from [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl) and distributed under the **GNU General Public License v3.0 (GPLv3)**. The full license text is included as `bin\piper\COPYING`. Source code is available at the repository linked above.

## Acknowledgements

Special thanks to:

- **[@Applevangelist](https://github.com/Applevangelist)** — for adopting HoundTTS into [MOOSE](https://github.com/FlightControl-Master/MOOSE) and for providing much-needed guidance.
- **[@SpecialK](https://github.com/karel26)** — for integrating HoundTTS installation into [DCSServerBot](https://github.com/Special-K-s-Flightsim-Bots/DCSServerBot).

HoundTTS builds on the work of several open-source projects:

- **[DCS-SimpleRadioStandalone (SRS)](https://github.com/ciribob/DCS-SimpleRadioStandalone)** — the SRS TCP/UDP protocol, packet framing, and audio pipeline were studied and adapted to implement the native SRS client in this project.
- **[SkyEye](https://github.com/dharmab/skyeye)** — SkyEye's Go implementation of the SRS client and Opus audio pipeline served as a reference for the direct SRS integration approach in this project.
- **[DCS-gRPC](https://github.com/DCS-gRPC/rust-server)** — the pattern of loading a native DLL into the DCS mission Lua state before sanitization (via `MissionScripting.lua`) was pioneered by DCS-gRPC and is followed here.
