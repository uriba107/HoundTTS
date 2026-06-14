# Adding a TTS Provider

**Reference: EdgeTTS** — the simplest existing provider. No API key, no config dependencies, clean HTTP/WS pattern. Read `src/providers/tts/edge/` alongside this guide.

---

## 1. `src/provider.h` — enum + parse + name

Add a new enumerator to `TtsProvider`, add a parse branch, and add a name branch. Aliases go in `ParseTtsProvider`.

```cpp
enum class TtsProvider {
    // ... existing entries ...
    Foo,
    Unknown
};

inline TtsProvider ParseTtsProvider(const std::string& s) {
    const std::string token = NormalizeProviderToken(s);
    // ... existing checks ...
    if (token == "foo" || token == "fooTTS") return TtsProvider::Foo;
    return TtsProvider::Unknown;
}

inline const char* TtsProviderName(TtsProvider p) {
    switch (p) {
        // ... existing cases ...
        case TtsProvider::Foo: return "foo";
        default:              return "unknown";
    }
}
```

## 2. `src/providers/tts/foo/` — provider implementation

Create `foo_tts.h` and `foo_tts.cpp`. Use `PCMQueue&` (non-const ref, 16kHz mono).

> **Synchronous** (like SapiTTS): return `std::vector<int16_t>` and push + MarkDone() in the dispatch branch.
> **Asynchronous** (all others): accept `PCMQueue&` parameter and call `queue.Push()` + `queue.MarkDone()` inside.

```cpp
// foo_tts.h
#pragma once
#include "backends/pcm_queue.h"
#include <string>

namespace HoundTTS {

class FooTTS {
public:
    static bool SynthesizeToQueue(
        const std::string& text,
        // provider-specific params (voicename, keys, etc.)
        PCMQueue& queue);
};

} // namespace HoundTTS
```

## 3. `src/config_reader.h` + `src/config_reader.cpp` — credentials (if needed)

**`config_reader.h`:**

Add a private member and public getter:

```cpp
// In public section:
std::string GetFooKey() const;

// In private section:
std::string fooKey_;
```

Add reset in `Load()`:
```cpp
fooKey_.clear();
```

**`config_reader.cpp`:**

Add parse branch in `ParseIni`:
```cpp
} else if (section == "Foo") {
    if (key == "api_key") fooKey_ = val;
}
```

Add getter:
```cpp
std::string ConfigReader::GetFooKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fooKey_;
}
```

## 4. `src/tts_pipeline.cpp` — dispatch

Add `#include` at top:

```cpp
#include "providers/tts/foo/foo_tts.h"
```

Add dispatch branch in `TTSPipeline::Produce` after existing ones:

```cpp
} else if (provider == TtsProvider::Foo) {
    std::string fooKey = cfg.GetFooKey();
    std::thread([message, fooKey, voice, speed, volume, dispatchQueue, finalizeCache]() {
        bool ok = FooTTS::SynthesizeToQueue(message, fooKey, voice, speed, volume, *dispatchQueue);
        finalizeCache(ok);
    }).detach();
```

Key patterns visible in every existing branch:
- Capture `dispatchQueue` (shared_ptr) and `finalizeCache` (lambda) by value
- Call `finalizeCache(ok)` after synthesis
- Use `std::thread(...).detach()` for async; inline + `dispatchQueue->MarkDone()` for sync
- `cfg` is `ConfigReader::Instance()` already captured above

## 5. `CMakeLists.txt` — source registration

Add your `.cpp` file to the `add_library(HoundTTS SHARED ...)` block:

```cmake
# TTS providers
src/providers/tts/foo/foo_tts.cpp
```

Keep entries alphabetically grouped within the TTS providers section (lines 80–92).

## 6. `README.md` — user documentation

Three places to update:

1. **Provider section** — add a new heading between existing ones (alpha order), with example `Transmit()` call and any quirks.

2. **Provider routing table** (around line 599) — add a row:
```markdown
| `"srs"`       | `"foo"`       | Direct SRS + Foo TTS |
```

3. **`provider_params` table** (around line 578) — add the provider name to the accepted values list.

## 7. `dcs/Config/HoundTTS-credentials.ini.example` — config template

If your provider needs an INI section, add it:

```ini
[Foo]
; API key for Foo TTS
api_key =
```

## 8. `src/providers/tts/AGENTS.md` — ownership + aliases

Add a row to the Ownership table and a row to the Lua aliases table in the file you're reading now.

---

## Testing

Build via `build.bat` (MSVC Docker), deploy to Saved Games, then test from DCS:

```lua
HoundTTS.Transmit("Test message",
    { freqs = "251.0", coalition = 2 },
    { provider = "foo" }
)
```

Check `Logs\HoundTTS.log` in Saved Games for provider errors.
