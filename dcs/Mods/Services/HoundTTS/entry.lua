declare_plugin("HoundTTS", {
    installed     = true,
    dirName       = current_mod_path,
    developerName = _("HoundTTS"),
    displayName   = _("HoundTTS - SRS Text-to-Speech Bridge"),
    version       = "2.0.0",
    state         = "installed",
    info          = _("HoundTTS\n\nText-to-Speech bridge for DCS via SRS ExternalAudio.\nProvides HoundTTS.TextToSpeech() to mission scripts.\n\nAuto-loads on desanitized servers (e.g. DCSServerBot).\nOn vanilla DCS, add one dofile() line to MissionScripting.lua."),
    load_immediate = true,
})

plugin_done()
