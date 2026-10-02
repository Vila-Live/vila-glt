<p align="center">
  <img src="icon.png" alt="Gemini Live Translate for OBS" width="120">
</p>

# Gemini Live Translate for OBS

[![Latest release](https://img.shields.io/github/v/release/weisunglee/obs-live-translate?sort=semver)](https://github.com/weisunglee/obs-live-translate/releases)
[![Release build](https://github.com/weisunglee/obs-live-translate/actions/workflows/release.yaml/badge.svg)](https://github.com/weisunglee/obs-live-translate/actions/workflows/release.yaml)
[![License: GPL v2](https://img.shields.io/badge/license-GPLv2-blue.svg)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)

A native OBS Studio plugin (**Windows · macOS · Linux**) that does real-time
**speech-to-speech** translation using the Google **Gemini Live API**
(`gemini-3.5-live-translate-preview`). It captures a microphone's audio, streams
it to Gemini, and plays the translated speech back as a separate OBS audio
source you can route to its own track — so a stream/recording can carry both the
original voice and a live translation.

> This plugin was built with significant assistance from AI (Claude). It is an
> independent project and is not affiliated with or endorsed by the OBS Project
> or Google. Bug reports and contributions are welcome.

## How it works

The plugin registers one OBS source, **Live Translate** *(audio
source)*. Add one per audio source you want translated and pick that source as
its **Source to translate** — a microphone, a browser source, a media source, any
OBS source that produces audio.

It taps the input's audio (after the input's own filters, before its fader, so
lowering the original in the mixer does not change what Gemini hears), resamples
it to 16 kHz mono 16-bit PCM, chunks it (100 ms / 3200-byte chunks), and streams
it **continuously** to Gemini over a TLS WebSocket — including the silence
during pauses, which the model relies on to detect when an utterance ends and
emit its translation promptly. The 24 kHz translated PCM that comes back is
pushed into the OBS mixer as this source's own audio. Configure the **input**,
**API key**, **target language**, **echo** option, and optional **playback
delay** in its properties.

```
input audio source (mic, browser, media, …)
   │ audio capture callback (post-filter, pre-fader; original is untouched)
   ▼
[Live Translate source]
   resample 16 kHz mono → chunk → WebSocket ─▶ Gemini Live API
                                                   │ 24 kHz PCM
   event-driven push loop ◀── ring buffer ◀────────┘
   → obs_source_output_audio → OBS mixer
```

Each source owns its own `TranslationSession` — the WebSocket connection (with
reconnect/backoff) and the input/output audio buffers — so several sources can
translate different inputs side by side. The output path pushes
every received PCM chunk to OBS with contiguous, duration-spaced timestamps,
paced so the scheduling lead stays bounded (~600 ms, enough to ride out the
model's phrase-boundary delivery jitter) — the OBS mixer is the clock.

## Getting a Gemini API key

The plugin needs a Google **Gemini API key**:

1. Sign in to **Google AI Studio** at <https://aistudio.google.com/apikey> with
   a Google account.
2. New users: AI Studio creates a default Google Cloud project and key
   automatically once you accept the Terms of Service. Otherwise click
   **Create API key**.
3. In the properties of any *Live Translate* source click **Edit** next to
   *API key*, paste the key into the field that opens and click **Save API
   key**. The line then reads *API key: saved (...xxxx)*, showing only the last
   four characters. One key is shared by all sources.

Notes:

- The key is **not** stored in the scene collection. It is saved as
  `GEMINI_API_KEY=...` in a `.env` file in the OBS config folder
  (`%APPDATA%\obs-studio\.env` on Windows, `~/.config/obs-studio/.env` on Linux,
  `~/Library/Application Support/obs-studio/.env` on macOS). It is never put
  back into the properties and cannot be read or set through OBS WebSocket. One
  key per provider serves every source in every scene collection; the model
  table names the variable each model uses.
- The `.env` file is **plain text**, the same on every platform — protect it
  like a password file (on Linux/macOS it is created readable by you only). You
  can also create or edit it by hand; it is read when OBS starts. Other
  variables in the file are left untouched.
- This guards against leaking the key by sharing scenes or through OBS
  WebSocket; it does not stop anyone who can read your OBS config folder.
- **Edit** → **Forget API key** removes it.
- A scene collection saved by an older version of the plugin still contains the
  key in plaintext (also in OBS's `.bak` copies) until it is saved again; on
  load that key is moved to the `.env` file if none is stored yet.
- `gemini-3.5-live-translate-preview` usage is billed per Google's pricing;
  check current quotas and pricing in AI Studio
  ([pricing](https://ai.google.dev/gemini-api/docs/pricing)).

## Status

Released for **Windows, macOS and Linux** — grab a prebuilt package from the
[Releases](https://github.com/weisunglee/obs-live-translate/releases) page
(Windows is the tested platform; see the note under *Install*). Current behavior:

- ✅ Any audio source → Gemini streaming (continuous, including pause silence),
  translated audio played back via the OBS mixer.
- ✅ Reconnect with exponential backoff; live API-key / target-language changes.
- ✅ Event-driven push output with a bounded scheduling lead (~600 ms) to ride
  out the model's phrase-boundary delivery jitter.
- ✅ **Live translate model** list, driven by [`data/models.json`](data/models.json)
  (see *Models and languages*).
- ✅ Optional **Repeat speech that is already in the target language** (echo,
  default off): with it off, input already in the target language stays silent;
  with it on, the model repeats it.
- ✅ Optional **Delay for translated audio** (0-30 seconds): hold the translated
  audio so every translated phrase plays later by the configured amount. The
  original is never delayed. Only useful when the picture is delayed too.
- ✅ Sentence endings play in full — streaming the mic continuously (silence
  included) lets the model detect when an utterance ends and emit it promptly,
  instead of holding it until the next one starts.
- ✅ One translation stream per *Live Translate* source. Add several to
  translate several inputs at once (e.g. a host plus guests in different
  languages); each has its own connection, language and settings. Other
  *Live Translate* sources are not offered as inputs, so a translation
  is never translated again.
- ✅ Hiding the source is the off switch. A session only runs while its *Live
  Translate* source is active in the program: hide it (eye icon, or an
  OBS show/hide hotkey) or switch to a scene without it and the connection is
  closed, so nothing is streamed to Gemini. Showing it again reconnects, which
  takes about a second.
- ✅ A muted input is sent as silence, so muting the original also stops its
  translation. Lowering the input's fader does not affect the translation.
- ⚠️ The translated audio lags your speech by a few seconds — the model's
  translation latency plus a fixed ~600 ms smoothing buffer (which doesn't
  accumulate; measured backlog stays <50 ms). Tip: when **recording**, pause a
  beat after your last sentence before hitting stop so the trailing translation
  is captured. **Live streams** aren't affected.

## Install (prebuilt)

Download the package for your platform from the
[Releases](https://github.com/weisunglee/obs-live-translate/releases) page, **with
OBS closed**:

- **Windows** — run `…-windows-x64-installer.exe` (it detects your OBS install
  automatically), or extract `…-windows-x64.zip` into your OBS Studio directory
  (e.g. `C:\Program Files\obs-studio\`). Unsigned, so SmartScreen may warn.
- **macOS** — unzip `…-macos-universal.zip` and copy `obs-live-translate.plugin`
  into `~/Library/Application Support/obs-studio/plugins/`. Unsigned / not
  notarized; if Gatekeeper blocks it, run
  `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-live-translate.plugin`.
- **Linux** — extract `…-linux-x86_64.tar.gz` into `~/.config/obs-studio/plugins/`
  (the `.so` should end up at
  `~/.config/obs-studio/plugins/obs-live-translate/bin/64bit/obs-live-translate.so`).

> The macOS and Linux packages are produced by CI but **not yet verified on those
> platforms** — feedback is welcome. Windows is the tested platform.

## Usage

1. Add a **Live Translate** source to your scene (Sources → **+** →
   *Live Translate*).

2. In its properties pick the **Source to translate**, the **Live translate
   model** and the **Target language**, and set the API key once (**Edit**).
   Once it connects the status reads *Connected* (reopen the properties to
   refresh it).

3. The source plays the translated voice. Mix it against the original in the
   OBS mixer (e.g. lower the original under the translation for a voice-over),
   or route it to its own track in *Advanced Audio Properties*.

Repeat for every audio source you want translated.

> The screenshots in `screenshots/` still show the earlier filter-based setup.

## Remote control (OBS WebSocket)

The source's settings are plain OBS source settings, so you can change the
**target language**, **echo** option, and **playback delay** live from any OBS WebSocket v5 client
(scripts, [`obs-cli`](https://github.com/muesli/obs-cli), Advanced Scene
Switcher, your own app) using the `SetInputSettings` request — no extra
plugin support needed:

```json
{ "requestType": "SetInputSettings",
  "requestData": {
    "inputName": "Live Translate",
    "inputSettings": { "target_lang": "ja" } } }
```

| setting | type | meaning |
|---|---|---|
| `input_source` | string | UUID of the OBS source to translate (empty stops the session) |
| `model` | string | model `id` from [`data/models.json`](data/models.json) |
| `target_lang` | string | language code from [`data/models.json`](data/models.json) (e.g. `en`, `zh`, `ja`, `pt-BR`) — the value, not the display name |
| `echo_target` | bool | repeat speech that is already in the target language |
| `playback_delay` | number | seconds to delay the translated audio stream, clamped to 0-30 |

Notes:

- `inputName` must match your OBS source name exactly.
- Changing `model`, `target_lang` or `echo_target` reconnects the session, so
  expect a brief gap. Changing `input_source` or `playback_delay` does not.
- The API key cannot be set or read this way; use the properties or the `.env`
  file.
- This is **set-only**. The runtime connection status (Connecting / Connected /
  API-key error) is not exposed over WebSocket — `GetInputSettings`
  returns the stored settings, not the live status.

## Models and languages

The **Live translate model** list and each model's languages come from
[`data/models.json`](data/models.json). The file is embedded into the plugin
when it is built, so the plugin works as a single binary.

```json
{ "models": [ {
    "id": "gemini-3.5-live-translate-preview",
    "name": "Gemini 3.5 Live Translate (preview)",
    "provider": "gemini",
    "api_key_env": "GEMINI_API_KEY",
    "supports_echo": true,
    "languages": [ { "code": "en", "name": "English" } ]
} ] }
```

To add or change a model without rebuilding, put a `models.json` of the same
shape into `plugin_config/obs-live-translate/` in the OBS config folder: a model
with an existing `id` replaces the built-in one, new ids are added. It is read
when OBS starts.

`provider` selects the protocol the plugin speaks. Only `gemini` is implemented;
a row with another provider is skipped until that protocol is added in code.
`api_key_env` is the variable in the `.env` file that holds that model's key.

## Build from source

Requires CMake ≥ 3.28 and a C++17 toolchain. Dependencies (nlohmann/json,
IXWebSocket + mbedTLS) are fetched by CMake; libobs/obs-deps come from
`buildspec.json` on Windows/macOS and from the system on Linux. The
`-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF` flag keeps third-party warnings from
failing the build.

**Windows** (Visual Studio 2022) — produces `build_x64\RelWithDebInfo\obs-live-translate.dll`:

```powershell
cmake --preset windows-x64 -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
cmake --build --preset windows-x64
```

**macOS** (Xcode 16+, universal) — produces `build_macos/RelWithDebInfo/obs-live-translate.plugin`:

```bash
cmake --preset macos -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
cmake --build --preset macos --target obs-live-translate
```

**Linux** — libobs comes from the obsproject PPA; install build deps first:

```bash
sudo add-apt-repository --yes ppa:obsproject/obs-studio
sudo apt-get update
sudo apt-get install -y obs-studio libsimde-dev libmbedtls-dev cmake ninja-build pkg-config
cmake --preset ubuntu-x86_64 -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
cmake --build --preset ubuntu-x86_64 --target obs-live-translate   # build_x86_64/obs-live-translate.so
```

(The exact CI steps live in [`.github/workflows/release.yaml`](.github/workflows/release.yaml).)

## Test

Use your platform's build directory (`build_x64` on Windows, `build_macos` on
macOS, `build_x86_64` on Linux). For example, on Windows:

```bash
# Build the libobs-free unit-test binary first
cmake --build --preset windows-x64 --target unit-tests

# Run the full unit-test suite
ctest --test-dir build_x64 --output-on-failure

# Run a single test case by name
ctest --test-dir build_x64 -R "<test case name>" --output-on-failure
```

The `unit-tests` target covers the pure logic (base64, ring buffer, backoff,
audio conversion, audio pacing/timestamper, Gemini protocol parsing) and does
**not** require libobs.

## Project layout

```
src/
  plugin-main.cpp        module entry; registers the source
  source.cpp             translated-audio source: input capture (resample →
                         chunk → stream) + event-driven output push loop
  translation-session.*  per-source WebSocket session + audio buffers + reconnect
  session-config.*       when a session runs / needs to reconnect
  model-catalog.*        parses/merges the model + language table
  model-store.*          loads the catalog (embedded JSON + optional overrides)
  api-key.*              API keys per provider, kept in <OBS config>/.env
  dotenv.*               minimal .env reading/editing
  live-protocol.*        build/parse Gemini Live API messages
  audio-pacing.*         OutputTimestamper (contiguous, lead-bounded timestamps)
  audio-convert.*        PCM downmix / conversion / chunking
  ring-buffer.*          bounded byte ring buffer
  backoff.*              exponential reconnect backoff
  base64.*
tests/                   Catch2 unit tests (one per pure module)
installer/               Inno Setup script for the Windows installer
cmake/, CMakePresets.json, buildspec.json
```

## Tech stack

C++17 · CMake (obs-plugintemplate) · libobs (OBS 31.x) · IXWebSocket (TLS via
mbedTLS) · nlohmann/json · Catch2.

## Key API facts

- Model: `models/gemini-3.5-live-translate-preview` over a TLS WebSocket.
- **Input** to Gemini: 16 kHz, 16-bit PCM, mono, little-endian, 100 ms chunks.
- **Output** from Gemini: 24 kHz, 16-bit PCM, mono.
- `translationConfig` takes `targetLanguageCode` (BCP-47) and
  `echoTargetLanguage`; there is **no source-language parameter** — the model
  auto-detects the spoken language.

## Supported languages

The **target language** dropdown offers the languages
`gemini-3.5-live-translate-preview` supports (from the
[official Live Translate docs](https://ai.google.dev/gemini-api/docs/live-api/live-translate)),
each labelled with its native name plus the English name (e.g. `日本語
(Japanese)`) so both native speakers and others can recognize it. Common
languages — English, Chinese, Japanese, Korean, Spanish, French, German,
Portuguese (BR), Italian, Russian, Indonesian, Thai, Vietnamese — are pinned to
the top; the rest follow in English-name alphabetical order. The exact list and
BCP-47 codes live in [`data/models.json`](data/models.json).

Chinese uses the script-less code **`zh`**, not `zh-Hant` / `zh-Hans`. Output is
speech, which has no script, so the script-specific codes sound identical — and
they break same-language echo (the model doesn't treat spoken Mandarin as an
exact match for a script-tagged target), whereas `zh` translates and echoes
correctly.

There is **no source-language selection** — Gemini auto-detects the spoken
language, so you only pick what to translate *into*.

## Non-goals (v1)

Captions/subtitles, translating one input into several languages at once, and
explicit source-language selection are out of scope.

## License

Licensed under the GNU General Public License v2.0 — see [LICENSE](LICENSE).
This matches OBS Studio's licensing, since the plugin links against libobs.
