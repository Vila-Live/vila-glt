#include <catch2/catch_test_macros.hpp>
#include "model-catalog.hpp"

using namespace lt;

namespace {

const char *kTwoModels = R"({"models":[
  {"id":"m1","name":"Model One","provider":"gemini",
   "api_key_env":"GEMINI_API_KEY","supports_echo":true,
   "languages":[{"code":"en","name":"English"},{"code":"sr","name":"Serbian"}]},
  {"id":"m2","name":"Model Two","provider":"gemini",
   "api_key_env":"OTHER_KEY","supports_echo":false,
   "languages":[{"code":"de","name":"German"}]}
]})";

}

TEST_CASE("catalog parses models in file order with all their fields")
{
    ModelCatalog c = parse_model_catalog(kTwoModels);
    REQUIRE(c.size() == 2);
    REQUIRE(c[0].id == "m1");
    REQUIRE(c[0].name == "Model One");
    REQUIRE(c[0].provider == "gemini");
    REQUIRE(c[0].api_key_env == "GEMINI_API_KEY");
    REQUIRE(c[0].supports_echo);
    REQUIRE(c[0].languages.size() == 2);
    REQUIRE(c[0].languages[1].code == "sr");
    REQUIRE(c[0].languages[1].name == "Serbian");
    REQUIRE(c[1].id == "m2");
    REQUIRE_FALSE(c[1].supports_echo);
}

TEST_CASE("malformed catalog text gives an empty catalog")
{
    REQUIRE(parse_model_catalog("").empty());
    REQUIRE(parse_model_catalog("not json").empty());
    REQUIRE(parse_model_catalog("{}").empty());
    REQUIRE(parse_model_catalog(R"({"models":"nope"})").empty());
}

TEST_CASE("incomplete models and unknown providers are skipped")
{
    ModelCatalog c = parse_model_catalog(R"({"models":[
      {"name":"no id","provider":"gemini","api_key_env":"K",
       "languages":[{"code":"en","name":"English"}]},
      {"id":"no-langs","name":"x","provider":"gemini","api_key_env":"K",
       "languages":[]},
      {"id":"no-key","name":"x","provider":"gemini",
       "languages":[{"code":"en","name":"English"}]},
      {"id":"other","name":"x","provider":"not-implemented","api_key_env":"K",
       "languages":[{"code":"en","name":"English"}]},
      {"id":"ok","name":"OK","provider":"gemini","api_key_env":"K",
       "languages":[{"code":"en","name":"English"}]}
    ]})");
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].id == "ok");
}

TEST_CASE("only providers the plugin can talk to are supported")
{
    REQUIRE(provider_supported("gemini"));
    REQUIRE_FALSE(provider_supported("not-implemented"));
}

TEST_CASE("merging replaces a model with the same id and appends new ones")
{
    ModelCatalog base = parse_model_catalog(kTwoModels);
    ModelCatalog extra = parse_model_catalog(R"({"models":[
      {"id":"m1","name":"Model One v2","provider":"gemini","api_key_env":"K",
       "languages":[{"code":"ja","name":"Japanese"}]},
      {"id":"m3","name":"Model Three","provider":"gemini","api_key_env":"K",
       "languages":[{"code":"fr","name":"French"}]}
    ]})");
    merge_model_catalog(base, extra);
    REQUIRE(base.size() == 3);
    REQUIRE(base[0].id == "m1");
    REQUIRE(base[0].name == "Model One v2");
    REQUIRE(base[1].id == "m2");
    REQUIRE(base[2].id == "m3");
}

TEST_CASE("find_model falls back to the first model for an unknown id")
{
    ModelCatalog c = parse_model_catalog(kTwoModels);
    REQUIRE(find_model(c, "m2")->id == "m2");
    REQUIRE(find_model(c, "gone")->id == "m1");
    REQUIRE(find_model(c, "")->id == "m1");
    REQUIRE(find_model(ModelCatalog{}, "m1") == nullptr);
}

TEST_CASE("model_has_language checks the model's own language list")
{
    ModelCatalog c = parse_model_catalog(kTwoModels);
    REQUIRE(model_has_language(c[0], "sr"));
    REQUIRE_FALSE(model_has_language(c[0], "de"));
}

TEST_CASE("the built-in catalog offers the Gemini live translate model")
{
    ModelCatalog c = parse_model_catalog(builtin_models_json());
    REQUIRE_FALSE(c.empty());
    REQUIRE(c[0].id == "gemini-3.5-live-translate-preview");
    REQUIRE(c[0].provider == "gemini");
    REQUIRE(c[0].api_key_env == "GEMINI_API_KEY");
    REQUIRE(c[0].supports_echo);
    REQUIRE(model_has_language(c[0], "sr"));
    REQUIRE(model_has_language(c[0], "en"));
}
