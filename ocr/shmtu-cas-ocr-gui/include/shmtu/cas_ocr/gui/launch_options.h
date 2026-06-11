#pragma once

#include <shmtu/cas_ocr/types.h>

#include <string>

namespace shmtu::cas::ocr::gui {

struct LaunchOptions {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = true;
    shmtu::cas::ocr::ModelVersion model_version = shmtu::cas::ocr::ModelVersion::V2;
    std::string v2_tag;       // e.g. "v2.0.5", empty = auto resolve latest
    std::string v2_backbone;  // e.g. "mobilenet_v3_small", empty = default
};

}  // namespace shmtu::cas::ocr::gui
