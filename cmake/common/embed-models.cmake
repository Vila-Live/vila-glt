# Embeds data/models.json into the plugin as a byte array, so the model/language
# table is a plain JSON file in the repo yet the plugin still works as a single
# DLL. Sets MODELS_JSON_CPP to the generated source file.

set(_models_json "${CMAKE_SOURCE_DIR}/data/models.json")
set(MODELS_JSON_CPP "${CMAKE_BINARY_DIR}/generated/models-json.cpp")

# Re-run CMake when the JSON changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_models_json}")

file(READ "${_models_json}" _models_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _models_bytes "${_models_hex}")
# Keep the generated lines short.
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],0x[0-9a-f][0-9a-f],)"
       "\\1\n    " _models_bytes "${_models_bytes}")

file(WRITE "${MODELS_JSON_CPP}.tmp"
"// Generated from data/models.json by cmake/common/embed-models.cmake.
// Do not edit; change the JSON and rebuild.
#include \"model-catalog.hpp\"

namespace lt {

namespace {
const unsigned char kModelsJson[] = {
    ${_models_bytes}
};
}

std::string builtin_models_json()
{
    return std::string(reinterpret_cast<const char *>(kModelsJson),
                       sizeof(kModelsJson));
}

}
")
# Only touch the real file when the content changed, to avoid needless rebuilds.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MODELS_JSON_CPP}.tmp" "${MODELS_JSON_CPP}")
