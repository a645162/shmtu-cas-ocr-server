#pragma once

#include <string>

namespace shmtu::cas::ocr::gui {

struct LaunchOptions {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = true;
};

}  // namespace shmtu::cas::ocr::gui
