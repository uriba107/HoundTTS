# src/providers/translate — Translation Providers

## Purpose

Text translation implementations. Each provider is a flat directory exposing a static `Translate()` function with an async polling pattern.

## Ownership

| Provider | Description |
|----------|-------------|
| openai/ | OpenAI chat completions (brevity-code-preserving) |
| google/ | Google Cloud Translation API v2 |
| libretranslate/ | LibreTranslate HTTP API (self-hosted) |
| aws/ | Amazon Translate |
| azure/ | Azure AI Translator |

## Local Contracts

- Async pattern: `static std::string Translate(text, lang, ...)` — blocking on DLL bg thread, polled via Lua timer
- Config via `ConfigReader` singleton
- Brevity-code preservation only in OpenAI provider

## Work Guidance

- Follow existing pattern: enum in `provider.h` → Translate static → dispatch in `tts_pipeline.cpp` → CMakeLists.txt
- Return empty string on failure (caller falls back to original text)

## Verification

No test framework. Test from DCS via `HoundTTS.Translate()`.

## Child DOX Index

None.
