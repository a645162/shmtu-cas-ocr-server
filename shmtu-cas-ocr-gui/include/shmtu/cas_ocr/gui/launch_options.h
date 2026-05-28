#pragma once

#include <string>

namespace shmtu::cas_ocr::gui {

struct LaunchOptions {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
};

}  // namespace shmtu::cas_ocr::gui
