#pragma once
#include "model-catalog.hpp"

namespace lt {

// Builds the model catalog; call once at module load. Starts from the
// data/models.json embedded in the plugin, then merges a models.json from the
// plugin's data folder and one from its config folder
// (<OBS config>/plugin_config/<module>/models.json) if they exist.
void model_catalog_load();

// The loaded catalog. Never changes after model_catalog_load, so it can be read
// from any thread.
const ModelCatalog &model_catalog();

}
