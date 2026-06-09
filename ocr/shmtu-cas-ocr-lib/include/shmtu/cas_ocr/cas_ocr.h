#pragma once

#include "types.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations to avoid exposing OpenCV in the public header
// when consumers only need the API types.
namespace cv { class Mat; }

namespace shmtu::cas::ocr {

[[nodiscard]] std::string_view library_version() noexcept;

// Main OCR engine class for SHMTU CAS CAPTCHA recognition.
//
// Uses PIMPL idiom to hide NCNN/OpenCV implementation details.
// Each instance maintains its own model copies and a mutex for
// thread-safe inference. Multiple instances can run in parallel
// (intended for worker-pool pattern in the server).
//
// Two model versions are supported and selected at load time:
//   * ModelVersion::V1 — original 3-model pipeline
//                         (resnet18 equal-symbol / resnet18 operator /
//                          resnet34 digit) stored under <model_dir>/v1/.
//   * ModelVersion::V2 — single trislot-decoder model (default)
//                         stored under <model_dir>/v2/.
// A backward-compatibility lookup also falls back to <model_dir>/ for the
// V1 filenames when <model_dir>/v1/ is not present.
//
// Usage:
//   shmtu::cas::ocr::CasOcr ocr("/path/to/models");
//   ocr.load_model("fp16", false);  // defaults to ModelVersion::V2
//   auto result = ocr.predict("/path/to/captcha.png");
class CasOcr {
public:
    // Construct an OCR engine.
    // model_dir: directory containing versioned subfolders (v1/, v2/).
    explicit CasOcr(std::string model_dir = "");

    ~CasOcr();

    // Non-copyable, movable.
    CasOcr(const CasOcr&) = delete;
    CasOcr& operator=(const CasOcr&) = delete;
    CasOcr(CasOcr&&) noexcept;
    CasOcr& operator=(CasOcr&&) noexcept;

    // Load models from the model_dir specified at construction.
    // precision:    "fp16" or "fp32". Defaults to "fp16".
    // use_gpu:      attempt to use GPU (Vulkan) acceleration if available.
    // num_threads:  0 means auto-tune based on the current runtime mode.
    // version:      ModelVersion::V2 by default. Selects v1/ or v2/ subdir.
    // Returns true if all required models loaded successfully.
    bool load_model(std::string_view precision = "fp16",
                    bool use_gpu = false,
                    int num_threads = 0,
                    ModelVersion version = ModelVersion::V2);

    // Backwards-compatible overload: defaults to V2 (new default).
    // Retained so existing callers continue to compile and link.
    bool load_model_v1(std::string_view precision = "fp16",
                       bool use_gpu = false,
                       int num_threads = 0) {
        return load_model(precision, use_gpu, num_threads, ModelVersion::V1);
    }

    // Release loaded models and free GPU resources.
    void release();

    // Query model state.
    bool is_loaded() const;
    ModelStatus model_status() const;
    ModelVersion model_version() const;

    // Predict CAPTCHA from an OpenCV Mat (BGR, 3-channel).
    // Thread-safe: internally locked per instance.
    PredictResult predict(const cv::Mat& image);

    // Predict CAPTCHA from an image file on disk.
    PredictResult predict(std::string_view image_path);

    // Predict CAPTCHA from raw image bytes in memory.
    // Supports JPEG, PNG, BMP, etc. (anything OpenCV can decode).
    PredictResult predict(std::span<const uint8_t> image_data);

    // --- Static GPU / Vulkan helpers ---

#ifdef NCNN_SUPPORT_VULKAN
    // Number of available Vulkan GPU devices.
    static int gpu_count();

    // Whether any Vulkan device is available.
    static bool is_vulkan_supported();

    // Default GPU index selected by NCNN.
    static int default_gpu_index();

    // Query info for a specific GPU device.
    static GpuDeviceInfo gpu_info(int gpu_index = -1);

    // Query info for all available GPU devices.
    static std::vector<GpuDeviceInfo> all_gpu_info();
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace shmtu::cas::ocr
