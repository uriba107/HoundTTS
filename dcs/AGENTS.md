# dcs — DCS World Deployment Files

## Purpose

Configuration templates, Lua scripts, and hook files for deploying HoundTTS into DCS World's Saved Games folder. These are the files users extract into `%USERPROFILE%\Saved Games\DCS\`.

## Ownership

### Config/

- `HoundTTS-config.lua.example` — Lua config template (SRS host/port, defaults)
- `HoundTTS-credentials.ini.example` — Credentials INI template (API keys, paths)

### Scripts/Hooks/

- `HoundTTS-hook.lua` — DCS hook script, loaded on every mission start. Auto-loads HoundTTS in desanitized environments.

### Mods/Services/HoundTTS/

- `entry.lua` — Module entry point (loaded via `require("HoundTTS")`)
- `Scripts/HoundTTS.lua` — Hook-side placeholder (currently logs load message)
- `Scripts/HoundTTS-mission.lua` — Mission-side Lua bridge: loads the DLL, registers the `HoundTTS` global table, exposes `Transmit`, `TextToSpeech`, `TransmitTone`, `TransmitNoise`, `Translate`, `TestTone`, `UpdateSession`, `KillSession`, and helper functions
- `examples/HoundTTS-test.lua` — Example usage for testing
- `examples/HoundTTS-jammer.lua` — Example usage for noise jamming

## Local Contracts

- Lua API in `HoundTTS-mission.lua` is the primary user-facing interface
- `TextToSpeech` is the drop-in STTS-compatible function; `Transmit` is the modern flexible API
- Examples must be kept in sync with the README
- INI and config Lua examples must match the defaults/defaults documented in the README

## Work Guidance

- User config files use `.example` suffix — never overwrite live configs on update
- Hook script must not break vanilla DCS environments (check `require` availability)
- Mission script loads DLL before sanitization — keep sanitization-safe

## Verification

Test by deploying to Saved Games and running `HoundTTS.TestTone("251.0", "AM", 2)` from a DCS mission script.

## Child DOX Index

None.
