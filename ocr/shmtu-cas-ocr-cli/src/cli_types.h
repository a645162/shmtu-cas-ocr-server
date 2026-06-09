#pragma once

#include <shmtu/cas_ocr/cas_ocr.h>

#include <cstdint>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::cli {

using ByteBuffer = std::vector<uint8_t>;

struct RemoteOcrResult {
    bool success = false;
    std::string expression;
    int result = 0;
    int equal_symbol = 0;
    int op = 0;
    int digit1 = 0;
    int digit2 = 0;
    std::string error;
    int http_status = 0;
    bool request_ok = false;
    int model_version = 0;
};

struct CliConfig {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
    shmtu::cas::ocr::ModelVersion model_version = shmtu::cas::ocr::ModelVersion::V2;
    bool json_output = false;
    std::string input_path;
    std::string server_host;
    int server_port = 21600;
    bool server_mode = false;
    bool compare_mode = false;
};

struct CompareEntry {
    std::string file_path;
    shmtu::cas::ocr::PredictResult local_result;
    RemoteOcrResult remote_result;
    bool local_ok = false;
    bool remote_ok = false;
};

}  // namespace shmtu::cas::ocr::cli
