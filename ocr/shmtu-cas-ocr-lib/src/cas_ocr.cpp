#include <shmtu/cas_ocr/cas_ocr.h>
#include <shmtu/cas_ocr/version.h>
#include <shmtu/cas_ocr/manifest.h>

#include "v1_engine.h"
#include "v2_engine.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <net.h>

#ifdef NCNN_SUPPORT_VULKAN
#include <gpu.h>
#endif

namespace shmtu::cas::ocr {

namespace {

bool path_check_windows_style(const std::string& dir_path) {
    for (const char c : dir_path) {
        if (c == '\\') {
            return true;
        }
    }
    return false;
}

[[maybe_unused]] void path_ensure_slash(std::string& dir_path) {
    if (!dir_path.empty() && dir_path.back() != '/' && dir_path.back() != '\\') {
        dir_path += path_check_windows_style(dir_path) ? "\\" : "/";
    }
}

// Test if a file exists.  Cheaper than the engine's load attempt + log noise.
bool file_exists(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (fp) {
        std::fclose(fp);
        return true;
    }
    return false;
}

// Resolve the directory that holds a given V1 weight.
//   * Prefer <root>/v1/<filename>.<precision>.<ext>
//   * Fall back to <root>/<filename>.<precision>.<ext> for the legacy layout
//     (v1 files copied directly into <root>/).
// Returns the resolved directory (without trailing slash), or an empty string
// when neither layout contains the file.  Caller should then try the next
// basename in the model triplet.
std::string resolve_v1_dir(const std::string& root, const std::string& basename,
                           const std::string& precision) {
    const std::string v1_dir = root + "/v1/";
    const std::string legacy = root + "/";
    const std::string v1_param = v1_dir + basename + "." + precision + ".param";
    const std::string v1_bin = v1_dir + basename + "." + precision + ".bin";
    if (file_exists(v1_param) && file_exists(v1_bin)) {
        return v1_dir;
    }
    const std::string legacy_param = legacy + basename + "." + precision + ".param";
    const std::string legacy_bin = legacy + basename + "." + precision + ".bin";
    if (file_exists(legacy_param) && file_exists(legacy_bin)) {
        return legacy;
    }
    return {};
}

// Resolve the directory that holds a given V2 weight.
//   * Prefer <root>/v2/<stem>.<precision>.<ext>
//   * Fall back to <root>/<stem>.<precision>.<ext>
// Scans all known v2 asset stems via infer_asset_stem_from_dir.
std::string resolve_v2_dir(const std::string& root, const std::string& precision) {
    // Try <root>/v2/ first (preferred layout)
    auto stem = infer_asset_stem_from_dir(root + "/v2/");
    if (!stem.empty()) {
        const std::string dir = root + "/v2/";
        const std::string param = dir + stem + "." + precision + ".param";
        const std::string bin   = dir + stem + "." + precision + ".bin";
        if (file_exists(param) && file_exists(bin)) {
            return dir;
        }
    }
    // Fallback: flat layout under <root>/
    stem = infer_asset_stem_from_dir(root);
    if (!stem.empty()) {
        const std::string param = root + "/" + stem + "." + precision + ".param";
        const std::string bin   = root + "/" + stem + "." + precision + ".bin";
        if (file_exists(param) && file_exists(bin)) {
            return root + "/";
        }
    }
    return {};
}

}  // namespace

std::string_view library_version() noexcept {
    return SHMTU_CAS_OCR_LIB_VERSION;
}

std::string model_version_to_string(ModelVersion version) {
    switch (version) {
        case ModelVersion::V1:
            return "v1";
        case ModelVersion::V2:
            return "v2";
    }
    return "v2";
}

ModelVersion model_version_from_string(const std::string& value) {
    if (value == "1" || value == "v1" || value == "V1") {
        return ModelVersion::V1;
    }
    // Default: V2 (also matches "2" / "v2" / "V2" / unknown / empty).
    return ModelVersion::V2;
}

struct CasOcr::Impl {
    explicit Impl(std::string dir) : model_dir(std::move(dir)) {}

    std::string model_dir;
    std::string loaded_precision;
    bool loaded_use_gpu = false;
    int loaded_num_threads = 0;
    ModelVersion loaded_version = ModelVersion::V2;
    bool is_init = false;
    ModelStatus status = ModelStatus::NotLoaded;

    std::unique_ptr<V1Engine> v1;
    std::unique_ptr<V2Engine> v2;

    std::mutex inference_mutex;

    bool load_all_models(const std::string& precision,
                         const bool requested_use_gpu,
                         const int requested_num_threads,
                         ModelVersion version) {
        if (model_dir.empty()) {
            return false;
        }

        // If we're already loaded with the same config, no-op.
        if (is_init &&
            loaded_version == version &&
            loaded_precision == precision &&
            loaded_use_gpu == requested_use_gpu &&
            loaded_num_threads == requested_num_threads) {
            return true;
        }

        release();

        std::string resolved_dir;
        switch (version) {
            case ModelVersion::V1: {
                // For V1 we look up the directory by any of the three stems —
                // typical practice is they all live in the same folder.
                resolved_dir = resolve_v1_dir(
                    model_dir, "resnet18_equal_symbol_latest", precision);
                if (resolved_dir.empty()) {
                    resolved_dir = resolve_v1_dir(
                        model_dir, "resnet18_operator_latest", precision);
                }
                if (resolved_dir.empty()) {
                    resolved_dir = resolve_v1_dir(
                        model_dir, "resnet34_digit_latest", precision);
                }
                if (resolved_dir.empty()) {
                    return false;
                }
                // Strip trailing slash so the engine's path_ensure_slash logic
                // is the sole authority on separators.
                while (!resolved_dir.empty() &&
                       (resolved_dir.back() == '/' || resolved_dir.back() == '\\')) {
                    resolved_dir.pop_back();
                }

                v1 = std::make_unique<V1Engine>();
                if (!v1->load(resolved_dir, precision, requested_use_gpu,
                              requested_num_threads)) {
                    v1.reset();
                    return false;
                }
                break;
            }
            case ModelVersion::V2: {
                resolved_dir = resolve_v2_dir(model_dir, precision);
                if (resolved_dir.empty()) {
                    return false;
                }
                while (!resolved_dir.empty() &&
                       (resolved_dir.back() == '/' || resolved_dir.back() == '\\')) {
                    resolved_dir.pop_back();
                }
                v2 = std::make_unique<V2Engine>();
                if (!v2->load(resolved_dir, precision, requested_use_gpu,
                              requested_num_threads)) {
                    v2.reset();
                    return false;
                }
                break;
            }
        }

        loaded_precision = precision;
        loaded_use_gpu = requested_use_gpu;
        loaded_num_threads = requested_num_threads;
        loaded_version = version;
        is_init = true;

        // status reflects whichever engine is active.
        status = (version == ModelVersion::V1)
            ? v1->status()
            : v2->status();
        return true;
    }

    PredictResult predict_dispatch(const cv::Mat& image) {
        if (!is_init) {
            PredictResult result;
            result.success = false;
            result.error = "Model not loaded";
            return result;
        }
        switch (loaded_version) {
            case ModelVersion::V1:
                return v1->predict(image);
            case ModelVersion::V2:
                return v2->predict(image);
        }
        PredictResult result;
        result.success = false;
        result.error = "Unknown model version";
        return result;
    }

    void release() {
        if (v1) {
            v1->release();
            v1.reset();
        }
        if (v2) {
            v2->release();
            v2.reset();
        }
        loaded_precision.clear();
        loaded_use_gpu = false;
        loaded_num_threads = 0;
        is_init = false;
        status = ModelStatus::NotLoaded;
    }
};

CasOcr::CasOcr(std::string model_dir) : impl_(new Impl(std::move(model_dir))) {}

CasOcr::~CasOcr() = default;

CasOcr::CasOcr(CasOcr&&) noexcept = default;
CasOcr& CasOcr::operator=(CasOcr&&) noexcept = default;

bool CasOcr::load_model(const std::string_view precision,
                        const bool use_gpu,
                        const int num_threads,
                        ModelVersion version) {
    return impl_->load_all_models(
        precision.empty() ? "fp16" : std::string(precision),
        use_gpu,
        num_threads,
        version);
}

void CasOcr::release() {
    impl_->release();
}

bool CasOcr::is_loaded() const {
    return impl_->is_init;
}

ModelStatus CasOcr::model_status() const {
    return impl_->status;
}

ModelVersion CasOcr::model_version() const {
    return impl_->loaded_version;
}

PredictResult CasOcr::predict(const cv::Mat& image) {
    if (!is_loaded()) {
        PredictResult result;
        result.success = false;
        result.error = "Model not loaded";
        return result;
    }
    if (image.empty()) {
        PredictResult result;
        result.success = false;
        result.error = "Empty input image";
        return result;
    }
    std::lock_guard<std::mutex> lock(impl_->inference_mutex);
    return impl_->predict_dispatch(image);
}

PredictResult CasOcr::predict(const std::string_view image_path) {
    const cv::Mat image = cv::imread(std::string(image_path), cv::IMREAD_COLOR);
    if (image.empty()) {
        PredictResult result;
        result.success = false;
        result.error = "Failed to read image: " + std::string(image_path);
        return result;
    }
    return predict(image);
}

PredictResult CasOcr::predict(const std::span<const uint8_t> image_data) {
    if (image_data.empty()) {
        PredictResult result;
        result.success = false;
        result.error = "Empty image data";
        return result;
    }
    const cv::Mat encoded(
        1,
        static_cast<int>(image_data.size()),
        CV_8UC1,
        const_cast<uint8_t*>(image_data.data()));
    const cv::Mat image = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (image.empty()) {
        PredictResult result;
        result.success = false;
        result.error = "Failed to decode image data";
        return result;
    }
    return predict(image);
}

#ifdef NCNN_SUPPORT_VULKAN

int CasOcr::gpu_count() {
    return ncnn::get_gpu_count();
}

bool CasOcr::is_vulkan_supported() {
    return gpu_count() > 0;
}

int CasOcr::default_gpu_index() {
    return ncnn::get_default_gpu_index();
}

GpuDeviceInfo CasOcr::gpu_info(int gpu_index) {
    GpuDeviceInfo info;
    if (gpu_index < 0) {
        gpu_index = default_gpu_index();
    }
    if (gpu_index < 0 || gpu_index >= gpu_count()) {
        return info;
    }
    const ncnn::GpuInfo& gpu_info = ncnn::get_gpu_device(gpu_index)->info;
    info.device_index = gpu_index;
    info.device_name = gpu_info.device_name();
    info.api_version = gpu_info.api_version();
    info.device_memory = gpu_info.max_shared_memory_size();
    info.device_type = static_cast<VulkanDeviceType>(gpu_info.type());
    return info;
}

std::vector<GpuDeviceInfo> CasOcr::all_gpu_info() {
    const int count = gpu_count();
    std::vector<GpuDeviceInfo> devices;
    devices.reserve(count);
    for (int i = 0; i < count; ++i) {
        devices.push_back(gpu_info(i));
    }
    return devices;
}

#endif

}  // namespace shmtu::cas::ocr
