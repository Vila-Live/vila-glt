#include "api-key.hpp"
#include "model-store.hpp"
#include <obs-module.h>

extern struct obs_source_info live_translate_source_info;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vila-glt", "en-US")

extern "C" const char *obs_module_name(void) { return "Vila GLT — Live Translate"; }
extern "C" const char *obs_module_description(void)
{
    return "Real-time speech-to-speech translation via the Gemini Live API.";
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-vila-glt] module loaded");
    lt::api_key_load();
    lt::model_catalog_load();
    obs_register_source(&live_translate_source_info);
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-vila-glt] module unloaded");
}
