#include "api-key.hpp"
#include "dotenv.hpp"
#include <atomic>
#include <mutex>
#include <obs-module.h>
#include <util/platform.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace lt {

namespace {

constexpr const char *kEnvName = "GEMINI_API_KEY";

std::mutex g_mtx;
std::string g_key;
std::atomic<uint32_t> g_version{1};

// <OBS config folder>/.env. Derived from this module's config path
// (<OBS config>/plugin_config/<module>/) so it also follows a portable-mode
// OBS install.
std::string env_file_path()
{
    char *module_dir = obs_module_config_path("");
    if (!module_dir) return "";
    std::string path = module_dir;
    bfree(module_dir);

    for (char &c : path)
        if (c == '\\') c = '/';
    while (!path.empty() && path.back() == '/') path.pop_back();
    // Drop "<module>" and "plugin_config".
    for (int i = 0; i < 2; ++i) {
        size_t slash = path.rfind('/');
        if (slash == std::string::npos) return "";
        path.erase(slash);
    }
    return path + "/.env";
}

std::string read_env_file(const std::string &path)
{
    char *text = os_quick_read_utf8_file(path.c_str());
    if (!text) return "";
    std::string out = text;
    bfree(text);
    return out;
}

void persist(const std::string &key)
{
    std::string path = env_file_path();
    if (path.empty()) return;

    std::string text = dotenv_set(read_env_file(path), kEnvName, key);
    if (text.empty()) {
        os_unlink(path.c_str());
        return;
    }
    if (!os_quick_write_utf8_file(path.c_str(), text.c_str(), text.size(),
                                  false)) {
        blog(LOG_ERROR, "[live-translate] could not write %s; the API key is "
                        "kept for this session only", path.c_str());
        return;
    }
#ifndef _WIN32
    // Readable by the owner only. On Windows the OBS config folder is already
    // private to the user account.
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
}

}

void api_key_load()
{
    std::string path = env_file_path();
    if (path.empty()) return;
    std::string key = dotenv_get(read_env_file(path), kEnvName);
    blog(LOG_INFO, "[live-translate] API key %s in %s",
         key.empty() ? "not found" : "loaded from", path.c_str());
    if (key.empty()) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_key = key;
    g_version.fetch_add(1, std::memory_order_relaxed);
}

std::string api_key_get()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_key;
}

void api_key_set(const std::string &key)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (key == g_key) return;
    g_key = key;
    persist(key);
    g_version.fetch_add(1, std::memory_order_relaxed);
}

uint32_t api_key_version()
{
    return g_version.load(std::memory_order_relaxed);
}

}
