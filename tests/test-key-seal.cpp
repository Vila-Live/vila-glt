#include <catch2/catch_test_macros.hpp>
#include "key-seal.hpp"

#include <string>

namespace {
const std::string kKey = "AIzaSy-not-a-real-key-0123456789";
}

TEST_CASE("a sealed API key unseals to the original")
{
    std::string sealed = lt::seal_api_key(kKey);
    REQUIRE_FALSE(sealed.empty());
    REQUIRE(lt::unseal_api_key(sealed) == kKey);
}

TEST_CASE("the sealed text does not contain the key where the OS can encrypt")
{
    if (!lt::api_key_sealing_is_encrypted()) SKIP("no OS secret protection");
    std::string sealed = lt::seal_api_key(kKey);
    REQUIRE(sealed.find(kKey) == std::string::npos);
}

TEST_CASE("an empty key seals to nothing")
{
    REQUIRE(lt::seal_api_key("").empty());
    REQUIRE(lt::unseal_api_key("").empty());
}

TEST_CASE("malformed or tampered sealed text unseals to an empty key")
{
    REQUIRE(lt::unseal_api_key("not json").empty());
    REQUIRE(lt::unseal_api_key("{}").empty());
    REQUIRE(lt::unseal_api_key(R"({"protection":"dpapi","data":"AAAA"})").empty());
    REQUIRE(lt::unseal_api_key(R"({"protection":"unknown","data":"AAAA"})").empty());
}
