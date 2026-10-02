#include "model-store.hpp"
#include <obs-module.h>
#include <util/platform.h>

namespace lt {

namespace {

constexpr const char *kFile = "models.json";

ModelCatalog g_catalog;

// Takes ownership of a bmalloc'ed path (may be null).
void merge_file(char *path, const char *what)
{
    if (!path) return;
    char *text = os_quick_read_utf8_file(path);
    if (text) {
        ModelCatalog extra = parse_model_catalog(text);
        bfree(text);
        blog(LOG_INFO, "[live-translate] %s %s: %zu usable model(s)", what, path,
             extra.size());
        merge_model_catalog(g_catalog, extra);
    }
    bfree(path);
}

}

void model_catalog_load()
{
    g_catalog = parse_model_catalog(builtin_models_json());
    merge_file(obs_module_file(kFile), "plugin data");
    merge_file(obs_module_config_path(kFile), "user config");
    blog(LOG_INFO, "[live-translate] model catalog: %zu model(s)",
         g_catalog.size());
}

const ModelCatalog &model_catalog()
{
    return g_catalog;
}

}
