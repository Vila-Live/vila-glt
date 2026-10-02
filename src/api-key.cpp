#include "api-key.hpp"
#include "key-seal.hpp"
#include <atomic>
#include <mutex>
#include <obs-module.h>
#include <util/platform.h>

namespace lt {

namespace {

constexpr const char *kKeyFile = "api-key.json";

std::mutex g_mtx;
std::string g_key;
std::atomic<uint32_t> g_version{1};

std::string config_path(const char *file)
{
    char *path = obs_module_config_path(file);
    if (!path) return "";
    std::string out = path;
    bfree(path);
    return out;
}

void persist(const std::string &key)
{
    std::string dir = config_path("");
    std::string path = config_path(kKeyFile);
    if (dir.empty() || path.empty()) return;

    if (key.empty()) {
        os_unlink(path.c_str());
        return;
    }
    std::string sealed = seal_api_key(key);
    if (sealed.empty()) {
        blog(LOG_ERROR, "[live-translate] could not seal the API key; it is "
                        "kept for this session only");
        return;
    }
    os_mkdirs(dir.c_str());
    if (!os_quick_write_utf8_file(path.c_str(), sealed.c_str(), sealed.size(),
                                  false))
        blog(LOG_ERROR, "[live-translate] could not write the API key file");
}

}

void api_key_load()
{
    std::string path = config_path(kKeyFile);
    if (path.empty()) return;
    char *text = os_quick_read_utf8_file(path.c_str());
    if (!text) return;
    std::string key = unseal_api_key(text);
    bfree(text);
    if (key.empty()) {
        blog(LOG_WARNING, "[live-translate] stored API key could not be read "
                          "(different Windows user or machine?); enter it again");
        return;
    }
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
