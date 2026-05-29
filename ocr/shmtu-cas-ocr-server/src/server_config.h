#pragma once

#include <shmtu/cas_ocr/server.h>

#include <expected>
#include <string>
#include <vector>

namespace shmtu::cas::ocr {

std::expected<ServerConfig, std::string> parse_server_config(
    int argc,
    char* argv[],
    std::vector<std::string>& override_messages);
void print_runtime_configuration(const ServerConfig& config);
void inspect_gpu_runtime(ServerConfig& config);

}  // namespace shmtu::cas::ocr
