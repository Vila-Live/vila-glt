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

Warnings: our targets (plugin, `unit-tests`) build at `/W4` with warnings as
errors (the template's default); third-party code is `SYSTEM` and IXWebSocket's
sources use `/w`. Do not configure with `-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF` on
Windows — fix the warning instead.

Worktrees: the template rebuilds libobs inside `.deps` on every configure and
records absolute paths there (`build_x64` and `build_x86` caches). Never point a
worktree's `.deps` at the main checkout's (junction/symlink): give it its own
copy or a fresh download.

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
  A plugin cannot supply its own Sources-list icon: OBS picks the theme's icon
  for the type, and `OBS_ICON_TYPE_CUSTOM` falls back to the default icon
  (`//TODO: Add ability for sources to define custom icons` in OBS 31.1.1
  `frontend/widgets/OBSBasic_Icons.cpp`). Do not patch the Qt source tree to
  work around it.
- `translation-session.*` — one per source: WebSocket to Gemini (`IXWebSocket`
  with its internal auto-reconnect disabled), input/output ring buffers,
  output-ready notification + interrupt. Waits for `SetupComplete` before
  draining input audio. Disconnects are sorted by `classify_disconnect`
  (`live-protocol.*`) following Google's error docs: `Transient` (drops, 5xx,
  408) and `RateLimited` (429, quota) are retried; `AuthError` (401/403, bad
  key), `OutOfCredit` (402, prepaid credits/billing) and `ConfigError` (other
  4xx, unknown model, 501) stop at once. When to retry is decided by
  `ReconnectPolicy` (`reconnect-policy.*`): the first drop of a session that ran
  at least `kStableSessionMs` (30 s) after setup reconnects immediately (covers
  the server's periodic connection reset); any other failure — including a
  session that drops soon after setup — waits 1, 2, 4, 8, 16 s and after
  `kMaxReconnectAttempts` (5) gives up. Giving up while still rate-limited shows
  the quota status (most likely the daily quota).
- `api-key.*` — API keys shared by all sources, one variable per provider (e.g.
  `GEMINI_API_KEY`) in `<OBS config folder>/.env` (never in source settings / the
  scene collection). Plain text by the owner's decision: no OS-specific secret
  stores.
- `data/models.json` — the model + language table, embedded into the plugin by
  `cmake/common/embed-models.cmake`; `model-store.*` loads it plus optional
  override files. Adding a model for an existing provider is a JSON change; a
  new provider needs a protocol implementation.
- `model-catalog.*`, `dotenv.*`, `session-config.*`, `audio-pacing.*`, `audio-convert.*`, `ring-buffer.*`,
  `backoff.*`, `reconnect-policy.*`, `live-protocol.*`, `output-delay.*`, `base64.*` — pure modules,
  each with a Catch2 test (`live-protocol.*` also holds `classify_disconnect`
  for WebSocket close codes, HTTP statuses, and JSON error payloads).

## Current state (2026-11-11)

- Version `0.2.0` (`buildspec.json`), module/DLL name `obs-vila-glt`, source id
  still `gemini_translated_audio_source` (same as upstream — never install both
  DLLs). Built and tested on Windows only.
- Not published: no `origin` remote, no GitHub fork yet, no release. Wait for
  the owner's word before any of that.
- Installer: Inno Setup 6.7.3 is installed per-user at
  `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe` (CI uses `choco install
  innosetup`, path `C:\Program Files (x86)\Inno Setup 6`). Build with
  `ISCC /Obuild_x64\installer installer\obs-vila-glt.iss` — `build_x64` is
  git-ignored. First built 2026-10-20. Run by the owner 2026-10-20 (Apps entry,
  name, version, icon OK; no Start-menu entry is expected) and again
  2026-11-10 on OBS 33.0.1: installs the DLL and `data\` (`models.json`, the
  three locales, `icon.ico`) correctly.
- History: initial redesign 2026-10-02 (Claude Code); reconnect/error handling,
  rename to `obs-vila-glt` and icon assets 2026-10-19 (Google Antigravity).
- Verified by the owner in OBS: single source translating a browser source
  (lag < 1 s steady state).
- **OBS test 2026-11-10** (owner + Claude Code), OBS 33.0.1 with the DLL built
  against the 31.1.1 SDK — loads with no warnings. Input: Media Source playing
  the owner's agreed test file (a 2 h 10 min Russian TV show with music, a
  Twitch VOD), target `sr`. Results from the OBS log:
  - ~32 min of open sessions, 13 session starts (language changes, owner
    pausing by hiding the source); setup completes in 0.16–0.67 s every time;
    no plugin errors, no audio-buffering growth, clean unload on OBS exit.
  - **Server session cap seen live:** after 9 min 51 s the server sent GoAway
    and closed with 1008 ("Connection aborted because the client failed to
    close the connection after receiving a GoAway signal once the session
    durat…" — reason cut at the WebSocket 123-byte limit). The plugin
    reconnected immediately (attempt 1/5, wait 0 ms); translation gap ≈ 0.3 s.
    Reconnect after the periodic reset is therefore **verified**. GoAway
    handling / a second overlapping session is **rejected** by the owner
    (2026-10-19, reconfirmed 2026-11-10) — do not propose it again.
  - "Cold start" (owner's term) = the translator's voice (timbre/tone) is not
    stable for the first seconds of every new session, so it recurs about
    every 10 min. Model-side, no voice parameter exists; owner rates it minor.
    Not audio loss: input captured while connecting is buffered and sent after
    `setupComplete`.
  - Sung lyrics over music are translated too (model behaviour, no option to
    skip). The owner pauses the translation (hides the source) during songs.
  - Cost: about $2 for those ~32 min (~$0.06/min, ~$4/h). Billing follows open
    session time, silence and music included — pausing during songs saves.
  - Recording in Simple output mode keeps only Track 1 (original + translation
    mixed); use Advanced output with separate tracks if the translation must be
    analysed on its own.
- **Not yet verified in OBS:** wrong API key (immediate stop + auto-hide),
  short network drop 5–10 s (Reconnecting n/5, then resume), drop > 30 s
  (5 attempts, then stop + hide), exhausted credits, several sources at once,
  two-way guest setup (VB-Cable + VDO.Ninja).
- OBS facts checked 2026-11-10 (do not re-derive): a new scene collection is
  always created empty ("clean"); global Desktop Audio / Mic/Aux are made only
  on OBS's very first run (`FirstRun` in `user.ini`). Since OBS 32.0, a source
  set to "Monitor and Output" whose monitoring device is also captured (e.g. by
  Desktop Audio) is not duplicated in the output. Monitoring the translation is
  safe; a loop happens only if the translation's input is Desktop Audio.
- Open items (ask before starting any of them):
  - Icon (2026-10-20): Material Symbols `interpreter_mode` (Rounded, fill,
    weight 700), chosen over `g_translate` so the plugin does not look like a
    Google product. `media/icon.svg` (optical size 48) and `icon-small.svg`
    (optical size 20, used for 16/24 px) are the sources, with a viewBox
    widened because the Material path overshoots 0..960. `icon.png` (blue
    #1A73E8, light backgrounds), `icon-light.png` (#E3E3E3, dark backgrounds),
    `icon.ico` (blue, 16–256 px) are generated from them by
    `tools/build-icons.py` (needs `pillow` and `resvg-py` in a venv; running it
    with no arguments rebuilds everything from `media/` byte-identically,
    `preview` renders a size/colour sheet). Used by the README
    (`<picture>`) and the installer (setup icon verified in the built .exe;
    Windows "Apps" icon verified by the owner 2026-10-20).
  - Warning when Desktop Audio is chosen as input (it can capture the monitored
    translation and loop).
  - Candidate feature, not decided: captions via `outputAudioTranscription` /
    `inputAudioTranscription`.

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
- Sessions are capped at about 10 min (observed 9 min 51 s on 2026-11-10): the
  server sends GoAway, then closes with 1008 if the client keeps the
  connection open. The plugin ignores GoAway by design and reconnects at once.
