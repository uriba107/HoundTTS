# HoundTTS

A native C++ DLL replacement for [DCS-SimpleTextToSpeech](https://github.com/ciribob/DCS-SimpleTextToSpeech) that integrates with DCS World as a Lua extension.

## Benefits over the Lua script

- **No PowerShell overhead** — connects natively to SRS via direct TCP/UDP protocol
- **No focus stealing** — all TTS synthesis runs in background threads, no visible windows
- **Parallel calls** — each TTS request is fire-and-forget, no blocking
- **Multiple TTS providers** — Piper (offline, bundled), SAPI (Windows system voices, no API key), Azure, Google, ElevenLabs
- **Credentials stay out of Lua** — API keys are read directly by the DLL from an INI file, never exposed in mission scripts or DCS logs
- **Auto-detects SRS path** from the Windows registry — no manual path configuration needed

## Requirements

### Runtime (machine running DCS)

| Requirement                                                                     | Notes                                                                                                                                   |
| ------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| **Windows 10 (1903+), Windows 11, Windows Server 2019, or Windows Server 2022** | Minimum OS enforced by the Win10 SDK 19041 PE header stamp                                                                              |
| **SimpleRadioStandalone (SRS)**                                                 | Required for all transmission. See [SRS requirements](https://github.com/ciribob/DCS-SimpleRadioStandalone) for its own .NET dependency |
| **.NET**                                                                        | Not required by HoundTTS.dll itself — pure native C++                                                                                   |

### Build (Docker)

- **Windows containers** build: Windows Docker host with Windows containers mode enabled
- **Linux containers** build: any Docker host (Linux, macOS, Windows with Linux containers)

## Building

### Docker (recommended)

Two pipelines are available, both invoked via the same PowerShell script:

#### Windows containers (MSVC — full feature set including SAPI)

Requires Docker Desktop in **Windows containers** mode.

```powershell
.\build-docker.ps1
```

Builds with MSVC + Windows 10 SDK (19041) inside a Windows Server Core container. All providers including SAPI 5.4 are available.

#### Linux containers (MinGW cross-compile)

Requires Docker Desktop in **Linux containers** mode.

```powershell
.\build-docker.ps1 -Windows:$false
```

Or on Linux/macOS:

```bash
./build-docker.sh
```

Cross-compiles with `mingw-w64` from a Debian container. SAPI is not available in this build (returns silence). All other providers work.

Both pipelines produce the same `dist\` layout:

```
dist\
├── base\          ← DLL + Lua scripts (always install this)
└── piper-addon\   ← Piper engine + bundled voices (install for Piper TTS)
```

### Windows (native, no Docker)

```bat
build.bat
```

Auto-detects MinGW or MSVC CLI, generates the import library from `lua.dll`, builds the DLL, and copies everything to `dist\`.

## Installation

### 1. Copy files

Install `dist\base\` into your DCS Saved Games folder (e.g. `%USERPROFILE%\Saved Games\DCS\`).  
For Piper TTS, also install `dist\piper-addon\` on top of it.

> **Config files:** copy each `.example` file to the same name without `.example` and edit it. These files are never overwritten by updates — your live settings are safe.

```
Saved Games\DCS\
├── Config\
│   ├── HoundTTS.lua.example          ← copy to HoundTTS.lua and edit
│   └── HoundTTS-credentials.ini.example  ← copy to HoundTTS-credentials.ini and fill in
├── Mods\Services\HoundTTS\
│   ├── bin\
│   │   ├── HoundTTS.dll
│   │   └── piper\                    ← from piper-addon (Piper TTS only)
│   │       ├── piper.exe
│   │       ├── espeak-ng.dll
│   │       ├── onnxruntime.dll
│   │       └── ...
│   ├── voices\                       ← from piper-addon
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

Add the following line to `MissionScripting.lua` (in the DCS World install folder at `Scripts\MissionScripting.lua`).

```diff
  --Initialization script for the Mission lua Environment (SSE)

  dofile('Scripts/ScriptingSystem.lua')
+ dofile(lfs.writedir()..[[Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua]])

  --Sanitize Mission Scripting environment
```

The line **must** appear before the `sanitizeModule` block so that `require`, `package`, and `lfs` are still available when the script runs.

## TTS Providers

All providers are selected per-call via the `provider` field in `HoundTTS.Transmit`. The default is controlled by `HoundTTS.DEFAULT_PROVIDER` (default: `"piper"`).

### Piper (offline, bundled)

Connects directly to SRS over TCP/UDP. Synthesizes speech via [piper-tts-go/piper](https://github.com/piper-tts-go/piper). **No internet or API key required.** `piper.exe` and two bundled voice models are included in `dist\piper-addon\`.

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

### SAPI (Windows system voices)

Uses the Windows Speech API 5.4 (`ISpVoice`) — the same engine used by Narrator and other Windows TTS. **No internet or API key required.** Uses whatever voices are installed on the Windows machine running DCS.

> **Aliases:** `"sapi"` and `"win"` are interchangeable.

> **Requires the MSVC build** (`.\.build-docker.ps1`). The MinGW build returns silence for SAPI.

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

### Amazon Polly

Requires AWS access key, secret key, and region set in `HoundTTS-credentials.ini`.

> **Aliases:** `"polly"` and `"aws"` are interchangeable.

```lua
HoundTTS.Transmit("Cleared to land",
    { freqs = "251.0", coalition = 2 },
    { provider = "polly", voice = "Joanna", culture = "en-US", engine = "neural" }
)
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
DEFAULT_PROVIDER     = "sapi"       -- "piper" | "sapi" | "azure" | "google" | "elevenlabs"
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
; Path to piper.exe — leave blank to use bundled bin\piper\piper.exe
exe =
; Path to folder containing .onnx voice model files
; Leave blank to use bundled voices\ folder
voice_path =

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
```

## Usage (in mission scripts)

HoundTTS provides two APIs:

- **`TextToSpeech`** — drop-in replacement for STTS. Identical signature, transmits directly over SRS
- **`Transmit`** — flexible API with named parameter tables. Supports all TTS providers, encryption, position.

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

-- Connection test (bypasses TTS entirely, sends a 440 Hz tone)
HoundTTS.TestTone("251.0", "AM", 2)

-- Estimate speech time
local seconds = HoundTTS.getSpeechTime("Hello DCS World", 1, false)
```

### API Reference

---

#### `HoundTTS.TextToSpeech(message, freqs, modulations, volume, name, coalition, [point], [speed], [gender], [culture], [voice], [googleTTS], [AzureCreds])`

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

#### `HoundTTS.Transmit(message, transmission_params, provider_params)`

Flexible API. Not constrained by STTS compatibility. Designed for Piper TTS, encryption, and future transmitter types.

**`transmission_params`** (table) — where and how to transmit:

| Field       | Type     | Default             | Description                              |
| ----------- | -------- | ------------------- | ---------------------------------------- |
| transmitter | string   | `"srs"`             | Transmitter type. Currently only `"srs"` |
| freqs       | string   | `"251.0"`           | Frequency in MHz, comma-separated        |
| modulations | string   | `"AM"`              | `AM` or `FM`, comma-separated            |
| coalition   | number   | `0`                 | 0=spectator, 1=red, 2=blue               |
| name        | string   | `"HoundTTS"`        | Client name shown in SRS                 |
| point       | Vec3/nil | `nil`               | DCS position for geo-location            |
| encrypt     | boolean  | `false`             | Enable SRS encryption                    |
| encKey      | number   | `0`                 | Encryption key (0–255, must match SRS)   |
| host        | string   | `HoundTTS.SRS_HOST` | SRS server IP                            |
| port        | number   | `HoundTTS.SRS_PORT` | SRS server port                          |

**`provider_params`** (table) — which TTS provider to use:

| Field    | Type   | Default                     | Description                                                                                                 |
| -------- | ------ | --------------------------- | ----------------------------------------------------------------------------------------------------------- |
| provider | string | `HoundTTS.DEFAULT_PROVIDER` | `"piper"` / `"sapi"` (`"win"`) / `"azure"` / `"google"` (`"gcloud"`) / `"elevenlabs"` / `"polly"` (`"aws"`) |
| voice    | string | `HoundTTS.DEFAULT_VOICE`    | Piper model name, SAPI voice name, Azure/Google/Polly voice name, or ElevenLabs voice ID                    |
| speaker  | string | `""`                        | Piper multi-speaker model: speaker name or numeric ID                                                       |
| engine   | string | `"standard"`                | Polly engine: `"standard"` / `"neural"` / `"generative"`                                                    |
| culture  | string | `HoundTTS.DEFAULT_CULTURE`  | BCP-47 locale e.g. `"en-US"`, `"en-GB"` (used by SAPI, Azure, Google)                                       |
| gender   | string | `HoundTTS.DEFAULT_GENDER`   | `"male"` / `"female"` (used by SAPI, Google)                                                                |
| speed    | number | `1.0`                       | Speech rate (0.5 = half speed, 1.0 = normal, 2.0 = double speed)                                            |
| volume   | number | `1.0`                       | Output level: 0.0 = silence, 1.0 = full volume                                                              |

**Provider routing:**

| `transmitter` | `provider`     | Engine used                         |
| ------------- | -------------- | ----------------------------------- |
| `"srs"`       | `"piper"`      | Direct SRS + Piper TTS (offline)    |
| `"srs"`       | `"sapi"`       | Direct SRS + Windows SAPI 5.4       |
| `"srs"`       | `"azure"`      | Direct SRS + Azure Cognitive Speech |
| `"srs"`       | `"google"`     | Direct SRS + Google Cloud TTS       |
| `"srs"`       | `"elevenlabs"` | Direct SRS + ElevenLabs WebSocket   |
| `"srs"`       | `"polly"`      | Direct SRS + AWS Polly              |

Returns: estimated speech time in seconds.

---

#### `HoundTTS.TestTone([freqs], [modulations], [coalition])`

Sends a 2-second 440 Hz sine wave tone directly over SRS, bypassing the TTS engine entirely. Use this to verify the SRS connection is working before debugging TTS issues.

```lua
HoundTTS.TestTone("251.0", "AM", 2)
```

| Argument    | Type   | Default   | Description                |
| ----------- | ------ | --------- | -------------------------- |
| freqs       | string | `"251.0"` | Frequency in MHz           |
| modulations | string | `"AM"`    | `AM` or `FM`               |
| coalition   | number | `0`       | 0=spectator, 1=red, 2=blue |

---

#### `HoundTTS.getSpeechTime(length, [speed], [googleTTS])`

Returns estimated speech duration in seconds. `length` can be a number (character count) or a string (measured automatically).

## Architecture

Same pattern as [DCS-gRPC](https://github.com/DCS-gRPC/rust-server): load the DLL
**before** DCS sanitizes the mission scripting environment.

```
MissionScripting.lua
│
├─ dofile('Scripts/ScriptingSystem.lua')
├─ dofile(lfs.writedir()..'Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua')
│       │
│       ├─ require("HoundTTS")   ← loads HoundTTS.dll (pre-sanitization)
│       ├─ reads optional Config\HoundTTS.lua
│       └─ defines HoundTTS.TextToSpeech / Transmit / TestTone / getSpeechTime / round
│
└─ sanitizeModule('os','io','lfs') + remove require/package
       ↓
   Mission scripts call HoundTTS.TextToSpeech() normally
   (DLL reference captured in upvalue before sanitization)
```

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
├── opus_encoder.*       # libopus streaming encoder
├── audio_queue.h        # Thread-safe Opus frame queue
├── process_launcher.*   # Async CreateProcess wrapper
├── speech_time.h        # Speech time estimation
├── utils.h              # String/path/registry helpers
├── backends/
│   └── srs/
│       ├── srs_backend.*  # Direct SRS backend (TCP/UDP protocol)
│       ├── srs_client.*   # SRS TCP/UDP client, 40ms multimedia timer
│       └── srs_types.h    # FreqMod, ParseFreqMods, GenerateGUID
└── providers/
    ├── piper/
    │   └── piper_tts.*        # CreateProcess piper.exe, PCM→Opus streaming
    ├── sapi/
    │   └── sapi_tts.*         # Windows SAPI 5.4 (ISpVoice, MSVC only; MinGW stub)
    ├── azure/
    │   └── azure_tts.*        # Azure Cognitive Services REST API
    ├── google/
    │   └── google_tts.*       # Google Cloud TTS REST API (OAuth2 service-account JWT via OpenSSL)
    └── elevenlabs/
        └── elevenlabs_tts.*   # ElevenLabs WebSocket streaming API
```

## Acknowledgements

HoundTTS builds on the work of several open-source projects:

- **[DCS-SimpleRadioStandalone (SRS)](https://github.com/ciribob/DCS-SimpleRadioStandalone)** — the SRS TCP/UDP protocol, packet framing, and audio pipeline were studied and adapted to implement the native SRS client in this project.
- **[SkyEye](https://github.com/dharmab/skyeye)** — SkyEye's Go implementation of the SRS client and Opus audio pipeline served as a reference for the direct SRS integration approach used here.
- **[DCS-gRPC](https://github.com/DCS-gRPC/rust-server)** — the pattern of loading a native DLL into the DCS mission Lua state before sanitization (via `MissionScripting.lua`) was pioneered by DCS-gRPC and is followed here.
