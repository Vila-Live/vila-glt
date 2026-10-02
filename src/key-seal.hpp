#pragma once
#include <string>

namespace lt {

// Turns an API key into the text stored on disk. On Windows the key is
// encrypted with DPAPI, so the file only unseals for the same Windows user on
// the same machine; elsewhere there is no OS protection and it is only encoded.
// An empty key seals to an empty string.
std::string seal_api_key(const std::string &key);

// Inverse of seal_api_key. Returns an empty key if the text is malformed, was
// tampered with, or was sealed by another user/machine.
std::string unseal_api_key(const std::string &sealed);

// True iff seal_api_key actually encrypts on this platform.
bool api_key_sealing_is_encrypted();

}
