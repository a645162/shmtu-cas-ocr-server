#pragma once

#include <shmtu/cas_ocr/types.h>

#include <mutex>
#include <string>
#include <string_view>

namespace cv { class Mat; }

namespace shmtu::cas::ocr {

// V2 engine — single trislot-decoder model (MobileNetV3-Small backbone).
//
// Pipeline:
//   - BGR -> grayscale -> resize to 64x192 (W=192, H=64)
//   - Normalize to [0,1] via mean=0, scale=1/255
//   - One forward pass produces three heads:
//       digit_left_logits   (10 classes)
//       operator_logits     (3 classes: 0=Add, 1=Sub, 2=Mul)
//       digit_right_logits  (10 classes)
//   - No equal-symbol detection is needed; that field is reported as -1.
//
// NOTE: the exact ncnn blob names emitted by pnnx depend on the export
// graph.  When this code is first wired up against a real v2 ncnn model,
// inspect `net.output_names()` / `net.blobs()` and confirm that the names
// below match.  The code uses the conventional `digit_left`, `operator`,
// `digit_right` names; if pnnx emits numeric ids (e.g. "421"), update the
// `extract` calls accordingly.  The fallback below tries both forms.
class V2Engine {
public:
    V2Engine();
    ~V2Engine();

    V2Engine(const V2Engine&) = delete;
    V2Engine& operator=(const V2Engine&) = delete;

    // Load the single V2 model.  Returns true on success.
    bool load(const std::string& model_dir,
              std::string_view precision,
              bool use_gpu,
              int num_threads);

    void release();

    bool is_loaded() const { return is_init_; }
    ModelStatus status() const { return status_; }

    PredictResult predict(const cv::Mat& bgr_image);

private:
    bool is_init_ = false;
    ModelStatus status_ = ModelStatus::NotLoaded;

    struct Impl;
    Impl* impl_ = nullptr;  // owned via unique_ptr in cpp
};

} // namespace shmtu::cas::ocr
