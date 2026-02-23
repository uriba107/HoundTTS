-- HoundTTS Provider Test Script
-- Include in a mission via a DO SCRIPT FILE trigger to verify all TTS providers.
--
-- Prerequisites:
--   1. HoundTTS-mission.lua must be loaded (via MissionScripting.lua dofile line).
--   2. HoundTTS-credentials.ini must be populated with credentials for each
--      cloud provider you want to test.
--   3. SRS must be running and connected on the configured host/port.
--
-- Usage:
--   Add a "Mission Start" trigger → "DO SCRIPT FILE" → select this file.
--   Each test fires with a GAP-second gap so you can hear them individually.
--   Listen on 251.000 AM (blue coalition) in SRS.
--
-- Frequencies used: all tests transmit on 251.000 AM, coalition 2 (blue).
-- Change TEST_FREQ / TEST_MOD / TEST_COALITION below to suit your mission.

local TEST_FREQ      = "251.0"
local TEST_MOD       = "AM"
local TEST_COALITION = 2
local GAP            = 12   -- seconds between tests

-- Helper: schedule a function call after `delay` seconds
local function after(delay, fn)
    timer.scheduleFunction(fn, nil, timer.getTime() + delay)
end

-- Helper: fire one Transmit call and log it
local function runTest(label, message, tp, ep)
    env.info("[HoundTTS-test] " .. label)
    local ok, err = pcall(function()
        HoundTTS.Transmit(message, tp, ep)
    end)
    if not ok then
        env.error("[HoundTTS-test] " .. label .. " FAILED: " .. tostring(err))
    end
end

-- Helper: fire one TextToSpeech call and log it
local function runTestTTS(label, ...)
    env.info("[HoundTTS-test] " .. label)
    local args = {...}
    local ok, ret = pcall(function()
        return HoundTTS.TextToSpeech(unpack(args))
    end)
    if not ok then
        env.error("[HoundTTS-test] " .. label .. " FAILED: " .. tostring(ret))
    elseif type(ret) ~= "number" then
        env.error("[HoundTTS-test] " .. label .. " bad return type: " .. type(ret))
    else
        env.info("[HoundTTS-test] " .. label .. " OK, speechTime=" .. tostring(ret))
    end
end

-- Base transmission params shared by all tests
local function baseTP(name_suffix)
    return {
        freqs      = TEST_FREQ,
        modulations= TEST_MOD,
        coalition  = TEST_COALITION,
        name       = "HoundTTS-Test-" .. (name_suffix or ""),
        volume     = 1.0,
    }
end

-- ============================================================
-- Phase 1: Transmit API tests
-- Each entry: { label, message, tp_name_or_table, ep, [volume=N] }
-- tp_name_or_table: string → passed to baseTP(), table → used directly
-- Special entries: { type = "tone" } for test tone
-- ============================================================
local tests = {
    -- Test tone (confirms SRS connection before any provider runs)
    { type = "tone" },

    -- SAPI (Windows built-in TTS, no credentials needed)
    { "SAPI default voice",
      "SAPI default voice. Windows built-in text to speech.",
      "SAPI",
      { provider = "sapi", culture = "en-US", gender = "female", speed = 1.0 } },

    { "SAPI male voice",
      "SAPI male voice at higher speed.",
      "SAPI-male",
      { provider = "sapi", culture = "en-US", gender = "male", speed = 1.3 } },

    { "SAPI low volume (0.25)",
      "SAPI at quarter volume. This should be noticeably quieter.",
      "SAPI-quiet",
      { provider = "sapi", culture = "en-US", gender = "female", speed = 1.0 },
      volume = 0.25 },

    -- Piper (local offline TTS, no credentials needed)
    -- Requires the piper-addon package. Voice is .onnx model filename in voices\ folder.
    { "Piper en_US-lessac-low",
      "Piper offline TTS. Lessac low quality model.",
      "Piper-lessac",
      { provider = "piper", voice = "en_US-lessac-low", culture = "en-US", speed = 1.0 } },

    { "Piper en_US-ryan-low",
      "Piper offline TTS. Ryan low quality model.",
      "Piper-ryan",
      { provider = "piper", voice = "en_US-ryan-low", culture = "en-US", speed = 1.0 } },

    { "Piper speed 1.4",
      "Piper at one point four times normal speed.",
      "Piper-fast",
      { provider = "piper", voice = "en_US-lessac-low", culture = "en-US", speed = 1.4 } },

    { "Piper low volume (0.25)",
      "Piper at quarter volume. This should be noticeably quieter.",
      "Piper-quiet",
      { provider = "piper", voice = "en_US-lessac-low", culture = "en-US", speed = 1.0 },
      volume = 0.25 },

    -- Azure Cognitive Services Speech
    -- Requires [Azure] key + region in HoundTTS-credentials.ini.
    { "Azure en-US-AriaNeural",
      "Azure Cognitive Services. Aria Neural voice.",
      "Azure-Aria",
      { provider = "azure", voice = "en-US-AriaNeural", culture = "en-US", gender = "female", speed = 1.0 } },

    { "Azure en-US-DavisNeural male",
      "Azure Cognitive Services. Davis Neural voice.",
      "Azure-Davis",
      { provider = "azure", voice = "en-US-DavisNeural", culture = "en-US", gender = "male", speed = 1.0 } },

    { "Azure SSML with prosody",
      '<speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis" xml:lang="en-US">'
      .. '<voice name="en-US-AriaNeural">'
      .. '<prosody rate="+20%" pitch="+5%">Azure SSML with prosody tags.</prosody>'
      .. '</voice></speak>',
      "Azure-SSML",
      { provider = "azure", voice = "en-US-AriaNeural", culture = "en-US", speed = 1.0 } },

    -- Google Cloud Text-to-Speech
    -- Requires [Google] credentials_file in HoundTTS-credentials.ini.
    { "Google en-GB-Standard-A female",
      "Google Cloud text to speech. British English female voice.",
      "Google-enGB-A",
      { provider = "google", voice = "en-GB-Standard-A", culture = "en-GB", gender = "female", speed = 1.0 } },

    { "Google en-US-Standard-D male",
      "Google Cloud text to speech. American English male voice.",
      "Google-enUS-D",
      { provider = "google", voice = "en-US-Standard-D", culture = "en-US", gender = "male", speed = 1.0 } },

    { "Google auto-select by culture+gender",
      "Google auto-selected voice from culture and gender only.",
      "Google-auto",
      { provider = "google", culture = "en-US", gender = "female", speed = 1.0 } },

    -- ElevenLabs
    -- Requires [ElevenLabs] api_key in HoundTTS-credentials.ini.
    -- Free-tier voices: Aria=9BWtsMINqrJLrRacOk9x, Brian=nPczCjzI2devNBz1zQrb
    { "ElevenLabs Aria (free tier)",
      "ElevenLabs Aria voice.",
      "ElevenLabs-Aria",
      { provider = "elevenlabs", voice = "9BWtsMINqrJLrRacOk9x", speed = 1.0 } },

    { "ElevenLabs Brian (free tier male)",
      "ElevenLabs Brian voice.",
      "ElevenLabs-Brian",
      { provider = "elevenlabs", voice = "nPczCjzI2devNBz1zQrb", speed = 1.0 } },

    -- Amazon Polly
    -- Requires [Polly] access_key + secret_key + region in HoundTTS-credentials.ini.
    -- engine = "standard" is free tier (5M chars/month for 12 months); "neural" is paid.
    { "Polly Joanna standard",
      "Amazon Polly. Joanna standard voice.",
      "Polly-Joanna",
      { provider = "polly", voice = "Joanna", culture = "en-US", gender = "female", speed = 1.0, engine = "standard" } },

    { "Polly Matthew standard",
      "Amazon Polly. Matthew standard voice.",
      "Polly-Matthew",
      { provider = "polly", voice = "Matthew", culture = "en-US", gender = "male", speed = 1.0, engine = "standard" } },

    { "Polly auto-select by culture+gender",
      "Amazon Polly auto-selected voice from culture and gender.",
      "Polly-auto",
      { provider = "polly", culture = "en-US", gender = "female", speed = 1.0, engine = "standard" } },

    -- Multi-frequency transmission
    { "Multi-frequency (251 + 305 AM)",
      "Transmitting on two frequencies at once.",
      { freqs = "251.0,305.0", modulations = "AM,AM", coalition = TEST_COALITION,
        name = "HoundTTS-Test-MultiFreq", volume = 1.0 },
      { provider = "sapi", culture = "en-US", gender = "female", speed = 1.0 } },

    -- Speed extremes
    { "SAPI slow (0.6x)",
      "Slow speed at zero point six.",
      "SAPI-slow",
      { provider = "sapi", culture = "en-US", gender = "female", speed = 0.6 } },

    { "Piper fast (1.8x)",
      "Fast speed at one point eight.",
      "Piper-vfast",
      { provider = "piper", voice = "en_US-lessac-low", culture = "en-US", speed = 1.8 } },
}

-- Schedule Phase 1 tests
for i, t in ipairs(tests) do
    after(1 + GAP * (i - 1), function()
        if t.type == "tone" then
            env.info("[HoundTTS-test] " .. i .. ": Test tone")
            local ok, err = pcall(function()
                HoundTTS.TestTone(TEST_FREQ, TEST_MOD, TEST_COALITION)
            end)
            if not ok then
                env.error("[HoundTTS-test] TestTone FAILED: " .. tostring(err))
            end
        else
            local tp = type(t[3]) == "string" and baseTP(t[3]) or t[3]
            if t.volume then tp.volume = t.volume end
            runTest(i .. ": " .. t[1], "Test " .. i .. ". " .. t[2], tp, t[4])
        end
        return nil
    end)
end

-- ============================================================
-- Phase 2: TextToSpeech — STTS backward-compatibility tests
--
-- These tests exercise HoundTTS.TextToSpeech(), which is the
-- drop-in replacement for STTS.TextToSpeech().  The signature
-- is identical to the original STTS script:
--
--   TextToSpeech(message, freqs, modulations, volume, name,
--                coalition, [point], [speed], [gender],
--                [culture], [voice], [googleTTS], [AzureCreds])
--
-- Provider routing (mirrors STTS behaviour):
--   googleTTS=true            → google provider
--   AzureCreds ~= nil         → azure provider
--   (default / googleTTS=false) → sapi provider
--
-- Each entry: { label, message, freq, mod, vol, name, coal, ... }
-- Special entries: { type = "getSpeechTime" }
-- ============================================================
local phase1_offset = 1 + GAP * #tests

local testsTTS = {
    -- Minimal call — required args only, all optionals omitted
    { "minimal required args",
      "TextToSpeech minimal call. SAPI default voice.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION },

    -- All optional args explicitly nil (same as omitting them)
    { "all optionals nil",
      "TextToSpeech with all optional arguments set to nil.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, nil, nil, nil, nil, nil, nil },

    -- Speed, gender, culture passthrough → SAPI
    { "speed+gender+culture → SAPI",
      "TextToSpeech with speed, gender, and culture.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, 1.2, "male", "en-US" },

    -- Explicit voice name → SAPI voice selection
    { "explicit voice name → SAPI",
      "TextToSpeech with explicit voice name.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, 1.0, "female", "en-US", "Zira" },

    -- googleTTS=false (explicit) → SAPI (same as default)
    { "googleTTS=false → SAPI",
      "TextToSpeech with googleTTS false. Should use SAPI.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, 1.0, "female", "en-US", "", false },

    -- googleTTS=true → Google Cloud TTS
    -- Requires [Google] credentials_file in HoundTTS-credentials.ini.
    { "googleTTS=true → Google",
      "TextToSpeech with googleTTS true. Google Cloud voice.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, 1.0, "female", "en-US", "en-US-Standard-C", true },

    -- AzureCreds ~= nil → Azure (any non-nil value triggers Azure routing)
    -- Requires [Azure] key + region in HoundTTS-credentials.ini.
    { "AzureCreds set → Azure",
      "TextToSpeech with AzureCreds set. Azure Neural voice.",
      TEST_FREQ, TEST_MOD, 1.0, "STTS-Test", TEST_COALITION,
      nil, 1.0, "female", "en-US", "en-US-AriaNeural", false, "azure" },

    -- Volume variation (0.5)
    { "volume 0.5",
      "TextToSpeech at half volume.",
      TEST_FREQ, TEST_MOD, 0.5, "STTS-Test", TEST_COALITION,
      nil, 1.0, "female", "en-US" },

    -- FM modulation
    { "FM modulation",
      "TextToSpeech on FM modulation.",
      TEST_FREQ, "FM", 1.0, "STTS-Test", TEST_COALITION },

    -- getSpeechTime return value sanity check (no transmission)
    { type = "getSpeechTime" },
}

-- Schedule Phase 2 tests
for i, t in ipairs(testsTTS) do
    after(phase1_offset + GAP * (i - 1), function()
        if t.type == "getSpeechTime" then
            env.info("[HoundTTS-test] TTS-" .. i .. ": getSpeechTime return value check")
            local test_str = "A rainbow is a meteorological phenomenon that is caused by reflection, refraction and dispersion of light in water droplets resulting in a spectrum of light appearing in the sky."
            local t1 = HoundTTS.getSpeechTime(test_str, 1, false)
            local t2 = HoundTTS.getSpeechTime(test_str, 1, true)
            local t3 = HoundTTS.getSpeechTime(#test_str, 1, false)
            if type(t1) ~= "number" or type(t2) ~= "number" or type(t3) ~= "number" then
                env.error("[HoundTTS-test] TTS-" .. i .. " FAILED: unexpected return types t1=" .. type(t1) .. " t2=" .. type(t2) .. " t3=" .. type(t3))
            elseif t2 >= t1 then
                env.error("[HoundTTS-test] TTS-" .. i .. " FAILED: googleTTS should be faster (shorter base), got t1=" .. t1 .. " t2=" .. t2)
            elseif t1 ~= t3 then
                env.error("[HoundTTS-test] TTS-" .. i .. " FAILED: string vs length mismatch t1=" .. t1 .. " t3=" .. t3)
            else
                env.info("[HoundTTS-test] TTS-" .. i .. " OK: sapi=" .. t1 .. "s google=" .. t2 .. "s byLen=" .. t3 .. "s")
            end
        else
            runTestTTS("TTS-" .. i .. ": " .. t[1], "Test TTS " .. i .. ". " .. t[2], unpack(t, 3))
        end
        return nil
    end)
end

-- ============================================================
-- Done marker
-- ============================================================
local phase2_offset = phase1_offset + GAP * #testsTTS
after(phase2_offset, function()
    env.info("[HoundTTS-test] All tests dispatched.")
    trigger.action.outText("HoundTTS: All provider tests dispatched. Check SRS on " .. TEST_FREQ .. " " .. TEST_MOD, 10)
    return nil
end)
