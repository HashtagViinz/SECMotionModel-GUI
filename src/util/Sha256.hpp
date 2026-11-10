#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace secgui {

// Returns the lowercase hexadecimal SHA-256 digest of the given bytes.
std::string sha256Hex(const uint8_t* data, std::size_t size);
std::string sha256Hex(const std::vector<uint8_t>& data);

} // namespace secgui
