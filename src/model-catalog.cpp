#include "model-catalog.hpp"
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace lt {

namespace {

std::string string_field(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_string()) return "";
    return j[key].get<std::string>();
}

bool parse_model(const json &j, ModelInfo &m)
{
    if (!j.is_object()) return false;
    m.id = string_field(j, "id");
    m.name = string_field(j, "name");
    m.provider = string_field(j, "provider");
    m.api_key_env = string_field(j, "api_key_env");
    m.supports_echo = j.value("supports_echo", false);
    if (m.name.empty()) m.name = m.id;

    if (j.contains("languages") && j["languages"].is_array()) {
        for (const auto &l : j["languages"]) {
            if (!l.is_object()) continue;
            Language lang{string_field(l, "code"), string_field(l, "name")};
            if (lang.code.empty()) continue;
            if (lang.name.empty()) lang.name = lang.code;
            m.languages.push_back(lang);
        }
    }
    return !m.id.empty() && !m.api_key_env.empty() && !m.languages.empty() &&
           provider_supported(m.provider);
}

}

bool provider_supported(const std::string &provider)
{
    // Each provider here needs a protocol implementation (see
    // translation-session / live-protocol).
    return provider == "gemini";
}

ModelCatalog parse_model_catalog(const std::string &json_text)
{
    ModelCatalog catalog;
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("models") ||
        !j["models"].is_array())
        return catalog;

    for (const auto &entry : j["models"]) {
        ModelInfo m;
        if (parse_model(entry, m)) catalog.push_back(m);
    }
    return catalog;
}

void merge_model_catalog(ModelCatalog &base, const ModelCatalog &extra)
{
    for (const auto &m : extra) {
        bool replaced = false;
        for (auto &existing : base) {
            if (existing.id == m.id) {
                existing = m;
                replaced = true;
                break;
            }
        }
        if (!replaced) base.push_back(m);
    }
}

const ModelInfo *find_model(const ModelCatalog &catalog, const std::string &id)
{
    for (const auto &m : catalog)
        if (m.id == id) return &m;
    return catalog.empty() ? nullptr : &catalog.front();
}

bool model_has_language(const ModelInfo &model, const std::string &code)
{
    for (const auto &l : model.languages)
        if (l.code == code) return true;
    return false;
}

}
