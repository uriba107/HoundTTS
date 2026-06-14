# src/providers/shared — Shared Auth Helpers

## Purpose

Reusable authentication logic shared by TTS and translation providers.

## Ownership

| Directory | Provides |
|-----------|----------|
| google/ | Service-account JWT auth (used by Google TTS + Google Translate) |
| aws/ | AWS Signature V4 signing (used by AWS Polly + AWS Translate) |
| azure/ | Azure auth header generation (used by Azure TTS + Azure Translate) |

## Local Contracts

- Headers are included by providers directly — no dispatch layer
- No ConfigReader dependency; auth data is passed in by the calling provider

## Work Guidance

- Keep auth logic self-contained and reusable across providers
- Do not add provider-specific logic here
