#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shmtu::cas::ocr {

std::string base64_encode(std::span<const uint8_t> data);
std::expected<std::vector<uint8_t>, std::string> base64_decode(std::string_view input);

}  // namespace shmtu::cas::ocr
