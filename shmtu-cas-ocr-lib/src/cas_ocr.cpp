#include <shmtu/cas_ocr/cas_ocr.h>

#include <mutex>
#include <cstring>
#include <algorithm>

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// NCNN
#include <net.h>

#ifdef NCNN_SUPPORT_VULKAN
#include <gpu.h>
#endif

namespace shmtu::cas_ocr {

// ---------------------------------------------------------------------------
// Internal constants (previously global / namespace-scope)
// ---------------------------------------------------------------------------

static constexpr float kMeanValues[3] = {123.675f, 116.28f, 103.53f};
static constexpr float kNormValues[3] = {
    1.0f / 58.395f,
    1.0f / 57.12f,
    1.0f / 57.375f
};

// Python Version: src/config/config.py
static constexpr float kEqualSymbolKeyStart = 0.7f;
static constexpr float kEqualSymbolKeyEnd   = 1.0f;
static constexpr float kKeyPointSymbol[3]   = {0.25f, 0.58f, 0.75f};
static constexpr float kKeyPointCHS[3]      = {0.15f, 0.33f, 0.46f};
static constexpr int   kConfigThresh        = 200;

// ---------------------------------------------------------------------------
// Helpers (file-local)
// ---------------------------------------------------------------------------

// Split image horizontally by ratio range.
static cv::Mat split_img_by_ratio(
    const cv::Mat& image,
    float start_ratio,
    float end_ratio
) {
    const int height = image.rows;
    const int width  = image.cols;

    if (start_ratio > end_ratio) {
        std::swap(start_ratio, end_ratio);
    }

    const int x_start = static_cast<int>(
        static_cast<float>(width) * start_ratio
    );
    int x_end = static_cast<int>(
        static_cast<float>(width) * end_ratio
    );
    if (end_ratio >= 1.0f) {
        x_end = width;
    }

    return image(
        cv::Rect(x_start, 0, x_end - x_start, height)
    ).clone();
}

// Ensure dir_path ends with a path separator.
static std::string ensure_trailing_slash(std::string dir_path) {
    if (!dir_path.empty() && dir_path.back() != '/' && dir_path.back() != '\\') {
#ifdef _WIN32
        dir_path += '\\';
#else
        dir_path += '/';
#endif
    }
    return dir_path;
}

// Map raw operator index to simplified operator type.
static Operator simplify_operator(int raw_op) {
    switch (raw_op) {
        case 0: case 1: return Operator::Add;
        case 2: case 3: return Operator::Sub;
        case 4: case 5: return Operator::Mul;
        default:         return Operator::Add;
    }
}

// Get display string for operator.
static const char* operator_to_str(int raw_op) {
    switch (raw_op) {
        case 0: case 1: return "+";
        case 2: case 3: return "-";
        case 4: case 5: return "*";
        default:         return "?";
    }
}

// Compute arithmetic result.
static int compute_result(int digit1, int digit2, int raw_op) {
    switch (simplify_operator(raw_op)) {
        case Operator::Add: return digit1 + digit2;
        case Operator::Sub: return digit1 - digit2;
        case Operator::Mul: return digit1 * digit2;
        default:            return 0;
    }
}

// ---------------------------------------------------------------------------
// CasOcr::Impl — all NCNN/OpenCV internals live here
// ---------------------------------------------------------------------------

struct CasOcr::Impl {
    std::string model_dir;
    bool use_gpu;
    ModelStatus status = ModelStatus::NotLoaded;

    // NCNN networks (each CasOcr instance owns its own)
    ncnn::Net net_equal_symbol;
    ncnn::Net net_operator;
    ncnn::Net net_digit;

    // NCNN memory allocators (following Android version pattern)
    ncnn::UnlockedPoolAllocator blob_allocator;
    ncnn::PoolAllocator workspace_allocator;

    // Mutex for thread-safe inference (one inference at a time per instance)
    std::mutex inference_mutex;

    Impl(const std::string& dir, bool gpu)
        : model_dir(dir), use_gpu(gpu) {}

    // Set NCNN net options (following Android version's set_net_opt).
    void set_net_opt(ncnn::Net& net) {
        ncnn::Option& opt = net.opt;
        opt.lightmode = true;
        opt.num_threads = 4;
        opt.blob_allocator = &blob_allocator;
        opt.workspace_allocator = &workspace_allocator;

#ifdef NCNN_SUPPORT_VULKAN
        // Only enable Vulkan if GPU requested AND devices are available.
        opt.use_vulkan_compute = use_gpu && (ncnn::get_gpu_count() > 0);
#endif
    }

    // Load a single model into a net.
    bool load_single_model(
        ncnn::Net& net,
        const std::string& dir_path,
        const std::string& name,
        const std::string& precision
    ) {
        auto full_dir = ensure_trailing_slash(dir_path);
        std::string param_path = full_dir + name + "." + precision + ".param";
        std::string model_path = full_dir + name + "." + precision + ".bin";

        int ret_param = net.load_param(param_path.c_str());
        int ret_model = net.load_model(model_path.c_str());

        if (ret_param != 0 || ret_model != 0) {
            return false;
        }
        return true;
    }

    // Load all three models.
    bool load_all_models(const std::string& precision) {
        // Configure nets before loading (NCNN requires opt set before load_param).
        set_net_opt(net_equal_symbol);
        set_net_opt(net_operator);
        set_net_opt(net_digit);

        bool ok = true;

        ok = ok && load_single_model(
            net_equal_symbol, model_dir,
            "resnet18_equal_symbol_latest", precision
        );
        ok = ok && load_single_model(
            net_operator, model_dir,
            "resnet18_operator_latest", precision
        );
        ok = ok && load_single_model(
            net_digit, model_dir,
            "resnet34_digit_latest", precision
        );

        if (ok) {
#ifdef NCNN_SUPPORT_VULKAN
            status = (use_gpu && ncnn::get_gpu_count() > 0)
                ? ModelStatus::LoadedGPU
                : ModelStatus::LoadedCPU;
#else
            status = ModelStatus::LoadedCPU;
#endif
        }
        return ok;
    }

    // Run inference on a single sub-image, returns class index.
    int predict_by_model(const ncnn::Net& net, const cv::Mat& input_image) const {
        cv::Mat image = input_image.clone();
        cv::resize(image, image, cv::Size(224, 224));

        if (image.channels() != 3) {
            return -1;
        }

        ncnn::Mat in = ncnn::Mat::from_pixels(
            image.data, ncnn::Mat::PIXEL_BGR, image.cols, image.rows
        );
        in.substract_mean_normalize(kMeanValues, kNormValues);

        ncnn::Extractor ex = net.create_extractor();
        ex.input("input", in);

        ncnn::Mat out;
        ex.extract("output", out);

        const int count = out.w;
        int max_idx = 0;
        for (int j = 1; j < count; ++j) {
            if (out[j] > out[max_idx]) {
                max_idx = j;
            }
        }
        return max_idx;
    }

    // Core prediction pipeline.
    PredictResult predict_validate_code(const cv::Mat& image_input) {
        PredictResult result;

        try {
            // Convert to grayscale, threshold, then back to 3-channel
            cv::Mat image_gray;
            cv::cvtColor(image_input, image_gray, cv::COLOR_BGR2GRAY);
            cv::threshold(
                image_gray, image_gray,
                kConfigThresh, 255, cv::THRESH_BINARY
            );

            cv::Mat image_3ch(image_gray.size(), CV_8UC3);
            cv::merge(
                std::vector<cv::Mat>{image_gray, image_gray, image_gray},
                image_3ch
            );

            // Step 1: Predict equal symbol type
            auto img_eq = split_img_by_ratio(
                image_3ch, kEqualSymbolKeyStart, kEqualSymbolKeyEnd
            );
            int pred_equal = predict_by_model(net_equal_symbol, img_eq);
            if (pred_equal < 0) {
                result.success = false;
                result.error = "Failed to predict equal symbol";
                return result;
            }

            // Step 2: Select key points based on equal symbol style
            const float* key_point = (pred_equal == static_cast<int>(EqualSymbol::CHS))
                ? kKeyPointCHS
                : kKeyPointSymbol;

            // Step 3: Split and predict operator
            auto img_op = split_img_by_ratio(
                image_3ch, key_point[0], key_point[1]
            );
            int pred_op = predict_by_model(net_operator, img_op);

            // Step 4: Split and predict digits
            auto img_d1 = split_img_by_ratio(
                image_3ch, 0.0f, key_point[0]
            );
            int pred_d1 = predict_by_model(net_digit, img_d1);

            auto img_d2 = split_img_by_ratio(
                image_3ch, key_point[1], key_point[2]
            );
            int pred_d2 = predict_by_model(net_digit, img_d2);

            // Step 5: Compute result
            result.digit1 = pred_d1;
            result.digit2 = pred_d2;
            result.op = pred_op;
            result.equal_symbol = pred_equal;
            result.result = compute_result(pred_d1, pred_d2, pred_op);
            result.expression =
                std::to_string(pred_d1) + " " +
                operator_to_str(pred_op) + " " +
                std::to_string(pred_d2) + " = " +
                std::to_string(result.result);
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.error = std::string("Prediction exception: ") + e.what();
        }

        return result;
    }

    void release() {
        net_equal_symbol.clear();
        net_operator.clear();
        net_digit.clear();
        status = ModelStatus::NotLoaded;
    }
};

// ---------------------------------------------------------------------------
// CasOcr public API — delegates to Impl
// ---------------------------------------------------------------------------

CasOcr::CasOcr(const std::string& model_dir, bool use_gpu)
    : impl_(std::make_unique<Impl>(model_dir, use_gpu)) {}

CasOcr::~CasOcr() = default;

CasOcr::CasOcr(CasOcr&&) noexcept = default;
CasOcr& CasOcr::operator=(CasOcr&&) noexcept = default;

bool CasOcr::load_model(const std::string& precision) {
    if (impl_->status != ModelStatus::NotLoaded) {
        return true;  // Already loaded
    }
    return impl_->load_all_models(precision);
}

void CasOcr::release() {
    impl_->release();
}

bool CasOcr::is_loaded() const {
    return impl_->status != ModelStatus::NotLoaded;
}

ModelStatus CasOcr::model_status() const {
    return impl_->status;
}

PredictResult CasOcr::predict(const cv::Mat& image) {
    if (!is_loaded()) {
        return {.success = false, .error = "Model not loaded"};
    }

    if (image.empty()) {
        return {.success = false, .error = "Empty input image"};
    }

    std::lock_guard<std::mutex> lock(impl_->inference_mutex);
    return impl_->predict_validate_code(image);
}

PredictResult CasOcr::predict(const std::string& image_path) {
    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if (image.empty()) {
        return {.success = false, .error = "Failed to read image: " + image_path};
    }
    return predict(image);
}

PredictResult CasOcr::predict(const std::vector<uint8_t>& image_data) {
    if (image_data.empty()) {
        return {.success = false, .error = "Empty image data"};
    }

    cv::Mat image = cv::imdecode(image_data, cv::IMREAD_COLOR);
    if (image.empty()) {
        return {.success = false, .error = "Failed to decode image data"};
    }

    // Resize to expected CAPTCHA dimensions
    cv::resize(image, image, cv::Size(400, 140));

    return predict(image);
}

// ---------------------------------------------------------------------------
// Vulkan / GPU static helpers
// ---------------------------------------------------------------------------

#ifdef NCNN_SUPPORT_VULKAN

int CasOcr::gpu_count() {
    return ncnn::get_gpu_count();
}

bool CasOcr::is_vulkan_supported() {
    return ncnn::get_gpu_count() > 0;
}

int CasOcr::default_gpu_index() {
    return ncnn::get_default_gpu_index();
}

GpuDeviceInfo CasOcr::gpu_info(int gpu_index) {
    if (gpu_index < 0) {
        gpu_index = default_gpu_index();
    }

    GpuDeviceInfo info;
    if (gpu_index >= gpu_count()) {
        return info;
    }

    const ncnn::GpuInfo& gi = ncnn::get_gpu_device(gpu_index)->info;
    info.device_index  = gpu_index;
    info.device_name   = gi.device_name();
    info.api_version   = gi.api_version();
    info.device_memory = gi.max_shared_memory_size();
    info.device_type   = static_cast<VulkanDeviceType>(gi.type());

    return info;
}

std::vector<GpuDeviceInfo> CasOcr::all_gpu_info() {
    const int count = gpu_count();
    std::vector<GpuDeviceInfo> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back(gpu_info(i));
    }
    return result;
}

#endif // NCNN_SUPPORT_VULKAN

} // namespace shmtu::cas_ocr
