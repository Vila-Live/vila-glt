#include "api-key.hpp"
#include "audio-convert.hpp"
#include "audio-pacing.hpp"
#include "languages.hpp"
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

const char *source_get_name(void *)
{
    return obs_module_text("Gemini Translated Audio");
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
    // The key field is write-only: a typed key moves to the shared, sealed key
    // store and is erased from the source settings right away, so it never
    // reaches the scene collection file or OBS WebSocket readers. An empty
    // field means "no change", not "forget the key".
    std::string typed_key = obs_data_get_string(settings, "api_key");
    if (!typed_key.empty()) lt::api_key_set(typed_key);
    obs_data_erase(settings, "api_key");

    // api_key stays empty here; sync_session fills it from the key store.
    lt::SessionConfig next;
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
    want.api_key = lt::api_key_get();
    if (!lt::session_should_run(want, d->program_active)) {
        if (d->session_started) {
            d->session.stop();
            d->session_started = false;
        }
        return;
    }
    if (!d->session_started || lt::session_needs_reconnect(d->applied, want)) {
        d->session.configure(want.api_key, want.target_lang, want.echo_target);
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

void *source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *d = new SourceData();
    d->context = source;
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

bool add_input_cb(void *param, obs_source_t *source)
{
    auto *list = static_cast<obs_property_t *>(param);
    if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) &&
        !is_translated_source(source))
        obs_property_list_add_string(list, obs_source_get_name(source),
                                     obs_source_get_uuid(source));
    return true;
}

std::string status_text(SourceData *d)
{
    if (!d) return obs_module_text("Idle");
    {
        std::lock_guard<std::mutex> lk(d->input_mtx);
        if (d->cfg.input_uuid.empty())
            return obs_module_text("Select an input audio source.");
        if (lt::api_key_get().empty())
            return obs_module_text("Enter a Gemini API key.");
        if (!d->input_weak)
            return obs_module_text("Input audio source not found.");
    }
    if (!d->program_active)
        return obs_module_text("Paused: this source is hidden or not in the "
                               "active scene.");
    return d->session.status_text();
}

bool forget_key_clicked(obs_properties_t *, obs_property_t *, void *)
{
    lt::api_key_set("");
    return true; // rebuild the properties so the key state text updates
}

obs_properties_t *source_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *inputs = obs_properties_add_list(
        props, "input_source", obs_module_text("Input Audio Source"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(inputs, obs_module_text("(none)"), "");
    obs_enum_sources(add_input_cb, inputs);

    obs_property_t *list = obs_properties_add_list(
        props, "target_lang", obs_module_text("Target Language"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (int i = 0; i < lt::kLanguagesCount; ++i)
        obs_property_list_add_string(list, lt::kLanguages[i].name,
                                     lt::kLanguages[i].code);

    obs_properties_add_bool(
        props, "echo_target",
        obs_module_text("Output speech even when it is already in the target "
                        "language (otherwise stays silent)"));
    obs_properties_add_float_slider(
        props, "playback_delay",
        obs_module_text("Playback Delay (seconds)"),
        kMinPlaybackDelaySeconds, kMaxPlaybackDelaySeconds, 0.1);

    obs_properties_add_text(
        props, "api_key",
        obs_module_text("Gemini API Key (shared by all sources; paste to set)"),
        OBS_TEXT_PASSWORD);
    const char *key_state =
        lt::api_key_get().empty()
            ? obs_module_text("API key: not set.")
            : obs_module_text("API key: saved. It is kept outside the scene "
                              "collection and is not shown here again.");
    obs_properties_add_text(props, "key_state", key_state, OBS_TEXT_INFO);
    obs_properties_add_button(props, "forget_key",
                              obs_module_text("Forget saved API key"),
                              forget_key_clicked);
    std::string status = status_text(static_cast<SourceData *>(data));
    obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
    return props;
}

void source_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "input_source", "");
    obs_data_set_default_string(settings, "target_lang", "en");
    obs_data_set_default_bool(settings, "echo_target", true);
    obs_data_set_default_double(settings, "playback_delay", 0.0);
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
    info.activate = source_activate;
    info.deactivate = source_deactivate;
    info.get_properties = source_properties;
    info.get_defaults = source_defaults;
    return info;
}();
