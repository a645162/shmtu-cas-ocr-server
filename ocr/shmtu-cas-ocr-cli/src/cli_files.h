#pragma once

#include "cli_types.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::cli {

namespace fs = std::filesystem;

std::expected<ByteBuffer, std::string> read_binary_file(const fs::path& path);
bool is_image_file(const fs::path& path);
std::expected<std::vector<std::string>, std::string> collect_image_paths(const fs::path& input);

}  // namespace shmtu::cas::ocr::cli
