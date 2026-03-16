-- HoundTTS mission-side script
-- Loaded via MissionScripting.lua dofile() BEFORE the sanitization block,
-- so package, require, lfs, env are all available here.
--
-- Install: add this line to MissionScripting.lua before the sanitizeModule block:
--   dofile(lfs.writedir()..[[Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua]])

-- Guard: all four globals must be available. On sanitized servers loaded via the
-- hook's pcall(dofile,...), this produces a clear error in dcs.log instead of a
-- silent nil-index crash somewhere deeper in the script.
if not (require and lfs and io and package) then
    env.error("[HoundTTS] Mission scripting environment is sanitized "..
        "(require/lfs/io/package unavailable). HoundTTS cannot load. "..
        "Install DCSServerBot (desanitizes automatically) or add "..
        "dofile(lfs.writedir()..[[Mods\\Services\\HoundTTS\\Scripts\\HoundTTS-mission.lua]]) "..
        "to MissionScripting.lua before the sanitizeModule block.")
    return
end

if not HoundTTS then
    HoundTTS = {}
end

-- -------------------------------------------------------------------------
-- Load optional config from Saved Games\DCS\Config\HoundTTS.lua
-- (mirrors the DCS-gRPC Config\dcs-grpc.lua pattern)
-- -------------------------------------------------------------------------
do
    env.info("[HoundTTS] Checking config at Config\\HoundTTS.lua ...")
    local file, err = io.open(lfs.writedir() .. [[Config\HoundTTS.lua]], "r")
    if file then
        local chunk = file:read("*all")
        file:close()
        local f, loadErr = loadstring(chunk)
        if f then
            setfenv(f, HoundTTS)
            local ok, runErr = pcall(f)
            if ok then
                env.info("[HoundTTS] Config\\HoundTTS.lua loaded successfully")
            else
                env.error("[HoundTTS] Config\\HoundTTS.lua runtime error: " .. tostring(runErr))
            end
        else
            env.error("[HoundTTS] Config\\HoundTTS.lua parse error: " .. tostring(loadErr))
        end
    else
        env.info("[HoundTTS] Config\\HoundTTS.lua not found (" .. tostring(err) .. ") — using defaults")
    end
end

-- -------------------------------------------------------------------------
-- Default settings (filled in after config load with or-pattern)
-- -------------------------------------------------------------------------
HoundTTS.SRS_HOST            = HoundTTS.SRS_HOST            or "127.0.0.1"
HoundTTS.SRS_PORT            = HoundTTS.SRS_PORT            or 5002
HoundTTS.SRS_ENCRYPT         = HoundTTS.SRS_ENCRYPT         or false
HoundTTS.SRS_ENC_KEY         = HoundTTS.SRS_ENC_KEY         or 0
HoundTTS.DEFAULT_TRANSMITTER = HoundTTS.DEFAULT_TRANSMITTER or "srs"
HoundTTS.DEFAULT_PROVIDER    = HoundTTS.DEFAULT_PROVIDER    or "sapi"
HoundTTS.DEFAULT_VOICE       = HoundTTS.DEFAULT_VOICE       or ""
HoundTTS.DEFAULT_SPEAKER     = HoundTTS.DEFAULT_SPEAKER     or ""
HoundTTS.DEFAULT_CULTURE     = HoundTTS.DEFAULT_CULTURE     or "en-US"
HoundTTS.DEFAULT_GENDER      = HoundTTS.DEFAULT_GENDER      or "female"

-- -------------------------------------------------------------------------
-- Load DLL before `require` gets sanitized
-- -------------------------------------------------------------------------
do
    local dllPath = lfs.writedir() .. [[Mods\Services\HoundTTS\bin\]]
    if not string.find(package.cpath, dllPath, 1, true) then
        package.cpath = package.cpath .. ";" .. dllPath .. "?.dll;"
    end
end

local ok, _dll = pcall(require, "HoundTTS")
if not ok then
    env.error("[HoundTTS] Failed to load HoundTTS.dll: " .. tostring(_dll))
    return
end

_dll.init(lfs.writedir())
env.info("[HoundTTS] HoundTTS.dll v" .. (_dll.version or "unknown") .. " loaded, writedir: " .. lfs.writedir())

-- Keep a direct reference to the raw DLL getSpeechTime before the Lua
-- wrapper below overwrites it in the same table (_dll == HoundTTS).
local _dll_getSpeechTime = _dll.getSpeechTime

-- -------------------------------------------------------------------------
-- Public API
-- -------------------------------------------------------------------------
function HoundTTS.round(x, n)
    local p = 10 ^ (n or 0)
    return math.floor(x * p + 0.5) / p
end

function HoundTTS.getSpeechTime(length, speed, providerOrGoogleTTS)
    if type(length) == "string" then length = #length end
    local provider
    if type(providerOrGoogleTTS) == "string" then
        provider = providerOrGoogleTTS
    elseif providerOrGoogleTTS then
        provider = "google"
    else
        provider = "sapi"
    end
    local result = _dll_getSpeechTime(length, speed or 1, provider)
    return result
end

-- -------------------------------------------------------------------------
-- HoundTTS.Transmit(message, transmission_params, provider_params [, translation_params])
--
-- translation_params (table, optional):
--   .provider         "openai" | "google" | "libretranslate" | "aws" | "azure" (omit to skip translation)
--   .language         string  ISO 639-1 target code e.g. "de", "fr", "ru"
--   .source_language  string  ISO 639-1 source code (default: "en")
--
--   When provided, the message is translated BEFORE being sent to the TTS engine.
--   The entire translate → TTS → SRS pipeline runs on a background thread.
--   On translation failure, the original (untranslated) message is spoken.
--
-- transmission_params (table):
--   .transmitter  "srs" (default) | "discord"
--   .freqs        string  e.g. "251.0" or "305.0,127.0"  (also accepts .freq)
--   .modulations  string  e.g. "AM" or "AM,FM"          (also accepts .modulation)
--   .coalition    number  0=spectator, 1=red, 2=blue
--   .name         string  client name shown in SRS
--   .point        DCS Vec3 (optional)
--   .encrypt      bool
--   .encKey       number  0–255
--   .host         string  SRS host override
--   .port         number  SRS port override
--
-- provider_params (table):
--   .provider     "piper" | "azure" | "google" | "elevenlabs" | "aws" | "polly" | "sapi" | "openai" | "kitten"/"kittentts" (deprecated — use "openai")
--   .voice        string  Piper model name OR ElevenLabs/Azure/Google/Polly voice ID
--   .speaker      string  Piper speaker name or integer ID (multi-speaker models only)
--   .culture      string  e.g. "en-US"
--   .gender       string  "male" | "female"
--   .speed        number
--   .engine       string  Polly only: "standard" | "neural" | "long-form" (overrides INI default)
--   .volume       number  0.0–1.0
-- -------------------------------------------------------------------------
function HoundTTS.Transmit(message, transmission_params, provider_params, translation_params)
    local tp = transmission_params or {}
    local ep = provider_params     or {}
    local xl = translation_params  or {}

    local transmitter = tp.transmitter or HoundTTS.DEFAULT_TRANSMITTER
    local freqs       = tostring(tp.freqs       or tp.freq       or "251.0")
    local modulations = tostring(tp.modulations or tp.modulation or "AM")
    local coalition   = tp.coalition or 0
    local name        = tp.name      or "HoundTTS"
    local encrypt     = tp.encrypt   or HoundTTS.SRS_ENCRYPT or false
    local encKey      = tp.encKey    or HoundTTS.SRS_ENC_KEY or 0
    local host        = tp.host      or HoundTTS.SRS_HOST
    local port        = tp.port      or HoundTTS.SRS_PORT

    local lat, lon, alt = 91.0, 181.0, -500.0
    if tp.point and type(tp.point) == "table" and tp.point.x then
        lat, lon, alt = coord.LOtoLL(tp.point)
        lat = HoundTTS.round(lat, 4)
        lon = HoundTTS.round(lon, 4)
        alt = math.ceil(alt)
    end

    local provider = ep.provider or HoundTTS.DEFAULT_PROVIDER
    local voice    = ep.voice    or HoundTTS.DEFAULT_VOICE   or ""
    local speaker  = ep.speaker  or HoundTTS.DEFAULT_SPEAKER or ""
    local culture  = ep.culture  or HoundTTS.DEFAULT_CULTURE or ""
    local gender   = ep.gender   or HoundTTS.DEFAULT_GENDER  or "female"
    local speed    = ep.speed
    local engine   = ep.engine or ""
    local volume      = ep.volume    or tp.volume    or 1.0


    local result = _dll.textToSpeech(
        message,
        {
            transmitter = transmitter,
            host        = host,
            port        = port,
            freqs       = freqs,
            modulations = modulations,
            coalition   = coalition,
            name        = name,
            encrypt     = encrypt,
            encKey      = encKey,
            lat         = lat,
            lon         = lon,
            alt         = alt,
        },
        {
            provider    = provider,
            voice       = voice,
            speaker     = speaker,
            culture     = culture,
            gender      = gender,
            speed       = speed,
            engine      = engine,
            volume      = volume
        },
        {
            provider        = xl.provider or "",
            language        = xl.language or "",
            source_language = xl.source_language or "en",
        }
    )

    return result or _dll_getSpeechTime(message, speed, provider)
end

-- -------------------------------------------------------------------------
-- HoundTTS.TextToSpeech — STTS-compatible drop-in replacement
-- Maps to transmitter="external_audio" for backward compatibility.
-- -------------------------------------------------------------------------
function HoundTTS.TextToSpeech(message, freqs, modulations, volume, name,
                               coalition, point, speed, gender, culture,
                               voice, googleTTS, AzureCreds)
    speed     = speed     or 1
    gender    = gender    or "female"
    culture   = culture   or ""
    voice     = voice     or ""
    volume    = volume    or 1.0
    name      = name      or "HoundTTS"
    coalition = coalition or 0

    local lat, lon, alt = 91.0, 181.0, -500.0
    if point and type(point) == "table" and point.x then
        lat, lon, alt = coord.LOtoLL(point)
        lat = HoundTTS.round(lat, 4)
        lon = HoundTTS.round(lon, 4)
        alt = math.ceil(alt)
    end

    message = message:gsub('"', '\\"')
    local provider = googleTTS and "google" or "sapi"
    if AzureCreds ~= nil then
        provider = "azure"
    end

    local result = _dll.textToSpeech(
        message,
        {
            transmitter = "srs",
            host        = HoundTTS.SRS_HOST,
            port        = HoundTTS.SRS_PORT,
            freqs       = tostring(freqs),
            modulations = tostring(modulations),
            coalition   = coalition,
            name        = name,
            encrypt     = false,
            encKey      = 0,
            lat         = lat,
            lon         = lon,
            alt         = alt,
        },
        {
            provider    = provider,
            voice       = voice,
            speaker     = "",
            culture     = culture,
            gender      = gender,
            speed       = speed,
            volume      = volume
        }
    )

    return result or _dll_getSpeechTime(message, speed, provider)
end

-- -------------------------------------------------------------------------
-- HoundTTS.Translate(message, provider_params, callback)
--
-- message : string — text to translate
--
-- provider_params (table):
--   .provider     "openai" (future: "google")
--   .language         string   ISO 639-1 target code e.g. "de", "fr", "ru" (or full name e.g. "German")
--   .source_language  string   ISO 639-1 source code (default: "en"). LibreTranslate only.
--
-- callback : function(translated, err)
--   Called asynchronously when the translation completes.
--   On success:  callback(translated_string, nil)
--   On failure:  callback(nil, error_string)
--
-- The function returns immediately (non-blocking). The HTTP request to the
-- LLM runs on a DLL background thread. A timer polls for the result every
-- 0.5 seconds and invokes the callback when ready.
-- -------------------------------------------------------------------------
function HoundTTS.Translate(message, provider_params, callback)
    if type(callback) ~= "function" then
        env.error("[HoundTTS] Translate requires a callback function as the 3rd argument")
        return
    end

    local ep = provider_params or {}
    local provider        = ep.provider or "openai"
    local language        = ep.language or "en"
    local source_language = ep.source_language or "en"

    local id, err = _dll.translateAsync(message, {
        provider        = provider,
        language        = language,
        source_language = source_language,
    })

    if id == nil then
        -- Provider validation failed synchronously
        callback(nil, err or "Failed to start translation")
        return
    end

    -- Poll every 0.5s until the background thread finishes
    local function poll(_, t)
        local result, pollErr = _dll.getTranslationResult(id)
        if result ~= nil then
            -- Got a translated string
            callback(result, nil)
            return nil   -- stop polling
        end
        if pollErr ~= nil then
            -- Done with error
            callback(nil, pollErr)
            return nil   -- stop polling
        end
        -- Still pending — check again in 0.5s
        return t + 0.5
    end

    timer.scheduleFunction(poll, nil, timer.getTime() + 0.5)
end

-- -------------------------------------------------------------------------
-- HoundTTS.TestTone([freqs], [modulations], [coalition], [duration], [volume])
-- Sends a 440Hz sine wave over SRS — good for connection testing.
-- duration = duration in seconds (default 2).
-- volume   = volume (default 1.0).
-- -------------------------------------------------------------------------
function HoundTTS.TestTone(freqs, modulations, coalition, duration, volume)
    freqs       = freqs       or "251.0"
    modulations = modulations or "AM"
    coalition   = coalition   or 0
    duration    = duration    or 2.0
    volume      = volume      or 1.0
    _dll.textToSpeech(
        "__test_tone__",
        {
            transmitter = "srs",
            host        = HoundTTS.SRS_HOST,
            port        = HoundTTS.SRS_PORT,
            freqs       = tostring(freqs),
            modulations = tostring(modulations),
            coalition   = coalition,
            name        = "HoundTTS-Test",
            encrypt     = false,
            encKey      = 0,
            lat         = 91.0,
            lon         = 181.0,
            alt         = -500.0,
        },
        {
            provider    = "",
            voice       = "",
            speaker     = "",
            culture     = "",
            gender      = "",
            speed       = duration,
            volume      = volume
        }
    )
end

env.info("[HoundTTS] ready")
