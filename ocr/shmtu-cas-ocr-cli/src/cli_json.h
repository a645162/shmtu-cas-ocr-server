#pragma once

#include "cli_types.h"

#include <string>
#include <string_view>

namespace shmtu::cas::ocr::cli {

std::string json_escape(std::string_view input);
std::string predict_result_to_json(const shmtu::cas::ocr::PredictResult& result);
std::string remote_result_to_json(const RemoteOcrResult& result);

}  // namespace shmtu::cas::ocr::cli
