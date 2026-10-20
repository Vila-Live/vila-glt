# AGENTS.md

## What this repo is

A native **Windows** OBS Studio plugin that does real-time speech-to-speech
translation via the Google Gemini Live API (`gemini-3.5-live-translate-preview`).
An OBS audio source's audio is streamed to Gemini and the translated audio is
emitted as a separate OBS audio source that can live on its own track. Several
translations (one per input) can run at once.

**This repo is a fork** of `weisunglee/obs-live-translate` (remote `upstream`,
branched from its v0.1.6), owned by `nklisic`. It has diverged on purpose —
per-source sessions instead of the mic filter + singleton, key storage in
`.env`, the JSON model table — so do not "restore" upstream behavior. See
`README.md` for the architecture and behavior. Your job is **maintenance and
incremental changes**, not a green-field build.

## Build / test / deploy (Windows x64)

This is a Windows-only build (needs libobs). Do **not** try to build on a
non-Windows filesystem — there is no libobs there. `cmake` from Visual Studio is
not on the non-interactive PATH; prepend it:

```powershell
set "PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cmake --preset windows-x64                          # configure (first run fetches obs-deps)
cmake --build --preset windows-x64                  # plugin DLL + tests
cmake --build --preset windows-x64 --target unit-tests   # pure-logic tests, no libobs
ctest --test-dir build_x64 --output-on-failure
```

Install: copy `build_x64\RelWithDebInfo\obs-vila-glt.dll` to
`C:\Program Files\obs-studio\obs-plugins\64bit\` and the `data\` folder to
`C:\Program Files\obs-studio\data\obs-plugins\obs-vila-glt\` (OBS must be
closed). The DLL alone also works — UI texts then fall back to English and the
model table to the copy embedded in the DLL.

UI strings: use `T("Key", "English fallback")` in `source.cpp` and add the key
to every file in `data/locale/` (`en-US`, `sr-CS` Latin, `sr-SP` Cyrillic).

## Rules

- **Test-driven.** For any change: write the failing test → confirm it fails →
  write the minimal implementation → confirm it passes → commit. Pure logic lives
  behind the libobs-free `unit-tests` target so it can be tested without OBS;
  changes that touch libobs (`source.cpp`, `translation-session`)
  are verified by a full plugin build.
- **Do not skip verification.** If a build/test can't run because a prerequisite
  (e.g. libobs) is missing, stop and report what is missing — don't claim done.
- **Commits**: use the repo owner's configured git identity (`nklisic`) — never
  author as someone else — and never add Claude/AI trailers. Our work lives on
  `master`; `main` mirrors upstream and is not committed to. Do not push or
  publish anything unless explicitly told.
- Keep changes scoped. The non-goals (captions, one input into several languages
  at once, explicit source-language selection) remain out of scope.

## Architecture (quick map)

- `source.cpp` — translated-audio source, the only registered OBS source. Input:
  an audio capture callback on the selected source (post-filter, pre-fader) →
  resample to 16 kHz mono → chunk → hand to its `TranslationSession`. Output:
  event-driven push loop that drains the session's output buffer and pushes to
  `obs_source_output_audio`, paced by `OutputTimestamper` to keep a bounded
  (~600 ms) scheduling lead. When a session stops on a terminal error (fatal
  auth/quota/config error or after all reconnect attempts fail), the push loop
  hides that source's scene items in **every** scene, groups included
  (`hide_source_in_scenes` → `obs_sceneitem_set_visible(false)`), so the
  operator sees the eye icon turn off in the Sources list during a live
  broadcast, while the red error status stays in Properties until the source is
  shown again. `source_deactivate` only sets flags, so hiding from the push-loop
  thread does not deadlock.
  Source-list icon: `info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT` (speaker).
- `translation-session.*` — one per source: WebSocket to Gemini (`IXWebSocket`
  with its internal auto-reconnect disabled), input/output ring buffers,
  output-ready notification + interrupt. Waits for `SetupComplete` before
  draining input audio. On a transient disconnect (`classify_disconnect` in
  `live-protocol.*`), reconnects immediately (`0 ms`) on the first drop of an
  established session and then with exponential backoff up to
  `kMaxReconnectAttempts` (5) times. Fatal errors (`QuotaExceeded`, `AuthError`,
  `ConfigError`) stop immediately without retrying.
- `api-key.*` — API keys shared by all sources, one variable per provider (e.g.
  `GEMINI_API_KEY`) in `<OBS config folder>/.env` (never in source settings / the
  scene collection). Plain text by the owner's decision: no OS-specific secret
  stores.
- `data/models.json` — the model + language table, embedded into the plugin by
  `cmake/common/embed-models.cmake`; `model-store.*` loads it plus optional
  override files. Adding a model for an existing provider is a JSON change; a
  new provider needs a protocol implementation.
- `model-catalog.*`, `dotenv.*`, `session-config.*`, `audio-pacing.*`, `audio-convert.*`, `ring-buffer.*`,
  `backoff.*`, `live-protocol.*`, `output-delay.*`, `base64.*` — pure modules,
  each with a Catch2 test (`live-protocol.*` also holds `classify_disconnect`
  for WebSocket close codes, HTTP statuses, and JSON error payloads).

## Current state (2026-10-20)

- Version `0.2.0` (`buildspec.json`), module/DLL name `obs-vila-glt`, source id
  still `gemini_translated_audio_source` (same as upstream — never install both
  DLLs). Built and tested on Windows only.
- Not published: no `origin` remote, no GitHub fork yet, no release/installer
  build. Wait for the owner's word before any of that.
- History: initial redesign 2026-10-02 (Claude Code); reconnect/error handling,
  rename to `obs-vila-glt` and icon assets 2026-10-19 (Google Antigravity).
- Verified by the owner in OBS: single source translating a browser source
  (lag < 1 s steady state). **Not yet verified in OBS:** reconnect/stop on a real
  drop or exhausted credits, several sources at once, two-way guest setup
  (VB-Cable + VDO.Ninja), behaviour of the auto-hide.
- Open items (ask before starting any of them):
  - Custom Source-list icon — the owner will show how it is done; until then
    `media/icon.*` is used only by the README and the installer script.
  - Windows installer (`installer/obs-vila-glt.iss`, Inno Setup) — not built yet.
  - Warning when Desktop Audio is chosen as input (it can capture the monitored
    translation and loop).
  - Candidate feature, not decided: captions via `outputAudioTranscription` /
    `inputAudioTranscription`.
  - Log prefix is still `[live-translate]` in `translation-session.cpp`,
    `api-key.cpp`, `model-store.cpp` (`plugin-main.cpp` uses `[obs-vila-glt]`).
- `docs/superpowers/` holds upstream's design notes for the single-session guard,
  which this fork removed; historical only.

## Key technical facts (verified against Google's docs)

- Model: `models/gemini-3.5-live-translate-preview`, WebSocket (TLS) endpoint.
- Input audio to Gemini: 16 kHz, 16-bit PCM, mono, little-endian, 100 ms chunks
  (3200 bytes/chunk).
- Output audio from Gemini: 24 kHz, 16-bit PCM, mono.
- `translationConfig` has only `targetLanguageCode` (BCP-47) and
  `echoTargetLanguage`. **No source-language param** — the model auto-detects the
  input language.
- The output stream is continuous (the model emits even during silence) and does
  **not** send `turnComplete`/`generationComplete`/`interrupted` in this mode.
  Perceived cut-offs at sentence ends are the model's own phrase-boundary cadence,
  not a plugin bug — the plugin's delivery is gap-free.
