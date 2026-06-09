#pragma once

#include <shmtu/cas_ocr/types.h>

#include <mutex>
#include <string>
#include <string_view>

namespace cv { class Mat; }

namespace shmtu::cas::ocr {

// V1 engine — original 3-model pipeline:
//   - resnet18 equal-symbol detector (CHS vs Symbol)
//   - resnet18 operator classifier (6 classes: Add/AddCHS/Sub/SubCHS/Mul/MulCHS)
//   - resnet34 digit classifier (10 classes)
//
// Uses BGR 224x224 input with ImageNet mean/std normalization.
class V1Engine {
public:
    V1Engine();
    ~V1Engine();

    V1Engine(const V1Engine&) = delete;
    V1Engine& operator=(const V1Engine&) = delete;

    // Load the three V1 models.  Returns true on success.
    // On success the engine is ready for predict().
    bool load(const std::string& model_dir,
              std::string_view precision,
              bool use_gpu,
              int num_threads);

    void release();

    bool is_loaded() const { return is_init_; }
    ModelStatus status() const { return status_; }

    // Run prediction.  Caller is responsible for image validity (non-empty BGR).
    PredictResult predict(const cv::Mat& bgr_image);

private:
    bool is_init_ = false;
    ModelStatus status_ = ModelStatus::NotLoaded;

    // Forward-declared implementation keeps ncnn out of the public header.
    struct Impl;
    Impl* impl_ = nullptr;  // owned via unique_ptr in cpp
};

} // namespace shmtu::cas::ocr
