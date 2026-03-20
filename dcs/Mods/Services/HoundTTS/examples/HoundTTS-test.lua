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
local GAP            = 6    -- seconds between tests (overlap is fine)

-- ============================================================
-- Providers to test — comment out any provider to skip it.
-- The test tone and "special" entries always run regardless.
-- ============================================================
local TTS_PROVIDERS_TO_TEST = {
    "sapi",
    "piper",
    "azure",
    "google",
    "elevenlabs",
    "aws",
    "openai",
    "kittentts",
}

local TRANSLATION_PROVIDERS_TO_TEST = {
    "google",
    "libretranslate",
    "openai",
}

-- Build a quick lookup set from the list above
local _providerEnabled = {}
for _, p in ipairs(TTS_PROVIDERS_TO_TEST) do
    _providerEnabled[p] = true
end

-- Build a quick lookup set for translation providers
local _translationProviderEnabled = {}
for _, p in ipairs(TRANSLATION_PROVIDERS_TO_TEST) do
    _translationProviderEnabled[p] = true
end

local function providerEnabled(ep)
    if type(ep) ~= "table" then return true end   -- tone / special entries
    local p = ep.provider
    if type(p) ~= "string" then return true end
    return _providerEnabled[p] == true
end

local function translationProviderEnabled(pp)
    if type(pp) ~= "table" then return true end
    local p = pp.provider
    if type(p) ~= "string" then return true end
    return _translationProviderEnabled[p] == true
end

-- Helper: schedule a function call after `delay` seconds
local function after(delay, fn)
    timer.scheduleFunction(fn, nil, timer.getTime() + delay)
end

-- Helper: fire one Transmit call and log it
local function runTest(label, message, tp, ep, xl)
    env.info("[HoundTTS-test] " .. label)
    local ok, err = pcall(function()
        HoundTTS.Transmit(message, tp, ep, xl)
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

    -- getSpeechTime return value sanity check (no transmission)
    { type = "getSpeechTime" },

    -- SAPI (Windows built-in TTS, no credentials needed)
    { "SAPI default (multi-freq 251+305)",
      "SAPI default voice on two frequencies. Windows built-in text to speech.",
      { freqs = "251.0,305.0", modulations = "AM,AM", coalition = TEST_COALITION,
        name = "HoundTTS-Test-SAPI", volume = 1.0 },
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

    { "Google SSML with prosody and break",
      '<speak><prosody rate="fast" pitch="+2st">Google SSML test.</prosody>'
      .. '<break time="500ms"/>Resumed at normal rate.</speak>',
      "Google-SSML",
      { provider = "google", voice = "en-US-Standard-C", culture = "en-US", gender = "female", speed = 1.0 } },

    { "Google SSML say-as cardinal number",
      '<speak>Altitude <say-as interpret-as="cardinal">25000</say-as> feet.</speak>',
      "Google-SSML-sayas",
      { provider = "google", voice = "en-US-Standard-D", culture = "en-US", gender = "male", speed = 1.0 } },

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

    -- Amazon Polly (TTS provider="aws", "polly" also accepted as alias)
    -- Requires [AWS] access_key + secret_key + region in HoundTTS-credentials.ini.
    -- engine = "standard" is free tier (5M chars/month for 12 months); "neural" is paid.
    { "AWS Polly Joanna standard",
      "Amazon Polly. Joanna standard voice.",
      "AWS-Polly-Joanna",
      { provider = "aws", voice = "Joanna", culture = "en-US", gender = "female", speed = 1.0, engine = "standard" } },

    { "AWS Polly Matthew standard",
      "Amazon Polly. Matthew standard voice.",
      "AWS-Polly-Matthew",
      { provider = "aws", voice = "Matthew", culture = "en-US", gender = "male", speed = 1.0, engine = "standard" } },

    { "AWS Polly auto-select by culture+gender",
      "Amazon Polly auto-selected voice from culture and gender.",
      "AWS-Polly-auto",
      { provider = "aws", culture = "en-US", gender = "female", speed = 1.0, engine = "standard" } },

    { "AWS Polly SSML with prosody and break",
      '<speak><prosody rate="fast" pitch="+2st">AWS Polly SSML test.</prosody>'
      .. '<break time="500ms"/>Resumed at normal rate.</speak>',
      "AWS-Polly-SSML",
      { provider = "aws", voice = "Joanna", culture = "en-US", gender = "female", speed = 1.0, engine = "standard" } },

    { "AWS Polly SSML say-as cardinal number",
      '<speak>Altitude <say-as interpret-as="cardinal">25000</say-as> feet.</speak>',
      "AWS-Polly-SSML-sayas",
      { provider = "aws", voice = "Matthew", culture = "en-US", gender = "male", speed = 1.0, engine = "standard" } },

    -- Kitten TTS (local/self-hosted neural TTS — https://github.com/devnen/Kitten-TTS-Server)
    -- Requires [KittenTTS] endpoint in HoundTTS-credentials.ini.
    -- Available voices: Bella, Jasper, Luna, Bruno, Rosie, Hugo, Kiki, Leo
    { "KittenTTS warmup",
      "Warmup.",
      "Kitten-warmup",
      { provider = "kittentts", voice = "Bella" } },
    { "KittenTTS Bella",
      "Kitten TTS. Bella voice. Neural text to speech.",
      "Kitten-Bella",
      { provider = "kittentts", voice = "Bella" } },

    { "KittenTTS Hugo male",
      "Kitten TTS. Hugo voice. Male neural voice.",
      "Kitten-Hugo",
      { provider = "kittentts", voice = "Hugo" } },

    { "KittenTTS speed 1.3",
      "Kitten TTS at one point three speed.",
      "Kitten-fast",
      { provider = "kittentts", voice = "Bella", speed = 1.3 } },

    -- OpenAI TTS (cloud) / OpenAI-compatible endpoints (LocalAI, etc.)
    -- Requires [OpenAI] api_key (and optionally endpoint + model) in HoundTTS-credentials.ini.
    -- OpenAI voices: alloy, ash, coral, echo, fable, onyx, nova, sage, shimmer
    -- { "OpenAI alloy (tts-1)",
    --   "OpenAI TTS. Alloy voice. Default model.",
    --   "OpenAI-alloy",
    --   { provider = "openai", voice = "alloy", speed = 1.0 } },

    -- { "OpenAI nova female (tts-1)",
    --   "OpenAI TTS. Nova voice. Female neural voice.",
    --   "OpenAI-nova",
    --   { provider = "openai", voice = "nova", speed = 1.0 } },

    -- { "OpenAI onyx male (tts-1)",
    --   "OpenAI TTS. Onyx voice. Male neural voice.",
    --   "OpenAI-onyx",
    --   { provider = "openai", voice = "onyx", speed = 1.0 } },

    -- { "OpenAI echo speed 1.3",
    --   "OpenAI TTS. Echo voice at one point three speed.",
    --   "OpenAI-echo-fast",
    --   { provider = "openai", voice = "echo", speed = 1.3 } },

    -- { "OpenAI shimmer low volume (0.25)",
    --   "OpenAI TTS. Shimmer voice at quarter volume.",
    --   "OpenAI-quiet",
    --   { provider = "openai", voice = "shimmer", speed = 1.0 },
    --   volume = 0.25 },

    -- pocket-tts voices: alba, marius, javert, jean, fantine, cosette, eponine, azelma
    { "OpenAI alba (pocket-tts)",
      "OpenAI TTS. alba voice. Default model.",
      "OpenAI-alba",
      { provider = "openai", voice = "alba", speed = 1.0 } },

    { "OpenAI marius (pocket-tts)",
      "OpenAI TTS. marius voice. Female neural voice.",
      "OpenAI-marius",
      { provider = "openai", voice = "marius", speed = 1.0 } },

    { "OpenAI javert male (pocket-tts)",
      "OpenAI TTS. javert voice. Male neural voice.",
      "OpenAI-javert",
      { provider = "openai", voice = "javert", speed = 1.0 } },

    { "OpenAI jean speed 1.3",
      "OpenAI TTS. jean voice at one point three speed.",
      "OpenAI-jean-fast",
      { provider = "openai", voice = "jean", speed = 1.3 } },

    { "OpenAI fantine low volume (0.25)",
      "OpenAI TTS. fantine voice at quarter volume.",
      "OpenAI-quiet",
      { provider = "openai", voice = "fantine", speed = 1.0 },
      volume = 0.25 },
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

-- Schedule Phase 1 tests (only those whose provider is enabled)
local activeTests = {}
for _, t in ipairs(tests) do
    if t.type ~= nil or providerEnabled(t[4]) then
        activeTests[#activeTests + 1] = t
    end
end

for i, t in ipairs(activeTests) do
    after(1 + GAP * (i - 1), function()
        if t.type == "getSpeechTime" then
            env.info("[HoundTTS-test] " .. i .. ": getSpeechTime return value check")
            local test_str = "A rainbow is a meteorological phenomenon that is caused by reflection, refraction and dispersion of light in water droplets resulting in a spectrum of light appearing in the sky."
            local t1 = HoundTTS.getSpeechTime(test_str, 1, false)
            local t2 = HoundTTS.getSpeechTime(test_str, 1, true)
            local t3 = HoundTTS.getSpeechTime(#test_str, 1, false)
            if type(t1) ~= "number" or type(t2) ~= "number" or type(t3) ~= "number" then
                env.error("[HoundTTS-test] " .. i .. " FAILED: unexpected return types t1=" .. type(t1) .. " t2=" .. type(t2) .. " t3=" .. type(t3))
            elseif t2 >= t1 then
                env.error("[HoundTTS-test] " .. i .. " FAILED: googleTTS should be faster, got t1=" .. t1 .. " t2=" .. t2)
            elseif t1 ~= t3 then
                env.error("[HoundTTS-test] " .. i .. " FAILED: string vs length mismatch t1=" .. t1 .. " t3=" .. t3)
            else
                env.info("[HoundTTS-test] " .. i .. " OK: sapi=" .. t1 .. "s google=" .. t2 .. "s byLen=" .. t3 .. "s")
            end
        elseif t.type == "tone" then
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
-- Stage 2: Translate-in-Transmit tests
--
-- These tests exercise the translation_params 4th argument to
-- HoundTTS.Transmit(). The message is translated on the DLL
-- background thread before TTS synthesis and SRS transmission.
--
-- Each entry: { label, message, tp_name_or_table, ep, xl }
--   ep = provider_params (TTS provider)
--   xl = translation_params (translate provider + target language)
-- ============================================================
local stage1_offset = 1 + GAP * #activeTests

local testsXL = {
    -- Google TTS (German voice) + Google Translate → German
    { "Google TTS + Google Translate → de",
      "Two aircraft approaching from the north at high altitude.",
      "GoogleXL-de",
      { provider = "google", voice = "de-DE-Standard-A", culture = "de-DE", gender = "female", speed = 1.0 },
      { provider = "google", language = "de" } },

    -- Piper (English model) + LibreTranslate → French
    { "Piper + LibreTranslate → fr",
      "Runway two seven, wind two five zero at twelve knots. Cleared for takeoff.",
      "PiperXL-fr",
      { provider = "piper", voice = "en_US-lessac-low", culture = "en-US", speed = 1.0 },
      { provider = "libretranslate", language = "fr" } },

    -- OpenAI pocket-tts (alba) + OpenAI Translate → German
    { "OpenAI pocket-tts + OpenAI Translate → de",
      "BOGEY, BRAA two seven zero for thirty five, angels twenty, hot, hostile.",
      "OpenAIXL-de",
      { provider = "openai", voice = "alba", speed = 1.0 },
      { provider = "openai", language = "de" } },
}

-- Schedule Stage 2 tests (both TTS and translation providers must be enabled)
for i, t in ipairs(testsXL) do
    if providerEnabled(t[4]) and translationProviderEnabled(t[5]) then
        after(stage1_offset + GAP * (i - 1), function()
            local tp = type(t[3]) == "string" and baseTP(t[3]) or t[3]
            runTest("XL-" .. i .. ": " .. t[1], "Translate+TTS " .. i .. ". " .. t[2], tp, t[4], t[5])
            return nil
        end)
    else
        env.info("[HoundTTS-test] Skipping translate-transmit test: " .. t[1] .. " (provider disabled)")
    end
end

-- ============================================================
-- Stage 3: Standalone Translation tests
--
-- These tests exercise HoundTTS.Translate(). Each test sends a
-- military-aviation-style message for translation and displays
-- both the input and output on screen via trigger.action.outText.
--
-- Requires [OpenAI] api_key + endpoint in HoundTTS-credentials.ini
-- and a chat_model set (default: gpt-4o-mini).
-- ============================================================
local stage2_offset = stage1_offset + GAP * #testsXL

-- Helper: run one translation test and show input/output on screen (async)
local function runTranslateTest(index, label, message, params)
    env.info("[HoundTTS-test] Translate-" .. index .. ": " .. label .. " (dispatched)")
    local ok, callErr = pcall(function()
        HoundTTS.Translate(message, params, function(result, err)
            if result then
                local display = "Translate-" .. index .. ": " .. label
                    .. "\n\nIN:  " .. message
                    .. "\nOUT: " .. tostring(result)
                env.info("[HoundTTS-test] " .. display)
                trigger.action.outText(display, 15)
            else
                local errMsg = "Translate-" .. index .. ": " .. label .. "\nFAILED: " .. tostring(err)
                env.error("[HoundTTS-test] " .. errMsg)
                trigger.action.outText("HoundTTS Translate FAILED\n" .. errMsg, 15)
            end
        end)
    end)
    if not ok then
        local errMsg = "Translate-" .. index .. ": " .. label .. "\nERROR: " .. tostring(callErr)
        env.error("[HoundTTS-test] " .. errMsg)
        trigger.action.outText("HoundTTS Translate FAILED\n" .. errMsg, 15)
    end
end

local testsTranslate = {
    -- Basic translation to German
    { "English → German (basic)",
      "Two aircraft approaching from the north at high altitude.",
      { provider = "openai", language = "de" } },

    -- Brevity code preservation: FOX calls should NOT be translated
    { "Brevity codes preserved (de)",
      "FOX 3, FOX 3! Missile away. SPLASH one bandit.",
      { provider = "openai", language = "de" } },

    -- BRAA call — numbers and brevity terms should stay intact
    { "BRAA call (fr)",
      "BOGEY, BRAA 270 for 35, angels 20, hot, hostile.",
      { provider = "openai", language = "fr" } },

    -- Mixed brevity and plain language
    { "Mixed brevity + plain (es)",
      "WINCHESTER on all air-to-air missiles. RTB to home plate. Request BINGO fuel state.",
      { provider = "openai", language = "es" } },

    -- GCI-style radio call
    { "GCI radio call (pt)",
      "Two bandits, BULLSEYE 090 for 45, angels 25, track west, hostile.",
      { provider = "openai", language = "pt" } },

    -- ATIS-style weather report
    { "ATIS weather (ru)",
      "Runway 27, wind 250 at 12 knots, visibility 10 kilometers, QNH 1013. Expect ILS approach.",
      { provider = "openai", language = "ru" } },

    -- Already in target language — should pass through unchanged
    { "Already in target language (en→en)",
      "TALLY two, MERGED, DEFENSIVE!",
      { provider = "openai", language = "en" } },

    -- CAS / JTAC terminology
    { "CAS 9-line (it)",
      "JTAC contact. Type 2 control. Bomb on coordinate. Friendlies marked with smoke. CLEARED HOT.",
      { provider = "openai", language = "it" } },

    -- ---- Google Translate tests ----
    -- Requires [Google] credentials_file + Cloud Translation API enabled.
    -- Note: Google Translate is a pure translation API — no prompt engineering,
    --       brevity codes may be translated literally (expected behavior).

    { "Google → de (basic)",
      "Two aircraft approaching from the north at high altitude.",
      { provider = "google", language = "de" } },

    { "Google → fr (BRAA call)",
      "BOGEY, BRAA 270 for 35, angels 20, hot, hostile.",
      { provider = "google", language = "fr" } },

    { "Google → ru (ATIS)",
      "Runway 27, wind 250 at 12 knots, visibility 10 kilometers, QNH 1013. Expect ILS approach.",
      { provider = "google", language = "ru" } },

    { "Google → es (mixed brevity)",
      "WINCHESTER on all air-to-air missiles. RTB to home plate. Request BINGO fuel state.",
      { provider = "google", language = "es" } },

    -- ---- LibreTranslate tests ----
    -- Requires a running LibreTranslate instance.
    -- Default endpoint: http://localhost:5000
    -- Configure via [LibreTranslate] endpoint in HoundTTS-credentials.ini

    { "LibreTranslate → de (basic)",
      "Two aircraft approaching from the north at high altitude.",
      { provider = "libretranslate", language = "de" } },

    { "LibreTranslate → fr (BRAA call)",
      "BOGEY, BRAA 270 for 35, angels 20, hot, hostile.",
      { provider = "libretranslate", language = "fr" } },

    { "LibreTranslate → ru (ATIS)",
      "Runway 27, wind 250 at 12 knots, visibility 10 kilometers, QNH 1013.",
      { provider = "libretranslate", language = "ru" } },

    { "LibreTranslate → es (radio call)",
      "Cleared for takeoff runway two seven. Wind two five zero at twelve. Altimeter two niner niner two.",
      { provider = "libretranslate", language = "es" } },
}

-- Schedule Stage 3 tests
env.info("[HoundTTS-test] Stage 3 scheduling: " .. #testsTranslate .. " translate tests, stage2_offset=" .. tostring(stage2_offset))
for i, t in ipairs(testsTranslate) do
    if translationProviderEnabled(t[3]) then
        after(stage2_offset + GAP * (i - 1), function()
            env.info("[HoundTTS-test] Stage 3 timer fired: translate test " .. i .. ": " .. t[1])
            runTranslateTest(i, t[1], t[2], t[3])
            return nil
        end)
    else
        env.info("[HoundTTS-test] Skipping translation test " .. i .. ": " .. t[1] .. " (provider disabled)")
    end
end

-- ============================================================
-- Stage 4: TextToSpeech — STTS backward-compatibility tests
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
-- ============================================================
local stage3_offset = stage2_offset + GAP * #testsTranslate

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
}

-- Schedule Stage 4 tests
local activeTTS = {}
for _, t in ipairs(testsTTS) do
    if providerEnabled({ provider = "sapi" }) then
        activeTTS[#activeTTS + 1] = t
    end
end

for i, t in ipairs(activeTTS) do
    after(stage3_offset + GAP * (i - 1), function()
        runTestTTS("TTS-" .. i .. ": " .. t[1], "Test TTS " .. i .. ". " .. t[2], unpack(t, 3))
        return nil
    end)
end

-- ============================================================
-- Done marker
-- ============================================================
local stage4_offset = stage3_offset + GAP * #testsTTS
after(stage4_offset, function()
    env.info("[HoundTTS-test] All tests dispatched.")
    trigger.action.outText("HoundTTS: All provider tests dispatched. Check SRS on " .. TEST_FREQ .. " " .. TEST_MOD, 10)
    return nil
end)
