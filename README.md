# HoundTTS

A native C++ DLL replacement for [DCS-SimpleTextToSpeech](https://github.com/ciribob/DCS-SimpleTextToSpeech) that integrates with DCS World as a Lua extension.

## Benefits over the Lua script

- **No PowerShell overhead** — connects natively to SRS via direct TCP/UDP protocol
- **No focus stealing** — all TTS synthesis runs in background threads, no visible windows
- **Parallel calls** — each TTS request is fire-and-forget, no blocking
- **Multiple TTS providers** — Piper (offline, bundled), SAPI (Windows system voices, no API key), Azure, Google, ElevenLabs, OpenAI (and compatible APIs like LocalAI, Kitten TTS Server)
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

Each release includes two archives:

- **`HoundTTS-windows.zip`** — the full working package. Contains the DLL, all Lua scripts, and config examples. **This is all you need to get started.**
- **`HoundTTS-piper-addon-windows.zip`** — an optional add-on, as the name states. Contains `piper.dll` (built from [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl), GPLv3), `onnxruntime.dll`, `espeak-ng-data/`, and bundled voice models (~150 MB). Install this in addition to the base package if you want to use Piper TTS. The DLL keeps voice models loaded between calls, eliminating per-call cold-start latency. On future updates you only need to re-download the base package — the piper add-on stays valid unless Piper itself is updated.

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
│   │       ├── piper.dll             ← in-process TTS engine (GPLv3)
│   │       ├── onnxruntime.dll
│   │       └── espeak-ng-data\
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

### Piper (offline, bundled)

**No internet or API key required.** Synthesizes speech in-process via `piper.dll` (built from [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl), GPLv3). Voice models stay loaded between calls — no per-call cold-start. Two bundled voice models and all dependencies are included in `HoundTTS-piper-addon-windows.zip`.

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
DEFAULT_PROVIDER     = "sapi"       -- "piper" | "sapi" | "azure" | "google" | "elevenlabs" | "openai" | "aws" | "polly" | "kitten"
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
; Leave blank to use bundled bin\piper\ folder (from the piper-addon package)
path =
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

[OpenAI]
; OpenAI API key (optional for local deployments)
api_key =
; Base URL — /v1/audio/speech is always appended
endpoint = https://api.openai.com
; Model name
model = tts-1
; Chat model for translation
chat_model = gpt-4o-mini

[KittenTTS]
; Full base URL of your Kitten TTS Server instance (https://github.com/uriba107/Kitten-TTS-Server)
; Include scheme and port, no trailing slash.
; Example: endpoint = http://192.168.10.30:8005
endpoint =
```

## Usage (in mission scripts)

HoundTTS provides two APIs:

- **`TextToSpeech`** — drop-in replacement for STTS. Identical signature, transmits directly over SRS
- **`Transmit`** — flexible API with named parameter tables. Supports all TTS providers, encryption, position.
- **`Translate`** — translate text via OpenAI chat models. Preserves military aviation brevity codes.

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

-- Translate text (async — does not block DCS)
HoundTTS.Translate("Bogey, BRAA 270 for 15, angels 20, hostile",
    { provider = "openai", language = "de" },
    function(translated, err)
        if translated then
            trigger.action.outText("Translation: " .. translated, 10)
        end
    end)

-- Translate and transmit (async)
HoundTTS.Translate("Two bandits, BRA 090 for 30, angels 15",
    { language = "fr" },
    function(msg, err)
        if msg then
            HoundTTS.Transmit(msg,
                { freqs = "251.0", coalition = 2, name = "GCI" },
                { provider = "openai", voice = "nova" })
        end
    end)
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

| Field    | Type   | Default                     | Description                                                                                                                                                           |
| -------- | ------ | --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| provider | string | `HoundTTS.DEFAULT_PROVIDER` | `"piper"` / `"sapi"` (`"win"`) / `"azure"` / `"google"` (`"gcloud"`) / `"elevenlabs"` / `"openai"` / `"polly"` (`"aws"`) / `"kitten"` (`"kittentts"`, `"kitten_tts"`) |
| voice    | string | `HoundTTS.DEFAULT_VOICE`    | Piper model name, SAPI voice name, Azure/Google/Polly voice name, or ElevenLabs voice ID                                                                              |
| speaker  | string | `""`                        | Piper multi-speaker model: speaker name or numeric ID                                                                                                                 |
| engine   | string | `"standard"`                | Polly engine: `"standard"` / `"neural"` / `"generative"`                                                                                                              |
| culture  | string | `HoundTTS.DEFAULT_CULTURE`  | BCP-47 locale e.g. `"en-US"`, `"en-GB"` (used by SAPI, Azure, Google)                                                                                                 |
| gender   | string | `HoundTTS.DEFAULT_GENDER`   | `"male"` / `"female"` (used by SAPI, Google)                                                                                                                          |
| speed    | number | `1.0`                       | Speech rate (0.5 = half speed, 1.0 = normal, 2.0 = double speed)                                                                                                      |
| volume   | number | `1.0`                       | Output level: 0.0 = silence, 1.0 = full volume                                                                                                                        |

**Provider routing:**

| `transmitter` | `provider`     | Engine used                                                                           |
| ------------- | -------------- | ------------------------------------------------------------------------------------- |
| `"srs"`       | `"piper"`      | Direct SRS + Piper TTS (offline)                                                      |
| `"srs"`       | `"sapi"`       | Direct SRS + Windows SAPI 5.4 (offline)                                               |
| `"srs"`       | `"azure"`      | Direct SRS + Azure Cognitive Speech                                                   |
| `"srs"`       | `"google"`     | Direct SRS + Google Cloud TTS                                                         |
| `"srs"`       | `"elevenlabs"` | Direct SRS + ElevenLabs WebSocket                                                     |
| `"srs"`       | `"aws"`        | Direct SRS + AWS Polly (`"polly"` alias)                                              |
| `"srs"`       | `"openai"`     | Direct SRS + OpenAI / LocalAI / Kitten TTS Server HTTP REST                           |
| `"srs"`       | `"kitten"`     | ⚠️ Deprecated — reroutes through OpenAI-compat endpoint; will be removed next release |

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

#### `HoundTTS.Translate(message, provider_params, callback)`

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
    ├── shared/
    │   └── google/
    │       └── google_auth.*       # Shared Google OAuth2 JWT auth (used by TTS + Translate)
    ├── tts/
    │   ├── azure/
    │   │   └── azure_tts.*        # Azure Cognitive Services REST API
    │   ├── elevenlabs/
    │   │   └── elevenlabs_tts.*   # ElevenLabs WebSocket streaming API
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
    │   └── sapi/
    │       └── sapi_tts.*         # Windows SAPI 5.4 (ISpVoice, MSVC only)
    └── translate/
        ├── google/
        │   └── google_translate.*      # Google Cloud Translation API v2
        ├── libretranslate/
        │   └── libretranslate.*        # LibreTranslate REST API (self-hosted)
        └── openai/
            └── openai_chat.*           # OpenAI chat completions API (translation)
```

## Troubleshooting

### Breaking Changes

#### 0.2.x → 0.3.x (upcoming)

**Kitten TTS dedicated provider removed**

The `"kitten"` / `"kittentts"` / `"kitten_tts"` provider names will be removed. In the current release they still work and log a deprecation warning in `Logs\HoundTTS.log`.

**Action required:** migrate to `provider = "openai"` and point `[OpenAI] endpoint` at your Kitten TTS Server — see the [Kitten TTS section](#kitten-tts-self-hosted--deprecated) above for step-by-step instructions.

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

### Docker (recommended)

A pipeline is available, invoked via the PowerShell script:

#### Windows containers (MSVC — full feature set including SAPI)

Requires Docker Desktop in **Windows containers** mode.

```powershell
.\build-docker.ps1
```

Builds with MSVC + Windows 10 SDK (19041) inside a Windows Server Core container. All providers, including SAPI 5.4, are available.

The pipeline produces the following `dist\` layout:

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

## Acknowledgements

HoundTTS builds on the work of several open-source projects:

- **[DCS-SimpleRadioStandalone (SRS)](https://github.com/ciribob/DCS-SimpleRadioStandalone)** — the SRS TCP/UDP protocol, packet framing, and audio pipeline were studied and adapted to implement the native SRS client in this project.
- **[SkyEye](https://github.com/dharmab/skyeye)** — SkyEye's Go implementation of the SRS client and Opus audio pipeline served as a reference for the direct SRS integration approach used here.
- **[DCS-gRPC](https://github.com/DCS-gRPC/rust-server)** — the pattern of loading a native DLL into the DCS mission Lua state before sanitization (via `MissionScripting.lua`) was pioneered by DCS-gRPC and is followed here.
- **[@Applevangelist](https://github.com/Applevangelist)** — for adopting HoundTTS into [MOOSE](https://github.com/FlightControl-Master/MOOSE) and providing much needed guidance.
