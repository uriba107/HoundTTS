-- HoundTTS DCS Hook Script
-- Thin loader: delegates all logic to Mods\Services\HoundTTS\Scripts\HoundTTS.lua
-- Install: copy to %USERPROFILE%\Saved Games\DCS\Scripts\Hooks\
--          (or DCS.openbeta for Open Beta installs)

local status, err = pcall(function()
    local lfs = require("lfs")
    dofile(lfs.writedir() .. [[Mods\Services\HoundTTS\Scripts\HoundTTS.lua]])
end)

if not status then
    log.write("HoundTTS", log.ERROR, "Failed to load HoundTTS.lua: " .. tostring(err))
end
