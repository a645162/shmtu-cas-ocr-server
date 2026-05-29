#pragma once

#include "cli_types.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace shmtu::cas::ocr::cli {

RemoteOcrResult call_remote_ocr(const std::string& host,
                                int port,
                                std::span<const uint8_t> image_bytes,
                                int timeout_sec = 30);
RemoteOcrResult call_remote_ocr_file(const std::string& host,
                                     int port,
                                     const std::filesystem::path& path,
                                     int timeout_sec = 30);
std::expected<void, std::string> check_remote_server(const CliConfig& config);

}  // namespace shmtu::cas::ocr::cli
