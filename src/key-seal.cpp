#include "key-seal.hpp"
#include "base64.hpp"
#include <nlohmann/json.hpp>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

using nlohmann::json;

namespace lt {

namespace {

constexpr const char *kDpapi = "dpapi";
constexpr const char *kNone = "none";

#ifdef _WIN32
bool dpapi_protect(const std::string &plain, std::vector<uint8_t> &out)
{
    DATA_BLOB in = {};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB sealed = {};
    if (!CryptProtectData(&in, L"obs-live-translate API key", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &sealed))
        return false;
    out.assign(sealed.pbData, sealed.pbData + sealed.cbData);
    LocalFree(sealed.pbData);
    return true;
}

bool dpapi_unprotect(const std::vector<uint8_t> &blob, std::string &out)
{
    DATA_BLOB in = {};
    in.pbData = const_cast<BYTE *>(blob.data());
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB plain = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain))
        return false;
    out.assign(reinterpret_cast<char *>(plain.pbData), plain.cbData);
    SecureZeroMemory(plain.pbData, plain.cbData);
    LocalFree(plain.pbData);
    return true;
}
#endif

}

bool api_key_sealing_is_encrypted()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::string seal_api_key(const std::string &key)
{
    if (key.empty()) return "";

    json j;
#ifdef _WIN32
    std::vector<uint8_t> blob;
    if (!dpapi_protect(key, blob)) return "";
    j["protection"] = kDpapi;
    j["data"] = base64_encode(blob.data(), blob.size());
#else
    j["protection"] = kNone;
    j["data"] = base64_encode(reinterpret_cast<const uint8_t *>(key.data()),
                              key.size());
#endif
    return j.dump();
}

std::string unseal_api_key(const std::string &sealed)
{
    json j = json::parse(sealed, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return "";
    if (!j.contains("protection") || !j["protection"].is_string() ||
        !j.contains("data") || !j["data"].is_string())
        return "";

    std::string protection = j["protection"].get<std::string>();
    std::vector<uint8_t> blob = base64_decode(j["data"].get<std::string>());
    if (blob.empty()) return "";

    if (protection == kNone)
        return std::string(blob.begin(), blob.end());
#ifdef _WIN32
    if (protection == kDpapi) {
        std::string key;
        if (dpapi_unprotect(blob, key)) return key;
    }
#endif
    return "";
}

}
