# Edge TTS — Available Voices

Voice list for the Microsoft Edge Read Aloud TTS service, used by the `edge` provider in HoundTTS.

**322 voices** across **142 locales** — retrieved 2026-06-06 from the Edge TTS API.

> **Note:** This list may change as Microsoft adds or removes voices. To get the latest list, open this URL in a browser:
> `https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4`

## Usage in HoundTTS

Set the `voice` parameter in `provider_params` to the **ShortName** value:

```lua
HoundTTS.Transmit("Hello world",
    { freqs = "251.0", coalition = 2 },
    { provider = "edge", voice = "en-US-AriaNeural" }
)
```

If `voice` is omitted, defaults are chosen from `culture` + `gender`:
- **female** → `{culture}-AriaNeural`
- **male** → `{culture}-GuyNeural`

## Voice List

| Locale | Language | Voice (ShortName) | Gender | Personality |
|--------|----------|-------------------|--------|-------------|
| af-ZA | Afrikaans | `af-ZA-AdriNeural` | Female | Friendly, Positive |
| af-ZA | Afrikaans | `af-ZA-WillemNeural` | Male | Friendly, Positive |
| am-ET | Amharic | `am-ET-MekdesNeural` | Female | Friendly, Positive |
| am-ET | Amharic | `am-ET-AmehaNeural` | Male | Friendly, Positive |
| ar-AE | Arabic | `ar-AE-FatimaNeural` | Female | Friendly, Positive |
| ar-AE | Arabic | `ar-AE-HamdanNeural` | Male | Friendly, Positive |
| ar-BH | Arabic | `ar-BH-LailaNeural` | Female | Friendly, Positive |
| ar-BH | Arabic | `ar-BH-AliNeural` | Male | Friendly, Positive |
| ar-DZ | Arabic | `ar-DZ-AminaNeural` | Female | Friendly, Positive |
| ar-DZ | Arabic | `ar-DZ-IsmaelNeural` | Male | Friendly, Positive |
| ar-EG | Arabic | `ar-EG-SalmaNeural` | Female | Friendly, Positive |
| ar-EG | Arabic | `ar-EG-ShakirNeural` | Male | Friendly, Positive |
| ar-IQ | Arabic | `ar-IQ-RanaNeural` | Female | Friendly, Positive |
| ar-IQ | Arabic | `ar-IQ-BasselNeural` | Male | Friendly, Positive |
| ar-JO | Arabic | `ar-JO-SanaNeural` | Female | Friendly, Positive |
| ar-JO | Arabic | `ar-JO-TaimNeural` | Male | Friendly, Positive |
| ar-KW | Arabic | `ar-KW-NouraNeural` | Female | Friendly, Positive |
| ar-KW | Arabic | `ar-KW-FahedNeural` | Male | Friendly, Positive |
| ar-LB | Arabic | `ar-LB-LaylaNeural` | Female | Friendly, Positive |
| ar-LB | Arabic | `ar-LB-RamiNeural` | Male | Friendly, Positive |
| ar-LY | Arabic | `ar-LY-ImanNeural` | Female | Friendly, Positive |
| ar-LY | Arabic | `ar-LY-OmarNeural` | Male | Friendly, Positive |
| ar-MA | Arabic | `ar-MA-MounaNeural` | Female | Friendly, Positive |
| ar-MA | Arabic | `ar-MA-JamalNeural` | Male | Friendly, Positive |
| ar-OM | Arabic | `ar-OM-AyshaNeural` | Female | Friendly, Positive |
| ar-OM | Arabic | `ar-OM-AbdullahNeural` | Male | Friendly, Positive |
| ar-QA | Arabic | `ar-QA-AmalNeural` | Female | Friendly, Positive |
| ar-QA | Arabic | `ar-QA-MoazNeural` | Male | Friendly, Positive |
| ar-SA | Arabic | `ar-SA-ZariyahNeural` | Female | Friendly, Positive |
| ar-SA | Arabic | `ar-SA-HamedNeural` | Male | Friendly, Positive |
| ar-SY | Arabic | `ar-SY-AmanyNeural` | Female | Friendly, Positive |
| ar-SY | Arabic | `ar-SY-LaithNeural` | Male | Friendly, Positive |
| ar-TN | Arabic | `ar-TN-ReemNeural` | Female | Friendly, Positive |
| ar-TN | Arabic | `ar-TN-HediNeural` | Male | Friendly, Positive |
| ar-YE | Arabic | `ar-YE-MaryamNeural` | Female | Friendly, Positive |
| ar-YE | Arabic | `ar-YE-SalehNeural` | Male | Friendly, Positive |
| az-AZ | Azerbaijani | `az-AZ-BanuNeural` | Female | Friendly, Positive |
| az-AZ | Azerbaijani | `az-AZ-BabekNeural` | Male | Friendly, Positive |
| bg-BG | Bulgarian | `bg-BG-KalinaNeural` | Female | Friendly, Positive |
| bg-BG | Bulgarian | `bg-BG-BorislavNeural` | Male | Friendly, Positive |
| bn-BD | Bangla | `bn-BD-NabanitaNeural` | Female | Friendly, Positive |
| bn-BD | Bangla | `bn-BD-PradeepNeural` | Male | Friendly, Positive |
| bn-IN | Bengali | `bn-IN-TanishaaNeural` | Female | Friendly, Positive |
| bn-IN | Bangla | `bn-IN-BashkarNeural` | Male | Friendly, Positive |
| bs-BA | Bosnian | `bs-BA-VesnaNeural` | Female | Friendly, Positive |
| bs-BA | Bosnian | `bs-BA-GoranNeural` | Male | Friendly, Positive |
| ca-ES | Catalan | `ca-ES-JoanaNeural` | Female | Friendly, Positive |
| ca-ES | Catalan | `ca-ES-EnricNeural` | Male | Friendly, Positive |
| cs-CZ | Czech | `cs-CZ-VlastaNeural` | Female | Friendly, Positive |
| cs-CZ | Czech | `cs-CZ-AntoninNeural` | Male | Friendly, Positive |
| cy-GB | Welsh | `cy-GB-NiaNeural` | Female | Friendly, Positive |
| cy-GB | Welsh | `cy-GB-AledNeural` | Male | Friendly, Positive |
| da-DK | Danish | `da-DK-ChristelNeural` | Female | Friendly, Positive |
| da-DK | Danish | `da-DK-JeppeNeural` | Male | Friendly, Positive |
| de-AT | German | `de-AT-IngridNeural` | Female | Friendly, Positive |
| de-AT | German | `de-AT-JonasNeural` | Male | Friendly, Positive |
| de-CH | German | `de-CH-LeniNeural` | Female | Friendly, Positive |
| de-CH | German | `de-CH-JanNeural` | Male | Friendly, Positive |
| de-DE | German | `de-DE-AmalaNeural` | Female | Friendly, Positive |
| de-DE | German | `de-DE-KatjaNeural` | Female | Friendly, Positive |
| de-DE | German | `de-DE-SeraphinaMultilingualNeural` | Female | Friendly, Positive |
| de-DE | German | `de-DE-ConradNeural` | Male | Friendly, Positive |
| de-DE | German | `de-DE-FlorianMultilingualNeural` | Male | Friendly, Positive |
| de-DE | German | `de-DE-KillianNeural` | Male | Friendly, Positive |
| el-GR | Greek | `el-GR-AthinaNeural` | Female | Friendly, Positive |
| el-GR | Greek | `el-GR-NestorasNeural` | Male | Friendly, Positive |
| en-AU | English | `en-AU-NatashaNeural` | Female | Friendly, Positive |
| en-AU | English | `en-AU-WilliamMultilingualNeural` | Male | Friendly, Positive |
| en-CA | English | `en-CA-ClaraNeural` | Female | Friendly, Positive |
| en-CA | English | `en-CA-LiamNeural` | Male | Friendly, Positive |
| en-GB | English | `en-GB-LibbyNeural` | Female | Friendly, Positive |
| en-GB | English | `en-GB-MaisieNeural` | Female | Friendly, Positive |
| en-GB | English | `en-GB-SoniaNeural` | Female | Friendly, Positive |
| en-GB | English | `en-GB-RyanNeural` | Male | Friendly, Positive |
| en-GB | English | `en-GB-ThomasNeural` | Male | Friendly, Positive |
| en-HK | English | `en-HK-YanNeural` | Female | Friendly, Positive |
| en-HK | English | `en-HK-SamNeural` | Male | Friendly, Positive |
| en-IE | English | `en-IE-EmilyNeural` | Female | Friendly, Positive |
| en-IE | English | `en-IE-ConnorNeural` | Male | Friendly, Positive |
| en-IN | English | `en-IN-NeerjaExpressiveNeural` | Female | Friendly, Positive |
| en-IN | English | `en-IN-NeerjaNeural` | Female | Friendly, Positive |
| en-IN | English | `en-IN-PrabhatNeural` | Male | Friendly, Positive |
| en-KE | English | `en-KE-AsiliaNeural` | Female | Friendly, Positive |
| en-KE | English | `en-KE-ChilembaNeural` | Male | Friendly, Positive |
| en-NG | English | `en-NG-EzinneNeural` | Female | Friendly, Positive |
| en-NG | English | `en-NG-AbeoNeural` | Male | Friendly, Positive |
| en-NZ | English | `en-NZ-MollyNeural` | Female | Friendly, Positive |
| en-NZ | English | `en-NZ-MitchellNeural` | Male | Friendly, Positive |
| en-PH | English | `en-PH-RosaNeural` | Female | Friendly, Positive |
| en-PH | English | `en-PH-JamesNeural` | Male | Friendly, Positive |
| en-SG | English | `en-SG-LunaNeural` | Female | Friendly, Positive |
| en-SG | English | `en-SG-WayneNeural` | Male | Friendly, Positive |
| en-TZ | English | `en-TZ-ImaniNeural` | Female | Friendly, Positive |
| en-TZ | English | `en-TZ-ElimuNeural` | Male | Friendly, Positive |
| en-US | English | `en-US-AnaNeural` | Female | Cute |
| en-US | English | `en-US-AriaNeural` | Female | Positive, Confident |
| en-US | English | `en-US-AvaMultilingualNeural` | Female | Expressive, Caring, Pleasant, Friendly |
| en-US | English | `en-US-AvaNeural` | Female | Expressive, Caring, Pleasant, Friendly |
| en-US | English | `en-US-EmmaMultilingualNeural` | Female | Cheerful, Clear, Conversational |
| en-US | English | `en-US-EmmaNeural` | Female | Cheerful, Clear, Conversational |
| en-US | English | `en-US-JennyNeural` | Female | Friendly, Considerate, Comfort |
| en-US | English | `en-US-MichelleNeural` | Female | Friendly, Pleasant |
| en-US | English | `en-US-AndrewMultilingualNeural` | Male | Warm, Confident, Authentic, Honest |
| en-US | English | `en-US-AndrewNeural` | Male | Warm, Confident, Authentic, Honest |
| en-US | English | `en-US-BrianMultilingualNeural` | Male | Approachable, Casual, Sincere |
| en-US | English | `en-US-BrianNeural` | Male | Approachable, Casual, Sincere |
| en-US | English | `en-US-ChristopherNeural` | Male | Reliable, Authority |
| en-US | English | `en-US-EricNeural` | Male | Rational |
| en-US | English | `en-US-GuyNeural` | Male | Passion |
| en-US | English | `en-US-RogerNeural` | Male | Lively |
| en-US | English | `en-US-SteffanNeural` | Male | Rational |
| en-ZA | English | `en-ZA-LeahNeural` | Female | Friendly, Positive |
| en-ZA | English | `en-ZA-LukeNeural` | Male | Friendly, Positive |
| es-AR | Spanish | `es-AR-ElenaNeural` | Female | Friendly, Positive |
| es-AR | Spanish | `es-AR-TomasNeural` | Male | Friendly, Positive |
| es-BO | Spanish | `es-BO-SofiaNeural` | Female | Friendly, Positive |
| es-BO | Spanish | `es-BO-MarceloNeural` | Male | Friendly, Positive |
| es-CL | Spanish | `es-CL-CatalinaNeural` | Female | Friendly, Positive |
| es-CL | Spanish | `es-CL-LorenzoNeural` | Male | Friendly, Positive |
| es-CO | Spanish | `es-CO-SalomeNeural` | Female | Friendly, Positive |
| es-CO | Spanish | `es-CO-GonzaloNeural` | Male | Friendly, Positive |
| es-CR | Spanish | `es-CR-MariaNeural` | Female | Friendly, Positive |
| es-CR | Spanish | `es-CR-JuanNeural` | Male | Friendly, Positive |
| es-CU | Spanish | `es-CU-BelkysNeural` | Female | Friendly, Positive |
| es-CU | Spanish | `es-CU-ManuelNeural` | Male | Friendly, Positive |
| es-DO | Spanish | `es-DO-RamonaNeural` | Female | Friendly, Positive |
| es-DO | Spanish | `es-DO-EmilioNeural` | Male | Friendly, Positive |
| es-EC | Spanish | `es-EC-AndreaNeural` | Female | Friendly, Positive |
| es-EC | Spanish | `es-EC-LuisNeural` | Male | Friendly, Positive |
| es-ES | Spanish | `es-ES-ElviraNeural` | Female | Friendly, Positive |
| es-ES | Spanish | `es-ES-XimenaNeural` | Female | Friendly, Positive |
| es-ES | Spanish | `es-ES-AlvaroNeural` | Male | Friendly, Positive |
| es-GQ | Spanish | `es-GQ-TeresaNeural` | Female | Friendly, Positive |
| es-GQ | Spanish | `es-GQ-JavierNeural` | Male | Friendly, Positive |
| es-GT | Spanish | `es-GT-MartaNeural` | Female | Friendly, Positive |
| es-GT | Spanish | `es-GT-AndresNeural` | Male | Friendly, Positive |
| es-HN | Spanish | `es-HN-KarlaNeural` | Female | Friendly, Positive |
| es-HN | Spanish | `es-HN-CarlosNeural` | Male | Friendly, Positive |
| es-MX | Spanish | `es-MX-DaliaNeural` | Female | Friendly, Positive |
| es-MX | Spanish | `es-MX-JorgeNeural` | Male | Friendly, Positive |
| es-NI | Spanish | `es-NI-YolandaNeural` | Female | Friendly, Positive |
| es-NI | Spanish | `es-NI-FedericoNeural` | Male | Friendly, Positive |
| es-PA | Spanish | `es-PA-MargaritaNeural` | Female | Friendly, Positive |
| es-PA | Spanish | `es-PA-RobertoNeural` | Male | Friendly, Positive |
| es-PE | Spanish | `es-PE-CamilaNeural` | Female | Friendly, Positive |
| es-PE | Spanish | `es-PE-AlexNeural` | Male | Friendly, Positive |
| es-PR | Spanish | `es-PR-KarinaNeural` | Female | Friendly, Positive |
| es-PR | Spanish | `es-PR-VictorNeural` | Male | Friendly, Positive |
| es-PY | Spanish | `es-PY-TaniaNeural` | Female | Friendly, Positive |
| es-PY | Spanish | `es-PY-MarioNeural` | Male | Friendly, Positive |
| es-SV | Spanish | `es-SV-LorenaNeural` | Female | Friendly, Positive |
| es-SV | Spanish | `es-SV-RodrigoNeural` | Male | Friendly, Positive |
| es-US | Spanish | `es-US-PalomaNeural` | Female | Friendly, Positive |
| es-US | Spanish | `es-US-AlonsoNeural` | Male | Friendly, Positive |
| es-UY | Spanish | `es-UY-ValentinaNeural` | Female | Friendly, Positive |
| es-UY | Spanish | `es-UY-MateoNeural` | Male | Friendly, Positive |
| es-VE | Spanish | `es-VE-PaolaNeural` | Female | Friendly, Positive |
| es-VE | Spanish | `es-VE-SebastianNeural` | Male | Friendly, Positive |
| et-EE | Estonian | `et-EE-AnuNeural` | Female | Friendly, Positive |
| et-EE | Estonian | `et-EE-KertNeural` | Male | Friendly, Positive |
| fa-IR | Persian | `fa-IR-DilaraNeural` | Female | Friendly, Positive |
| fa-IR | Persian | `fa-IR-FaridNeural` | Male | Friendly, Positive |
| fi-FI | Finnish | `fi-FI-NooraNeural` | Female | Friendly, Positive |
| fi-FI | Finnish | `fi-FI-HarriNeural` | Male | Friendly, Positive |
| fil-PH | Filipino | `fil-PH-BlessicaNeural` | Female | Friendly, Positive |
| fil-PH | Filipino | `fil-PH-AngeloNeural` | Male | Friendly, Positive |
| fr-BE | French | `fr-BE-CharlineNeural` | Female | Friendly, Positive |
| fr-BE | French | `fr-BE-GerardNeural` | Male | Friendly, Positive |
| fr-CA | French | `fr-CA-SylvieNeural` | Female | Friendly, Positive |
| fr-CA | French | `fr-CA-AntoineNeural` | Male | Friendly, Positive |
| fr-CA | French | `fr-CA-JeanNeural` | Male | Friendly, Positive |
| fr-CA | French | `fr-CA-ThierryNeural` | Male | Friendly, Positive |
| fr-CH | French | `fr-CH-ArianeNeural` | Female | Friendly, Positive |
| fr-CH | French | `fr-CH-FabriceNeural` | Male | Friendly, Positive |
| fr-FR | French | `fr-FR-DeniseNeural` | Female | Friendly, Positive |
| fr-FR | French | `fr-FR-EloiseNeural` | Female | Friendly, Positive |
| fr-FR | French | `fr-FR-VivienneMultilingualNeural` | Female | Friendly, Positive |
| fr-FR | French | `fr-FR-HenriNeural` | Male | Friendly, Positive |
| fr-FR | French | `fr-FR-RemyMultilingualNeural` | Male | Friendly, Positive |
| ga-IE | Irish | `ga-IE-OrlaNeural` | Female | Friendly, Positive |
| ga-IE | Irish | `ga-IE-ColmNeural` | Male | Friendly, Positive |
| gl-ES | Galician | `gl-ES-SabelaNeural` | Female | Friendly, Positive |
| gl-ES | Galician | `gl-ES-RoiNeural` | Male | Friendly, Positive |
| gu-IN | Gujarati | `gu-IN-DhwaniNeural` | Female | Friendly, Positive |
| gu-IN | Gujarati | `gu-IN-NiranjanNeural` | Male | Friendly, Positive |
| he-IL | Hebrew | `he-IL-HilaNeural` | Female | Friendly, Positive |
| he-IL | Hebrew | `he-IL-AvriNeural` | Male | Friendly, Positive |
| hi-IN | Hindi | `hi-IN-SwaraNeural` | Female | Friendly, Positive |
| hi-IN | Hindi | `hi-IN-MadhurNeural` | Male | Friendly, Positive |
| hr-HR | Croatian | `hr-HR-GabrijelaNeural` | Female | Friendly, Positive |
| hr-HR | Croatian | `hr-HR-SreckoNeural` | Male | Friendly, Positive |
| hu-HU | Hungarian | `hu-HU-NoemiNeural` | Female | Friendly, Positive |
| hu-HU | Hungarian | `hu-HU-TamasNeural` | Male | Friendly, Positive |
| id-ID | Indonesian | `id-ID-GadisNeural` | Female | Friendly, Positive |
| id-ID | Indonesian | `id-ID-ArdiNeural` | Male | Friendly, Positive |
| is-IS | Icelandic | `is-IS-GudrunNeural` | Female | Friendly, Positive |
| is-IS | Icelandic | `is-IS-GunnarNeural` | Male | Friendly, Positive |
| it-IT | Italian | `it-IT-ElsaNeural` | Female | Friendly, Positive |
| it-IT | Italian | `it-IT-IsabellaNeural` | Female | Friendly, Positive |
| it-IT | Italian | `it-IT-DiegoNeural` | Male | Friendly, Positive |
| it-IT | Italian | `it-IT-GiuseppeMultilingualNeural` | Male | Friendly, Positive |
| iu-Cans-CA | Inuktitut | `iu-Cans-CA-SiqiniqNeural` | Female | Friendly, Positive |
| iu-Cans-CA | Inuktitut | `iu-Cans-CA-TaqqiqNeural` | Male | Friendly, Positive |
| iu-Latn-CA | Inuktitut | `iu-Latn-CA-SiqiniqNeural` | Female | Friendly, Positive |
| iu-Latn-CA | Inuktitut | `iu-Latn-CA-TaqqiqNeural` | Male | Friendly, Positive |
| ja-JP | Japanese | `ja-JP-NanamiNeural` | Female | Friendly, Positive |
| ja-JP | Japanese | `ja-JP-KeitaNeural` | Male | Friendly, Positive |
| jv-ID | Javanese | `jv-ID-SitiNeural` | Female | Friendly, Positive |
| jv-ID | Javanese | `jv-ID-DimasNeural` | Male | Friendly, Positive |
| ka-GE | Georgian | `ka-GE-EkaNeural` | Female | Friendly, Positive |
| ka-GE | Georgian | `ka-GE-GiorgiNeural` | Male | Friendly, Positive |
| kk-KZ | Kazakh | `kk-KZ-AigulNeural` | Female | Friendly, Positive |
| kk-KZ | Kazakh | `kk-KZ-DauletNeural` | Male | Friendly, Positive |
| km-KH | Khmer | `km-KH-SreymomNeural` | Female | Friendly, Positive |
| km-KH | Khmer | `km-KH-PisethNeural` | Male | Friendly, Positive |
| kn-IN | Kannada | `kn-IN-SapnaNeural` | Female | Friendly, Positive |
| kn-IN | Kannada | `kn-IN-GaganNeural` | Male | Friendly, Positive |
| ko-KR | Korean | `ko-KR-SunHiNeural` | Female | Friendly, Positive |
| ko-KR | Korean | `ko-KR-HyunsuMultilingualNeural` | Male | Friendly, Positive |
| ko-KR | Korean | `ko-KR-InJoonNeural` | Male | Friendly, Positive |
| lo-LA | Lao | `lo-LA-KeomanyNeural` | Female | Friendly, Positive |
| lo-LA | Lao | `lo-LA-ChanthavongNeural` | Male | Friendly, Positive |
| lt-LT | Lithuanian | `lt-LT-OnaNeural` | Female | Friendly, Positive |
| lt-LT | Lithuanian | `lt-LT-LeonasNeural` | Male | Friendly, Positive |
| lv-LV | Latvian | `lv-LV-EveritaNeural` | Female | Friendly, Positive |
| lv-LV | Latvian | `lv-LV-NilsNeural` | Male | Friendly, Positive |
| mk-MK | Macedonian | `mk-MK-MarijaNeural` | Female | Friendly, Positive |
| mk-MK | Macedonian | `mk-MK-AleksandarNeural` | Male | Friendly, Positive |
| ml-IN | Malayalam | `ml-IN-SobhanaNeural` | Female | Friendly, Positive |
| ml-IN | Malayalam | `ml-IN-MidhunNeural` | Male | Friendly, Positive |
| mn-MN | Mongolian | `mn-MN-YesuiNeural` | Female | Friendly, Positive |
| mn-MN | Mongolian | `mn-MN-BataaNeural` | Male | Friendly, Positive |
| mr-IN | Marathi | `mr-IN-AarohiNeural` | Female | Friendly, Positive |
| mr-IN | Marathi | `mr-IN-ManoharNeural` | Male | Friendly, Positive |
| ms-MY | Malay | `ms-MY-YasminNeural` | Female | Friendly, Positive |
| ms-MY | Malay | `ms-MY-OsmanNeural` | Male | Friendly, Positive |
| mt-MT | Maltese | `mt-MT-GraceNeural` | Female | Friendly, Positive |
| mt-MT | Maltese | `mt-MT-JosephNeural` | Male | Friendly, Positive |
| my-MM | Burmese | `my-MM-NilarNeural` | Female | Friendly, Positive |
| my-MM | Burmese | `my-MM-ThihaNeural` | Male | Friendly, Positive |
| nb-NO | Norwegian | `nb-NO-PernilleNeural` | Female | Friendly, Positive |
| nb-NO | Norwegian | `nb-NO-FinnNeural` | Male | Friendly, Positive |
| ne-NP | Nepali | `ne-NP-HemkalaNeural` | Female | Friendly, Positive |
| ne-NP | Nepali | `ne-NP-SagarNeural` | Male | Friendly, Positive |
| nl-BE | Dutch | `nl-BE-DenaNeural` | Female | Friendly, Positive |
| nl-BE | Dutch | `nl-BE-ArnaudNeural` | Male | Friendly, Positive |
| nl-NL | Dutch | `nl-NL-ColetteNeural` | Female | Friendly, Positive |
| nl-NL | Dutch | `nl-NL-FennaNeural` | Female | Friendly, Positive |
| nl-NL | Dutch | `nl-NL-MaartenNeural` | Male | Friendly, Positive |
| pl-PL | Polish | `pl-PL-ZofiaNeural` | Female | Friendly, Positive |
| pl-PL | Polish | `pl-PL-MarekNeural` | Male | Friendly, Positive |
| ps-AF | Pashto | `ps-AF-LatifaNeural` | Female | Friendly, Positive |
| ps-AF | Pashto | `ps-AF-GulNawazNeural` | Male | Friendly, Positive |
| pt-BR | Portuguese | `pt-BR-FranciscaNeural` | Female | Friendly, Positive |
| pt-BR | Portuguese | `pt-BR-ThalitaMultilingualNeural` | Female | Friendly, Positive |
| pt-BR | Portuguese | `pt-BR-AntonioNeural` | Male | Friendly, Positive |
| pt-PT | Portuguese | `pt-PT-RaquelNeural` | Female | Friendly, Positive |
| pt-PT | Portuguese | `pt-PT-DuarteNeural` | Male | Friendly, Positive |
| ro-RO | Romanian | `ro-RO-AlinaNeural` | Female | Friendly, Positive |
| ro-RO | Romanian | `ro-RO-EmilNeural` | Male | Friendly, Positive |
| ru-RU | Russian | `ru-RU-SvetlanaNeural` | Female | Friendly, Positive |
| ru-RU | Russian | `ru-RU-DmitryNeural` | Male | Friendly, Positive |
| si-LK | Sinhala | `si-LK-ThiliniNeural` | Female | Friendly, Positive |
| si-LK | Sinhala | `si-LK-SameeraNeural` | Male | Friendly, Positive |
| sk-SK | Slovak | `sk-SK-ViktoriaNeural` | Female | Friendly, Positive |
| sk-SK | Slovak | `sk-SK-LukasNeural` | Male | Friendly, Positive |
| sl-SI | Slovenian | `sl-SI-PetraNeural` | Female | Friendly, Positive |
| sl-SI | Slovenian | `sl-SI-RokNeural` | Male | Friendly, Positive |
| so-SO | Somali | `so-SO-UbaxNeural` | Female | Friendly, Positive |
| so-SO | Somali | `so-SO-MuuseNeural` | Male | Friendly, Positive |
| sq-AL | Albanian | `sq-AL-AnilaNeural` | Female | Friendly, Positive |
| sq-AL | Albanian | `sq-AL-IlirNeural` | Male | Friendly, Positive |
| sr-RS | Serbian | `sr-RS-SophieNeural` | Female | Friendly, Positive |
| sr-RS | Serbian | `sr-RS-NicholasNeural` | Male | Friendly, Positive |
| su-ID | Sundanese | `su-ID-TutiNeural` | Female | Friendly, Positive |
| su-ID | Sundanese | `su-ID-JajangNeural` | Male | Friendly, Positive |
| sv-SE | Swedish | `sv-SE-SofieNeural` | Female | Friendly, Positive |
| sv-SE | Swedish | `sv-SE-MattiasNeural` | Male | Friendly, Positive |
| sw-KE | Swahili | `sw-KE-ZuriNeural` | Female | Friendly, Positive |
| sw-KE | Swahili | `sw-KE-RafikiNeural` | Male | Friendly, Positive |
| sw-TZ | Swahili | `sw-TZ-RehemaNeural` | Female | Friendly, Positive |
| sw-TZ | Swahili | `sw-TZ-DaudiNeural` | Male | Friendly, Positive |
| ta-IN | Tamil | `ta-IN-PallaviNeural` | Female | Friendly, Positive |
| ta-IN | Tamil | `ta-IN-ValluvarNeural` | Male | Friendly, Positive |
| ta-LK | Tamil | `ta-LK-SaranyaNeural` | Female | Friendly, Positive |
| ta-LK | Tamil | `ta-LK-KumarNeural` | Male | Friendly, Positive |
| ta-MY | Tamil | `ta-MY-KaniNeural` | Female | Friendly, Positive |
| ta-MY | Tamil | `ta-MY-SuryaNeural` | Male | Friendly, Positive |
| ta-SG | Tamil | `ta-SG-VenbaNeural` | Female | Friendly, Positive |
| ta-SG | Tamil | `ta-SG-AnbuNeural` | Male | Friendly, Positive |
| te-IN | Telugu | `te-IN-ShrutiNeural` | Female | Friendly, Positive |
| te-IN | Telugu | `te-IN-MohanNeural` | Male | Friendly, Positive |
| th-TH | Thai | `th-TH-PremwadeeNeural` | Female | Friendly, Positive |
| th-TH | Thai | `th-TH-NiwatNeural` | Male | Friendly, Positive |
| tr-TR | Turkish | `tr-TR-EmelNeural` | Female | Friendly, Positive |
| tr-TR | Turkish | `tr-TR-AhmetNeural` | Male | Friendly, Positive |
| uk-UA | Ukrainian | `uk-UA-PolinaNeural` | Female | Friendly, Positive |
| uk-UA | Ukrainian | `uk-UA-OstapNeural` | Male | Friendly, Positive |
| ur-IN | Urdu | `ur-IN-GulNeural` | Female | Friendly, Positive |
| ur-IN | Urdu | `ur-IN-SalmanNeural` | Male | Friendly, Positive |
| ur-PK | Urdu | `ur-PK-UzmaNeural` | Female | Friendly, Positive |
| ur-PK | Urdu | `ur-PK-AsadNeural` | Male | Friendly, Positive |
| uz-UZ | Uzbek | `uz-UZ-MadinaNeural` | Female | Friendly, Positive |
| uz-UZ | Uzbek | `uz-UZ-SardorNeural` | Male | Friendly, Positive |
| vi-VN | Vietnamese | `vi-VN-HoaiMyNeural` | Female | Friendly, Positive |
| vi-VN | Vietnamese | `vi-VN-NamMinhNeural` | Male | Friendly, Positive |
| zh-CN | Chinese | `zh-CN-XiaoxiaoNeural` | Female | Warm |
| zh-CN | Chinese | `zh-CN-XiaoyiNeural` | Female | Lively |
| zh-CN | Chinese | `zh-CN-YunjianNeural` | Male | Passion |
| zh-CN | Chinese | `zh-CN-YunxiNeural` | Male | Lively, Sunshine |
| zh-CN | Chinese | `zh-CN-YunxiaNeural` | Male | Cute |
| zh-CN | Chinese | `zh-CN-YunyangNeural` | Male | Professional, Reliable |
| zh-CN-liaoning | Chinese | `zh-CN-liaoning-XiaobeiNeural` | Female | Humorous |
| zh-CN-shaanxi | Chinese | `zh-CN-shaanxi-XiaoniNeural` | Female | Bright |
| zh-HK | Chinese | `zh-HK-HiuGaaiNeural` | Female | Friendly, Positive |
| zh-HK | Chinese | `zh-HK-HiuMaanNeural` | Female | Friendly, Positive |
| zh-HK | Chinese | `zh-HK-WanLungNeural` | Male | Friendly, Positive |
| zh-TW | Chinese | `zh-TW-HsiaoChenNeural` | Female | Friendly, Positive |
| zh-TW | Chinese | `zh-TW-HsiaoYuNeural` | Female | Friendly, Positive |
| zh-TW | Chinese | `zh-TW-YunJheNeural` | Male | Friendly, Positive |
| zu-ZA | Zulu | `zu-ZA-ThandoNeural` | Female | Friendly, Positive |
| zu-ZA | Zulu | `zu-ZA-ThembaNeural` | Male | Friendly, Positive |
