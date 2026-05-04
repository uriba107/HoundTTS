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

-- Internal: returns true if obj is a live DCS Unit instance.
-- TODO: use in Transmit/TransmitNoise to auto-extract LatLngAlt from a Unit
--       passed in the 'point' field, replacing the manual getPoint() call.
local function isDcsUnit(obj)
    if type(obj) ~= "table" then return false end
    return getmetatable(obj) == Unit
end

--- check if object is DCS static object
-- @param obj DCS Object canidate
-- @return[type=Bool] True if object is static object
local function isStaticObject(obj)
    if type(obj) ~= "table" then return false end
    return getmetatable(obj) == StaticObject
end

local function isPoint(point)
    if type(point) ~= "table" then return false end
    return (type(point.x) == "number") and (type(point.z) == "number")
end

local function getTransmitterPos(dcsObject)
    if dcsObject == nil then return nil end
    if dcsObject ~= nil and (dcsObject:isExist() == false or dcsObject:getLife() < 1) then
        return nil
    end
    local pos = dcsObject:getPoint()
    local transmitterObjectCat, transmitterSubCat = dcsObject:getCategory()
    if transmitterObjectCat == Object.Category.STATIC or (transmitterObjectCat == Object.Category.UNIT and transmitterSubCat == Unit.Category.GROUND_UNIT) then
        local verticalOffset = (dcsObject:getDesc()["box"]["max"]["y"] + 5) or 20
        pos.y = pos.y + verticalOffset
    end
    return pos
end

local function getCoords(point)
    local lat, lon, alt = 91.0, 181.0, -500.0
    if isPoint(point) then
        lat, lon, alt = coord.LOtoLL(point)
        lat = HoundTTS.round(lat, 4)
        lon = HoundTTS.round(lon, 4)
        alt = math.ceil(alt)
    end
    return lat, lon, alt
end

local tracked_sessions = {}
local function addTrackingSession(sessionId,dcsObject)
    if sessionId and not tracked_sessions[sessionId] then
        tracked_sessions[sessionId] = dcsObject
    end
end
local function removeTrackingSession(sessionId,killSession)
    if killSession == true then
        HoundTTS.KillSession(sessionId)
    end
    tracked_sessions[sessionId] = nil
end

local function updateTrackedSessions(_,time)
    local toRemove = {}
    for sessionId, dcsObject in pairs(tracked_sessions) do
        if isDcsUnit(dcsObject) or isStaticObject(dcsObject) then
            if dcsObject:isExist() and dcsObject:getLife() >= 1 then
                local alive = HoundTTS.UpdateSession(sessionId,{point = isDcsUnit(dcsObject) and getTransmitterPos(dcsObject) or nil})
                if not alive then
                    table.insert(toRemove, {sessionId, true})
                end
            else
                table.insert(toRemove, {sessionId, true})
            end
        else
            table.insert(toRemove, sessionId)
        end
    end
    for _, v in ipairs(toRemove) do
        if type(v) == "table" then
            removeTrackingSession(v[1], v[2])
        else
            removeTrackingSession(v)
        end
    end
    return time + 0.5
end

timer.scheduleFunction(updateTrackedSessions, nil, timer.getTime() + 0.5)

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
--   .provider     "piper" | "azure" | "google" | "elevenlabs" | "aws" | "polly" | "sapi" | "openai"
--   .voice        string  Piper model name OR ElevenLabs/Azure/Google/Polly voice ID
--   .speaker      string  Piper speaker name or integer ID (multi-speaker models only)
--   .culture      string  e.g. "en-US"
--   .gender       string  "male" | "female"
--   .speed        number
--   .engine       string  Polly only: "standard" | "neural" | "long-form" (overrides INI default)
--   .volume       number  0.0–1.0
--
-- Returns: speechTime (number), sessionId (string)
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

    if not isPoint(tp.point) and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        tp.point = getTransmitterPos(tp.dcsObject)
    end
    local lat, lon, alt = getCoords(tp.point)

    local provider = ep.provider or HoundTTS.DEFAULT_PROVIDER
    local voice    = ep.voice    or HoundTTS.DEFAULT_VOICE   or ""
    local speaker  = ep.speaker  or HoundTTS.DEFAULT_SPEAKER or ""
    local culture  = ep.culture  or HoundTTS.DEFAULT_CULTURE or ""
    local gender   = ep.gender   or HoundTTS.DEFAULT_GENDER  or "female"
    local speed    = ep.speed
    local engine   = ep.engine or ""
    local volume   = tp.volume or ep.volume or 1.0  -- prefer transmission_params


    local result, sessionId = _dll.textToSpeech(
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

    if sessionId and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        addTrackingSession(sessionId,tp.dcsObject)
    end
    return result or _dll_getSpeechTime(message, speed, provider), sessionId
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

    local lat, lon, alt = getCoords(point)

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
-- HoundTTS.TransmitNoise(transmission_params, provider_params)
--
-- Starts a continuous noise jammer transmission.
-- Returns a sessionId string that can be passed to UpdateSession / KillSession.
--
-- transmission_params (table) — same shape as Transmit:
--   .transmitter  "srs" (default)
--   .freqs        string  e.g. "251.0" or "305.0,127.0"  (also .freq)
--   .modulations  string  e.g. "AM" or "AM,FM"           (also .modulation)
--   .coalition    number  0=spectator, 1=red, 2=blue
--   .name         string  client name shown in SRS
--   .point        DCS Vec3 (optional) — initial transmitter position
--   .encrypt      bool
--   .encKey       number  0–255
--   .host         string  SRS host override
--   .port         number  SRS port override
--
-- provider_params (table):
--   .noiseType    "white" (default) | "chirp" | "harsh" | "jam"
--   .volume       number  0.0–1.0  (default 1.0)
--   .seed         number  RNG seed (optional, auto-generated if absent)
-- -------------------------------------------------------------------------
function HoundTTS.TransmitNoise(transmission_params, provider_params)
    local tp = transmission_params or {}
    local ep = provider_params     or {}

    local transmitter = tp.transmitter or HoundTTS.DEFAULT_TRANSMITTER
    local freqs       = tostring(tp.freqs       or tp.freq       or "251.0")
    local modulations = tostring(tp.modulations or tp.modulation or "AM")
    local coalition   = tp.coalition or 0
    local name        = tp.name      or "HoundTTS-Jammer"
    local encrypt     = tp.encrypt   or HoundTTS.SRS_ENCRYPT or false
    local encKey      = tp.encKey    or HoundTTS.SRS_ENC_KEY or 0
    local host        = tp.host      or HoundTTS.SRS_HOST
    local port        = tp.port      or HoundTTS.SRS_PORT

    if not isPoint(tp.point) and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        tp.point = getTransmitterPos(tp.dcsObject)
    end

    local lat, lon, alt = getCoords(tp.point)

    local noiseType = ep.noiseType or "white"
    local volume    = tp.volume or ep.volume or 1.0  -- prefer transmission_params
    local duration  = ep.duration  or 600         -- <=0 means continuous
    local noiseParams = { noiseType = noiseType, volume = volume, duration = duration }
    if ep.seed then noiseParams.seed = ep.seed end

    local sessionId = _dll.startNoise(
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
        noiseParams
    )
    if sessionId and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        addTrackingSession(sessionId,tp.dcsObject)
    end
    return sessionId
end

-- -------------------------------------------------------------------------
-- HoundTTS.UpdateSession(sessionId, update_params)
--
-- Updates the position of a live transmission (TTS or noise).
-- Position updates are SRS-specific and trigger an immediate TCP re-sync.
-- Can be called as frequently as needed (e.g. every 0.5s from a scheduler).
--
-- sessionId    : string — returned by Transmit / TransmitNoise / TransmitTone
-- update_params (table):
--   .point      DCS Vec3 (optional) — new transmitter position
--
-- Returns true if the session is still alive (transmission ongoing).
-- Returns false if the session was not found OR the transmission has ended
-- naturally (e.g. noise duration elapsed, TTS finished). Use this to break
-- out of a position-update scheduler loop without sending a kill command:
--
--   local function track(_, t)
--       if not HoundTTS.UpdateSession(id, { point = unit:getPoint() }) then
--           return nil  -- stop polling
--       end
--       return t + 0.5
--   end
--   timer.scheduleFunction(track, nil, timer.getTime() + 0.5)
-- -------------------------------------------------------------------------
function HoundTTS.UpdateSession(sessionId, update_params)
    if not sessionId then return false end
    local up = update_params or {}

    local updateTbl = {}
    if isPoint(up.point) then
        local lat, lon, alt = getCoords(up.point)
        updateTbl.lat = lat
        updateTbl.lon = lon
        updateTbl.alt = alt
    end

    -- _dll.updateSession returns:
    --   true  — session found and still alive
    --   false — session found but transmission has ended
    --   nil   — session not found
    -- The documented contract above promises false for "not found", so map nil→false.
    local ok = _dll.updateSession(sessionId, updateTbl)
    return ok or false
end

-- -------------------------------------------------------------------------
-- HoundTTS.KillSession(sessionId)
--
-- Stops a live transmission (TTS or noise) identified by sessionId.
-- For noise jammers this terminates the noise loop immediately.
-- For TTS transmissions it signals an early stop.
--
-- Returns true if the session was found and killed, false otherwise.
-- -------------------------------------------------------------------------
function HoundTTS.KillSession(sessionId)
    if not sessionId then return false end
    local result = _dll.updateSession(sessionId, { alive = false })
    -- _dll.updateSession returns the alive state (false after kill) or nil
    -- if session not found.  Any non-nil return means the session existed.
    return result ~= nil
end

-- -------------------------------------------------------------------------
-- HoundTTS.KillAllSessions()
--
-- Stops every live transmission.  Returns the number of sessions killed.
-- -------------------------------------------------------------------------
function HoundTTS.KillAllSessions()
    tracked_sessions = {}
    return _dll.killAllSessions()
end

-- -------------------------------------------------------------------------
-- HoundTTS.TransmitTone(transmission_params, provider_params)
--
-- Transmits a fixed-frequency sine-wave tone over SRS.
-- Returns a sessionId (tone is finite but can be killed early with KillSession).
--
-- transmission_params (table) — same shape as Transmit / TransmitNoise:
--   .transmitter  "srs" (default)
--   .freqs        string  e.g. "251.0"   (also .freq)
--   .modulations  string  e.g. "AM"      (also .modulation)
--   .coalition    number  0=spectator, 1=red, 2=blue
--   .name         string  client name shown in SRS
--   .point        DCS Vec3 (optional) — transmitter position
--   .encrypt      bool
--   .encKey       number  0–255
--   .host / .port override SRS address
--
-- provider_params (table):
--   .duration  number  seconds (default 2.0)
--   .freqHz    number  Hz (default 440.0)
--   .volume    number  0.0–1.0 (default 1.0)
-- -------------------------------------------------------------------------
function HoundTTS.TransmitTone(transmission_params, provider_params)
    local tp = transmission_params or {}
    local ep = provider_params     or {}

    local transmitter = tp.transmitter or HoundTTS.DEFAULT_TRANSMITTER
    local freqs       = tostring(tp.freqs       or tp.freq       or "251.0")
    local modulations = tostring(tp.modulations or tp.modulation or "AM")
    local coalition   = tp.coalition or 0
    local name        = tp.name      or "HoundTTS-Tone"
    local encrypt     = tp.encrypt   or HoundTTS.SRS_ENCRYPT or false
    local encKey      = tp.encKey    or HoundTTS.SRS_ENC_KEY or 0
    local host        = tp.host      or HoundTTS.SRS_HOST
    local port        = tp.port      or HoundTTS.SRS_PORT

    if not isPoint(tp.point) and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        tp.point = getTransmitterPos(tp.dcsObject)
    end
    local lat, lon, alt = getCoords(tp.point)

    local duration = ep.duration or tp.duration or 2.0
    local freqHz   = math.max(20.0, math.min(20000.0, ep.freqHz or 440.0))
    local volume   = tp.volume or ep.volume or 1.0  -- prefer transmission_params

    local sessionId = _dll.startTone(
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
        { duration = duration, freqHz = freqHz, volume = volume }
    )
    if sessionId and (isDcsUnit(tp.dcsObject) or isStaticObject(tp.dcsObject)) then
        addTrackingSession(sessionId,tp.dcsObject)
    end
    return sessionId
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

    HoundTTS.TransmitTone(
        {
            freqs       = tostring(freqs),
            modulations = tostring(modulations),
            coalition   = coalition,
            name        = "HoundTTS-Test",
        },
        { duration = duration, volume = volume }
    )
end

-- Kill all active sessions on mission end to prevent jammers surviving a restart
local missionEndHandler = {}
function missionEndHandler:onEvent(event)
    if event.id == world.event.S_EVENT_MISSION_END then
        env.info("[HoundTTS] mission end — killing all sessions")
        HoundTTS.KillAllSessions()
    end
end
world.addEventHandler(missionEndHandler)

env.info("[HoundTTS] ready")
