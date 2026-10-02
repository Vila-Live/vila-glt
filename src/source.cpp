#include "api-key.hpp"
#include "audio-convert.hpp"
#include "audio-pacing.hpp"
#include "dotenv.hpp"
#include "model-store.hpp"
#include "output-delay.hpp"
#include "session-config.hpp"
#include "translation-session.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <media-io/audio-resampler.h>
#include <mutex>
#include <obs-module.h>
#include <string>
#include <thread>
#include <util/platform.h>
#include <vector>

namespace {

constexpr const char *kSourceId = "gemini_translated_audio_source";
constexpr double kMinPlaybackDelaySeconds = 0.0;
constexpr double kMaxPlaybackDelaySeconds = 30.0;
constexpr uint32_t kOutputSampleRate = 24000;
constexpr size_t kDrainCapBytes = 9600;        // 200 ms of 24 kHz mono S16
constexpr uint32_t kWaitTimeoutMs = 100;
constexpr uint16_t kOutputBytesPerFrame = sizeof(int16_t);
constexpr uint64_t kInputSyncIntervalNs = 1000000000ULL; // re-check input 1x/s
// Scheduling lead cap. Diagnostics showed the model delivers audio every ~250 ms
// and pauses up to ~600 ms at phrase boundaries (never longer). Buffering 600 ms
// ahead lets OBS play through those pauses so live playback stays continuous
// instead of stuttering at every boundary. For one-way live streaming the only
// cost is a little more end-to-end latency, which is irrelevant. (The tail-on-stop
// truncation this enlarges only affects recordings, not live streams.) Bounded,
// so the lead can't grow unboundedly the way the original (uncapped) lead did.
constexpr uint64_t kMaxLeadNs = 600000000ULL;  // 600 ms scheduling lead cap

struct SourceData {
    obs_source_t *context = nullptr;
    // Each source owns its whole translation stream, so several of them can
    // translate different inputs side by side.
    lt::TranslationSession session;

    // Input side: audio captured from the selected source.
    audio_resampler_t *resampler = nullptr;
    lt::Chunker chunker{3200};
    std::mutex input_mtx; // guards cfg and input_weak
    lt::SessionConfig cfg;
    obs_weak_source_t *input_weak = nullptr; // attached input, if any

    // Session control. OBS reports settings changes on the UI thread and
    // activation on the graphics thread; both only record what is wanted and
    // bump state_version. The push loop is the one thread that starts/stops the
    // session, so a blocking stop never stalls rendering or the UI.
    std::atomic<bool> program_active{false};
    std::atomic<uint32_t> state_version{1};
    uint32_t seen_version = 0;      // push loop only
    uint32_t seen_key_version = 0;  // push loop only
    bool session_started = false;   // push loop only
    lt::SessionConfig applied;      // push loop only

    // Output side: translated audio pushed into the OBS mixer.
    std::thread thread;
    std::atomic<bool> active{false};
    std::atomic<uint32_t> playback_delay_ms{0};
    lt::OutputTimestamper timestamper{kOutputSampleRate, kMaxLeadNs};
    lt::OutputDelayBuffer delay{0};
    uint32_t delay_ms = 0;
};

uint32_t delay_seconds_to_ms(double seconds)
{
    double clamped = std::clamp(seconds, kMinPlaybackDelaySeconds,
                               kMaxPlaybackDelaySeconds);
    return static_cast<uint32_t>(std::lround(clamped * 1000.0));
}

bool is_translated_source(obs_source_t *source)
{
    return std::strcmp(obs_source_get_unversioned_id(source), kSourceId) == 0;
}

// Localized text for `key`, or the English `fallback` when the locale files are
// not installed next to the plugin (a DLL-only install still reads properly).
const char *T(const char *key, const char *fallback)
{
    const char *text = nullptr;
    if (obs_module_get_string(key, &text) && text && *text) return text;
    return fallback;
}

const char *source_get_name(void *)
{
    return T("SourceName", "Live Translate");
}

void create_resampler(SourceData *d)
{
    const struct audio_output_info *info =
        audio_output_get_info(obs_get_audio());
    struct resample_info src = {};
    src.samples_per_sec = info->samples_per_sec;
    src.format = AUDIO_FORMAT_FLOAT_PLANAR;
    src.speakers = info->speakers;
    struct resample_info dst = {};
    dst.samples_per_sec = 16000;
    dst.format = AUDIO_FORMAT_16BIT;
    dst.speakers = SPEAKERS_MONO;
    d->resampler = audio_resampler_create(&dst, &src);
}

// Runs on the input source's audio thread, after that source's filters and
// before its fader — so lowering the input's volume in the mixer does not
// change what Gemini hears.
void input_audio_cb(void *param, obs_source_t *, const struct audio_data *audio,
                    bool muted)
{
    auto *d = static_cast<SourceData *>(param);
    if (!d->resampler || !d->session.is_running()) return;

    uint8_t *out[MAX_AV_PLANES] = {};
    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool ok = audio_resampler_resample(
        d->resampler, out, &out_frames, &ts_offset,
        (const uint8_t *const *)audio->data, audio->frames);
    if (!ok || out_frames == 0) return;

    size_t bytes = static_cast<size_t>(out_frames) * 2;
    // A muted input must not be translated, but the stream has to stay
    // continuous for the model's turn detection — send silence instead of
    // nothing.
    if (muted) std::memset(out[0], 0, bytes);

    // Send the audio continuously, including silence, the way the official
    // Live API client streams the mic. Gating out silence made our input
    // stream discontinuous, which disrupts the model's real-time VAD/turn
    // handling and delays translation output until the next speech resumes.
    auto chunks = d->chunker.push(out[0], bytes);
    for (auto &c : chunks)
        d->session.push_input_pcm(c.data(), c.size());
}

// Caller holds input_mtx.
void detach_input(SourceData *d)
{
    if (!d->input_weak) return;
    if (obs_source_t *src = obs_weak_source_get_source(d->input_weak)) {
        obs_source_remove_audio_capture_callback(src, input_audio_cb, d);
        obs_source_release(src);
    }
    obs_weak_source_release(d->input_weak);
    d->input_weak = nullptr;
    d->chunker.reset();
}

// Keeps the capture callback attached to the configured input. Called on
// settings changes and periodically from the push loop, so an input that is
// created after us (scene-collection load order) or recreated later gets picked
// up without reopening the properties.
void sync_input(SourceData *d)
{
    std::lock_guard<std::mutex> lk(d->input_mtx);
    if (d->input_weak) {
        obs_source_t *cur = obs_weak_source_get_source(d->input_weak);
        bool same = cur && d->cfg.input_uuid == obs_source_get_uuid(cur);
        obs_source_release(cur);
        if (same) return;
        detach_input(d);
    }
    if (d->cfg.input_uuid.empty()) return;

    obs_source_t *src = obs_get_source_by_uuid(d->cfg.input_uuid.c_str());
    if (!src) return;
    // Never listen to a translated-audio source (ourselves included): that
    // would translate a translation.
    if (!is_translated_source(src)) {
        obs_source_add_audio_capture_callback(src, input_audio_cb, d);
        d->input_weak = obs_source_get_weak_source(src);
    }
    obs_source_release(src);
}

void source_update(void *data, obs_data_t *settings)
{
    auto *d = static_cast<SourceData *>(data);
    // api_key stays empty here; sync_session fills it from the key store. The
    // "api_key" setting is only the scratch field of the key editor in the
    // properties (see save_key_clicked) and is never read here.
    lt::SessionConfig next;
    next.model = obs_data_get_string(settings, "model");
    next.key_name = obs_data_get_string(settings, "api_key_name");
    next.enabled = obs_data_get_bool(settings, "enabled");
    next.target_lang = obs_data_get_string(settings, "target_lang");
    next.echo_target = obs_data_get_bool(settings, "echo_target");
    next.input_uuid = obs_data_get_string(settings, "input_source");
    d->playback_delay_ms.store(
        delay_seconds_to_ms(obs_data_get_double(settings, "playback_delay")),
        std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lk(d->input_mtx);
        d->cfg = next;
    }
    sync_input(d);
    d->state_version.fetch_add(1, std::memory_order_relaxed);
}

// The .env variable holding the key this source uses: the selected one if it
// belongs to the model's provider, else the provider's default variable.
std::string key_env_name(const lt::ModelInfo *model, const std::string &selected)
{
    if (lt::env_name_belongs(model->api_key_env, selected)) return selected;
    return model->api_key_env;
}

void source_activate(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    d->program_active = true;
    d->state_version.fetch_add(1, std::memory_order_relaxed);
}

void source_deactivate(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    d->program_active = false;
    d->state_version.fetch_add(1, std::memory_order_relaxed);
}

// Push loop only. Brings the session in line with the wanted state: stopped
// while the source is hidden or unconfigured, (re)connected when it becomes
// runnable or when the key/language/echo changes. A session that stopped itself
// on an auth error stays down until one of those changes, or until the source
// is hidden and shown again.
void sync_session(SourceData *d)
{
    uint32_t version = d->state_version.load(std::memory_order_relaxed);
    uint32_t key_version = lt::api_key_version();
    if (version == d->seen_version && key_version == d->seen_key_version)
        return;
    d->seen_version = version;
    d->seen_key_version = key_version;

    lt::SessionConfig want;
    {
        std::lock_guard<std::mutex> lk(d->input_mtx);
        want = d->cfg;
    }
    // Resolve the model through the catalog: an id that is no longer listed
    // falls back to the first model.
    const lt::ModelInfo *model = lt::find_model(lt::model_catalog(), want.model);
    if (model) {
        want.model = model->id;
        want.api_key = lt::api_key_get(key_env_name(model, want.key_name));
        want.echo_target = want.echo_target && model->supports_echo;
    }
    if (!model || !lt::session_should_run(want, d->program_active)) {
        if (d->session_started) {
            d->session.stop();
            d->session_started = false;
        }
        return;
    }
    if (!d->session_started || lt::session_needs_reconnect(d->applied, want)) {
        d->session.configure(want.api_key, want.model, want.target_lang,
                             want.echo_target);
        d->session_started = true;
    }
    d->applied = want;
}

void push_loop(SourceData *d)
{
    std::vector<uint8_t> buf(kDrainCapBytes);
    uint64_t last_input_sync = 0;
    while (d->active) {
        sync_session(d);

        uint64_t loop_now = os_gettime_ns();
        if (loop_now - last_input_sync >= kInputSyncIntervalNs) {
            last_input_sync = loop_now;
            sync_input(d);
        }

        if (d->session.take_interrupted()) {
            d->timestamper.reset();
            d->delay.reset();
        }

        size_t n = d->session.wait_and_read_output(buf.data(), buf.size(),
                                                   kWaitTimeoutMs);
        if (n < sizeof(int16_t))
            continue;
        uint32_t delay_ms = d->playback_delay_ms.load(std::memory_order_relaxed);
        if (delay_ms != d->delay_ms) {
            d->delay_ms = delay_ms;
            d->delay.set_delay_bytes(lt::output_delay_bytes(
                delay_ms, kOutputSampleRate, kOutputBytesPerFrame));
            d->timestamper.reset();
        }

        std::vector<uint8_t> delayed = d->delay.push(buf.data(), n);
        if (delayed.size() < sizeof(int16_t))
            continue;
        size_t frames = delayed.size() / sizeof(int16_t);

        uint64_t now = os_gettime_ns();
        uint64_t ts = d->timestamper.next_timestamp(now, frames);
        // Pace emission so we never schedule audio further than kMaxLeadNs ahead
        // of the wall clock. Without this the scheduling lead, established during
        // the model's initial burst, never drains (the stream has no gaps) and a
        // recording stop truncates that whole lead off the tail.
        uint64_t delay = d->timestamper.pacing_delay_ns(now);
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::nanoseconds(delay));

        struct obs_source_audio out = {};
        out.data[0] = delayed.data();
        out.frames = static_cast<uint32_t>(frames);
        out.speakers = SPEAKERS_MONO;
        out.format = AUDIO_FORMAT_16BIT;
        out.samples_per_sec = kOutputSampleRate;
        out.timestamp = ts;
        obs_source_output_audio(d->context, &out);
    }
}

const lt::ModelInfo *model_from_settings(obs_data_t *settings)
{
    return lt::find_model(lt::model_catalog(),
                          obs_data_get_string(settings, "model"));
}

// Keys never stay in the source settings. A scene saved by an older version of
// the plugin still carries one: move it to the key store (unless a key is
// already stored) and drop it from the settings.
void migrate_legacy_key(obs_data_t *settings)
{
    std::string legacy = obs_data_get_string(settings, "api_key");
    if (legacy.empty()) return;
    const lt::ModelInfo *model = model_from_settings(settings);
    if (model && lt::api_key_get(model->api_key_env).empty())
        lt::api_key_set(model->api_key_env, legacy);
    obs_data_erase(settings, "api_key");
}

// Called whenever OBS serializes the source: whatever is sitting in the key
// editor's scratch fields must not reach the scene collection file.
void source_save(void *, obs_data_t *settings)
{
    obs_data_erase(settings, "api_key");
    obs_data_erase(settings, "key_label");
}

void *source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *d = new SourceData();
    d->context = source;
    migrate_legacy_key(settings);
    create_resampler(d);
    source_update(d, settings);
    d->active = true;
    d->thread = std::thread(push_loop, d);
    return d;
}

void source_destroy(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    // Stop the push loop first: it re-attaches the input periodically.
    d->active = false;
    if (d->thread.joinable()) d->thread.join();
    {
        std::lock_guard<std::mutex> lk(d->input_mtx);
        detach_input(d);
    }
    d->session.stop();
    if (d->resampler) audio_resampler_destroy(d->resampler);
    delete d;
}

struct InputListCtx {
    obs_property_t *list;
    std::string selected_uuid;
};

// Offer exactly what the OBS Audio Mixer shows: sources whose audio is active
// (a camera whose microphone is disabled is not), minus other Live Translate
// sources. The currently selected input stays listed even if its audio is off
// right now, so reopening the properties does not silently drop it.
bool add_input_cb(void *param, obs_source_t *source)
{
    auto *ctx = static_cast<InputListCtx *>(param);
    if (is_translated_source(source)) return true;
    if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) return true;
    const char *uuid = obs_source_get_uuid(source);
    if (obs_source_audio_active(source) || ctx->selected_uuid == uuid)
        obs_property_list_add_string(ctx->list, obs_source_get_name(source),
                                     uuid);
    return true;
}

struct Status {
    std::string text;
    enum obs_text_info_type level;
};

Status current_status(SourceData *d, const lt::ModelInfo *model)
{
    if (!d) return {T("Status.Idle", "Idle"), OBS_TEXT_INFO_NORMAL};
    if (!model)
        return {T("Status.NoModel", "No live translate model available."),
                OBS_TEXT_INFO_ERROR};
    bool enabled;
    {
        std::lock_guard<std::mutex> lk(d->input_mtx);
        enabled = d->cfg.enabled;
        if (d->cfg.input_uuid.empty())
            return {T("Status.SelectSource", "Select a source to translate."),
                    OBS_TEXT_INFO_WARNING};
        if (lt::api_key_get(key_env_name(model, d->cfg.key_name)).empty())
            return {T("Status.SetKey", "Set the API key for this model."),
                    OBS_TEXT_INFO_WARNING};
        if (!d->input_weak)
            return {T("Status.SourceNotFound", "Source to translate not found."),
                    OBS_TEXT_INFO_WARNING};
    }
    if (!enabled)
        return {T("Status.Off", "Off: the connection is switched off."),
                OBS_TEXT_INFO_WARNING};
    if (!d->program_active)
        return {T("Status.Paused", "Paused: this source is hidden or not in "
                                   "the active scene."),
                OBS_TEXT_INFO_WARNING};

    switch (d->session.status()) {
    case lt::ConnStatus::Connected:
        return {T("Status.Connected", "Connected"), OBS_TEXT_INFO_NORMAL};
    case lt::ConnStatus::Connecting:
        return {T("Status.Connecting", "Connecting..."), OBS_TEXT_INFO_NORMAL};
    case lt::ConnStatus::Reconnecting:
        return {T("Status.Reconnecting", "Reconnecting..."),
                OBS_TEXT_INFO_WARNING};
    case lt::ConnStatus::AuthError:
        // Carries the provider's own error message.
        return {d->session.status_text(), OBS_TEXT_INFO_ERROR};
    case lt::ConnStatus::Idle:
        break;
    }
    return {T("Status.Idle", "Idle"), OBS_TEXT_INFO_NORMAL};
}

void show_status(obs_properties_t *props, SourceData *d,
                 const lt::ModelInfo *model)
{
    obs_property_t *p = obs_properties_get(props, "status");
    if (!p) return;
    Status s = current_status(d, model);
    obs_property_set_description(p, s.text.c_str());
    obs_property_text_set_info_type(p, s.level);
}

// The key dropdown: one entry per stored key of the model's provider, shown as
// "<name> (...last four)". The provider's default key has the value "" so a
// source that never chose a key uses it.
void fill_key_list(obs_properties_t *props, const lt::ModelInfo *model)
{
    obs_property_t *keys = obs_properties_get(props, "api_key_name");
    obs_property_list_clear(keys);
    std::vector<std::string> names;
    if (model) names = lt::api_key_names(model->api_key_env);
    if (names.empty()) {
        obs_property_list_add_string(keys, T("ApiKey.NotSet", "not set"), "");
        return;
    }
    for (const auto &name : names) {
        std::string label = lt::env_label(model->api_key_env, name);
        bool is_default = name == model->api_key_env;
        if (label.empty()) label = T("ApiKey.Default", "default");
        std::string text =
            label + " (" + lt::api_key_hint(lt::api_key_get(name)) + ")";
        obs_property_list_add_string(keys, text.c_str(),
                                     is_default ? "" : name.c_str());
    }
}

// Everything in the properties that depends on the selected model: the language
// list, whether the echo option applies, and which provider's keys are listed.
void apply_model_to_properties(obs_properties_t *props,
                               const lt::ModelInfo *model)
{
    obs_property_t *langs = obs_properties_get(props, "target_lang");
    obs_property_list_clear(langs);
    if (model)
        for (const auto &l : model->languages)
            obs_property_list_add_string(langs, l.name.c_str(), l.code.c_str());

    obs_property_set_visible(obs_properties_get(props, "echo_target"),
                             model && model->supports_echo);
    fill_key_list(props, model);
}

bool model_modified(void *, obs_properties_t *props, obs_property_t *,
                    obs_data_t *settings)
{
    apply_model_to_properties(props, model_from_settings(settings));
    return true;
}

// Shows or hides the key editor (name + key fields, Save/Remove) in place of
// the Edit button.
void show_key_editor(obs_properties_t *props, bool editing)
{
    obs_property_set_visible(obs_properties_get(props, "edit_key"), !editing);
    obs_property_set_visible(obs_properties_get(props, "key_label"), editing);
    obs_property_set_visible(obs_properties_get(props, "api_key"), editing);
    obs_property_set_visible(obs_properties_get(props, "save_key"), editing);
    obs_property_set_visible(obs_properties_get(props, "remove_key"), editing);
}

// Opens the editor on the selected key: its name is prefilled, so Save replaces
// that key, while typing another name adds a new key.
bool edit_key_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
    auto *d = static_cast<SourceData *>(data);
    obs_data_t *settings = obs_source_get_settings(d->context);
    if (const lt::ModelInfo *model = model_from_settings(settings)) {
        std::string selected = key_env_name(
            model, obs_data_get_string(settings, "api_key_name"));
        obs_data_set_string(
            settings, "key_label",
            lt::env_label(model->api_key_env, selected).c_str());
    }
    obs_data_release(settings);
    show_key_editor(props, true);
    return true;
}

// Ends key editing. Save stores the typed key under the typed name and selects
// it; Remove deletes the selected key. The scratch fields are cleared either
// way, so a key never stays in the source settings.
bool finish_key_edit(obs_properties_t *props, void *data, bool remove)
{
    auto *d = static_cast<SourceData *>(data);
    obs_data_t *settings = obs_source_get_settings(d->context);
    const lt::ModelInfo *model = model_from_settings(settings);
    if (model) {
        const std::string &base = model->api_key_env;
        if (remove) {
            lt::api_key_set(
                key_env_name(model,
                             obs_data_get_string(settings, "api_key_name")),
                "");
            obs_data_set_string(settings, "api_key_name", "");
        } else {
            std::string typed = obs_data_get_string(settings, "api_key");
            if (!typed.empty()) {
                std::string env = lt::env_name_with_label(
                    base, obs_data_get_string(settings, "key_label"));
                lt::api_key_set(env, typed);
                obs_data_set_string(settings, "api_key_name",
                                    env == base ? "" : env.c_str());
            }
        }
    }
    obs_data_erase(settings, "api_key");
    obs_data_erase(settings, "key_label");
    obs_data_release(settings);
    // The selected key may have changed without OBS noticing.
    obs_source_update(d->context, nullptr);

    fill_key_list(props, model);
    show_key_editor(props, false);
    show_status(props, d, model);
    return true;
}

bool save_key_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
    return finish_key_edit(props, data, false);
}

bool remove_key_clicked(obs_properties_t *props, obs_property_t *, void *data)
{
    return finish_key_edit(props, data, true);
}

// OBS does not refresh an open properties window by itself, so the status line
// is a snapshot; this button takes a new one.
bool refresh_status_clicked(obs_properties_t *props, obs_property_t *,
                            void *data)
{
    auto *d = static_cast<SourceData *>(data);
    obs_data_t *settings = obs_source_get_settings(d->context);
    const lt::ModelInfo *model = model_from_settings(settings);
    obs_data_release(settings);
    show_status(props, d, model);
    return true;
}

obs_properties_t *source_properties(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    const lt::ModelInfo *model = lt::find_model(lt::model_catalog(), "");
    std::string selected_input;
    if (d) {
        obs_data_t *settings = obs_source_get_settings(d->context);
        model = model_from_settings(settings);
        selected_input = obs_data_get_string(settings, "input_source");
        obs_data_release(settings);
    }

    obs_properties_t *props = obs_properties_create();

    // Engine: which model translates, and with which key. The stored key is
    // never put into a field: Edit opens empty fields for a new key, Save moves
    // it to the key store and closes the editor again.
    obs_properties_t *engine = obs_properties_create();
    obs_property_t *models = obs_properties_add_list(
        engine, "model", T("Model", "Live translate model"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const auto &m : lt::model_catalog())
        obs_property_list_add_string(models, m.name.c_str(), m.id.c_str());
    obs_property_set_modified_callback2(models, model_modified, nullptr);
    obs_properties_add_list(engine, "api_key_name", T("ApiKey", "API key"),
                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    if (d) {
        obs_properties_add_button2(engine, "edit_key", T("ApiKey.Edit", "Edit"),
                                   edit_key_clicked, d);
        obs_properties_add_text(
            engine, "key_label",
            T("ApiKey.Name", "Key name (empty = default)"), OBS_TEXT_DEFAULT);
        obs_properties_add_text(engine, "api_key",
                                T("ApiKey.New", "New API key"),
                                OBS_TEXT_PASSWORD);
        obs_properties_add_button2(engine, "save_key", T("ApiKey.Save", "Save"),
                                   save_key_clicked, d);
        obs_properties_add_button2(engine, "remove_key",
                                   T("ApiKey.Remove", "Remove"),
                                   remove_key_clicked, d);
    }
    obs_properties_add_group(props, "engine", T("Group.Engine", "Engine"),
                             OBS_GROUP_NORMAL, engine);

    // Translate Settings: what is translated, into which language, and how.
    obs_properties_t *translate = obs_properties_create();
    obs_property_t *inputs = obs_properties_add_list(
        translate, "input_source", T("Source", "Source to translate"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(inputs, T("Source.None", "(none)"), "");
    InputListCtx ctx{inputs, selected_input};
    obs_enum_sources(add_input_cb, &ctx);
    obs_properties_add_list(translate, "target_lang",
                            T("TargetLanguage", "Target language"),
                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_properties_add_bool(
        translate, "echo_target",
        T("Echo", "Repeat speech that is already in the target language"));
    obs_properties_add_float_slider(
        translate, "playback_delay",
        T("Delay", "Delay for translated audio (seconds)"),
        kMinPlaybackDelaySeconds, kMaxPlaybackDelaySeconds, 0.1);
    obs_properties_add_group(props, "translate",
                             T("Group.Translate", "Translate Settings"),
                             OBS_GROUP_NORMAL, translate);

    // Connection: the group's own checkbox is the "enabled" setting, the manual
    // on/off switch for the session.
    obs_properties_t *connection = obs_properties_create();
    obs_properties_add_text(connection, "status", "", OBS_TEXT_INFO);
    if (d)
        obs_properties_add_button2(connection, "refresh_status",
                                   T("Status.Refresh", "Refresh status"),
                                   refresh_status_clicked, d);
    obs_properties_add_group(props, "enabled",
                             T("Group.Connection", "Connection"),
                             OBS_GROUP_CHECKABLE, connection);

    apply_model_to_properties(props, model);
    show_key_editor(props, false);
    show_status(props, d, model);
    return props;
}

void source_defaults(obs_data_t *settings)
{
    const lt::ModelInfo *first = lt::find_model(lt::model_catalog(), "");
    obs_data_set_default_string(settings, "model", first ? first->id.c_str() : "");
    obs_data_set_default_string(settings, "api_key_name", "");
    obs_data_set_default_string(settings, "input_source", "");
    obs_data_set_default_string(settings, "target_lang", "en");
    obs_data_set_default_bool(settings, "echo_target", false);
    obs_data_set_default_double(settings, "playback_delay", 0.0);
    obs_data_set_default_bool(settings, "enabled", true);
}

} // namespace

struct obs_source_info live_translate_source_info = [] {
    struct obs_source_info info = {};
    info.id = "gemini_translated_audio_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = source_get_name;
    info.create = source_create;
    info.destroy = source_destroy;
    info.update = source_update;
    info.save = source_save;
    info.activate = source_activate;
    info.deactivate = source_deactivate;
    info.get_properties = source_properties;
    info.get_defaults = source_defaults;
    return info;
}();
