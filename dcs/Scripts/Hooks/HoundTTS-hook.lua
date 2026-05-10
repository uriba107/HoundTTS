-- HoundTTS DCS Hook Script
-- Thin loader: delegates all logic to Mods\Services\HoundTTS\Scripts\HoundTTS.lua
-- Install: copy to %USERPROFILE%\Saved Games\DCS\Scripts\Hooks\
--          (or DCS.openbeta for Open Beta installs)
--
-- Auto-load behaviour (no MissionScripting.lua edit required):
--   If the mission scripting environment is desanitized (e.g. DCSServerBot is
--   installed), HoundTTS-mission.lua is injected automatically on mission load.
--   On vanilla / sanitized servers the user must add the dofile() line to
--   MissionScripting.lua as described in the README.

local status, err = pcall(function()
    local lfs = require("lfs")
    dofile(lfs.writedir() .. [[Mods\Services\HoundTTS\Scripts\HoundTTS.lua]])
end)

if not status then
    log.write("HoundTTS", log.ERROR, "Failed to load HoundTTS.lua: " .. tostring(err))
end

-- ---------------------------------------------------------------------------
-- Mission-load injection
-- ---------------------------------------------------------------------------
local houndtts_hooks = {}

function houndtts_hooks.onMissionLoadEnd()
    local lfs = require("lfs")

    -- Build the dofile injection command (hook has lfs; use forward slashes for safety)
    local path = lfs.writedir():gsub("\\", "/") ..
        "Mods/Services/HoundTTS/Scripts/HoundTTS-mission.lua"

    -- Attempt injection. HoundTTS-mission.lua guards with `if not HoundTTS` so
    -- double-loading via the legacy MissionScripting.lua dofile is a safe no-op.
    -- Wrap in pcall so failures in a sanitized env are caught silently inside the
    -- mission state without raising an unhandled Lua error there.
    -- Any real failures (missing DLL, sanitized env) are reported by
    -- HoundTTS-mission.lua itself via env.error(), which appears in dcs.log.
    local cmd = string.format(
        'a_do_script("pcall(dofile,\\"%s\\")")', path)
    net.dostring_in("mission", cmd)
    log.write("HoundTTS", log.INFO,
        "HoundTTS-mission.lua injection attempted via hook. "..
        "See dcs.log for mission-side load result.")
end

function houndtts_hooks.onSimulationStop()
    -- Mission-end cleanup: clear PCM cache + release idle COM DLLs
    -- (e.g. MSTTSEngine_OneCore.dll) so the next mission starts clean.
    -- Dispatched into the mission Lua state where the DLL functions live.
    local cmd = [[
        if HoundTTS and type(HoundTTS.onMissionEnd) == "function" then
            HoundTTS.onMissionEnd()
            env.info("[HoundTTS] mission-end cleanup complete")
        end
    ]]
    local ok, err = pcall(net.dostring_in, "mission", cmd)
    if not ok then
        log.write("HoundTTS", log.ERROR,
            "Failed to dispatch mission-end cleanup: " .. tostring(err))
    end
end

local ok, regErr = pcall(Sim.setUserCallbacks, houndtts_hooks)
if not ok then
    log.write("HoundTTS", log.ERROR,
        "Failed to register hook callbacks: " .. tostring(regErr))
end
