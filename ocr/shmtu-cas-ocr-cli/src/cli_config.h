#pragma once

#include "cli_types.h"

#include <expected>
#include <string>

namespace shmtu::cas::ocr::cli {

void print_banner();
std::expected<CliConfig, std::string> parse_args(int argc, char* argv[]);

}  // namespace shmtu::cas::ocr::cli
