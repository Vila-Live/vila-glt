#pragma once
#include <string>
#include <vector>

namespace lt {

struct Language {
    std::string code; // sent to the model as the target language
    std::string name; // shown in the dropdown
};

// One live-translate model the user can pick. The table comes from
// data/models.json (embedded into the plugin at build time) and can be extended
// with a models.json in the plugin's config folder.
struct ModelInfo {
    std::string id;          // model name sent to the provider
    std::string name;        // shown in the dropdown
    std::string provider;    // which protocol to speak; see provider_supported
    std::string api_key_env; // variable in <OBS config>/.env holding the key
    bool supports_echo = false;
    std::vector<Language> languages;
};

using ModelCatalog = std::vector<ModelInfo>;

// True iff the plugin has a protocol implementation for this provider. A new
// JSON row only works by itself for a supported provider.
bool provider_supported(const std::string &provider);

// Parses catalog JSON. Models that are incomplete or use an unsupported
// provider are skipped; malformed text gives an empty catalog.
ModelCatalog parse_model_catalog(const std::string &json_text);

// Adds `extra` on top of `base`: a model with an existing id replaces it in
// place, new ids are appended.
void merge_model_catalog(ModelCatalog &base, const ModelCatalog &extra);

// The model with this id, else the first model, else nullptr for an empty
// catalog.
const ModelInfo *find_model(const ModelCatalog &catalog, const std::string &id);

bool model_has_language(const ModelInfo &model, const std::string &code);

// The data/models.json that was embedded at build time.
std::string builtin_models_json();

}
