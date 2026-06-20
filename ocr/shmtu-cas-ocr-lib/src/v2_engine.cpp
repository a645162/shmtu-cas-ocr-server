#include "v2_engine.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <net.h>

#ifdef NCNN_SUPPORT_VULKAN
#include <gpu.h>
#endif

namespace shmtu::cas::ocr {

namespace {

// V2 mobilenet_v3_small.trislot_decoder model:
//   input  : grayscale 1x64x192 (W=192, H=64), normalized to [0,1]
//   output : three heads (digit_left / operator / digit_right)
constexpr int kInputWidth = 192;
constexpr int kInputHeight = 64;
constexpr const char* kParamStem =
    "mobilenet_v3_small.trislot_decoder.v2_0";

bool path_check_windows_style(const std::string& dir_path) {
    for (const char c : dir_path) {
        if (c == '\\') {
            return true;
        }
    }
    return false;
}

void path_ensure_slash(std::string& dir_path) {
    if (!dir_path.empty() && dir_path.back() != '/' && dir_path.back() != '\\') {
        dir_path += path_check_windows_style(dir_path) ? "\\" : "/";
    }
}

// Operator encoding in V2: 0=Add, 1=Sub, 2=Mul (3 classes, no CHS variants).
int compute_result_v2(int left, int right, int op) {
    switch (op) {
        case 0:
            return left + right;
        case 1:
            return left - right;
        case 2:
            return left * right;
        default:
            return 0;
    }
}

const char* operator_char_v2(int op) {
    switch (op) {
        case 0:
            return "+";
        case 1:
            return "-";
        case 2:
            return "*";
        default:
            return "?";
    }
}

// Map V2's 3-class operator index onto the existing Operator enum (6 classes)
// so downstream JSON consumers that decode `op` keep working.
//   0 (Add)  -> Add      (0)
//   1 (Sub)  -> Sub      (2)
//   2 (Mul)  -> Mul      (4)
int map_v2_op_to_legacy(int v2_op) {
    switch (v2_op) {
        case 0:
            return static_cast<int>(Operator::Add);
        case 1:
            return static_cast<int>(Operator::Sub);
        case 2:
            return static_cast<int>(Operator::Mul);
        default:
            return static_cast<int>(Operator::Add);
    }
}

int argmax(const ncnn::Mat& m) {
    if (m.w <= 0) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < m.w; ++i) {
        if (m[i] > m[best]) {
            best = i;
        }
    }
    return best;
}

cv::Mat preprocess_v2_input(const cv::Mat& bgr_image) {
    // Align with Model/.../common/preprocess.py default "min_channel_otsu".
    cv::Mat score;
    if (bgr_image.channels() == 3) {
        std::vector<cv::Mat> channels;
        cv::split(bgr_image, channels);
        cv::Mat min_bg;
        cv::min(channels[0], channels[1], min_bg);
        cv::Mat min_bgr;
        cv::min(min_bg, channels[2], min_bgr);
        score = cv::Scalar::all(255) - min_bgr;
    } else if (bgr_image.channels() == 4) {
        std::vector<cv::Mat> channels;
        cv::split(bgr_image, channels);
        cv::Mat min_bg;
        cv::min(channels[0], channels[1], min_bg);
        cv::Mat min_bgr;
        cv::min(min_bg, channels[2], min_bgr);
        score = cv::Scalar::all(255) - min_bgr;
    } else if (bgr_image.channels() == 1) {
        score = bgr_image.clone();
    } else {
        cv::Mat gray;
        cv::cvtColor(bgr_image, gray, cv::COLOR_BGR2GRAY);
        score = gray;
    }

    cv::Mat blur;
    cv::GaussianBlur(score, blur, cv::Size(3, 3), 0.0);

    cv::Mat binary;
    cv::threshold(blur, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    cv::Mat resized;
    cv::resize(binary, resized, cv::Size(kInputWidth, kInputHeight), 0.0, 0.0, cv::INTER_NEAREST);
    return resized;
}

}  // namespace

struct V2Engine::Impl {
    ncnn::UnlockedPoolAllocator blob_allocator;
    ncnn::PoolAllocator workspace_allocator;
    ncnn::Net net;

    static bool resolve_use_gpu(bool requested) {
#ifdef NCNN_SUPPORT_VULKAN
        return requested && ncnn::get_gpu_count() > 0;
#else
        (void)requested;
        return false;
#endif
    }

    static int resolve_num_threads(int requested, bool use_gpu) {
        if (requested > 0) {
            return requested;
        }
        const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());
        if (use_gpu) {
            return 1;
        }
        return static_cast<int>(std::min(hardware_threads, 4u));
    }

    void configure(bool use_gpu, int num_threads) {
        ncnn::Option& opt = net.opt;
        opt.lightmode = true;
        opt.num_threads = std::max(1, num_threads);
        opt.blob_allocator = &blob_allocator;
        opt.workspace_allocator = &workspace_allocator;
#ifdef NCNN_SUPPORT_VULKAN
        opt.use_vulkan_compute = use_gpu;
#else
        (void)use_gpu;
#endif
    }

    bool load_models(const std::string& dir_path, const std::string& precision) {
        std::string dir = dir_path;
        path_ensure_slash(dir);
        const std::string param_path = dir + kParamStem + "." + precision + ".param";
        const std::string bin_path = dir + kParamStem + "." + precision + ".bin";
        return net.load_param(param_path.c_str()) == 0 &&
               net.load_model(bin_path.c_str()) == 0;
    }

    PredictResult predict_image(const cv::Mat& bgr_image) {
        PredictResult result;
        result.model_version = static_cast<int>(ModelVersion::V2);
        try {
            cv::Mat gray = preprocess_v2_input(bgr_image);

            // Grayscale normalization to [0, 1]: mean=0, scale=1/255.
            ncnn::Mat in = ncnn::Mat::from_pixels(
                gray.data, ncnn::Mat::PIXEL_GRAY, gray.cols, gray.rows);
            const float norm[1] = {1.0f / 255.0f};
            in.substract_mean_normalize(nullptr, norm);

            ncnn::Extractor ex = net.create_extractor();
            ex.input("in0", in);

            // pnnx's ncnn export for this v2 model uses the literal blob
            // names emitted by the Python forward graph:
            //   in0  -> input  (1 x 64 x 192 grayscale)
            //   out0 -> digit_left   (10 classes)
            //   out1 -> operator      (3 classes: 0=Add, 1=Sub, 2=Mul)
            //   out2 -> digit_right  (10 classes)
            // These names were confirmed by reading the .param file
            // (`grep -E '^(Input|InnerProduct)' *.param`) and by inspecting
            // `net.output_names()` in an interactive ncnn run.
            ncnn::Mat m_digit_left, m_operator, m_digit_right;
            const int rc_left  = ex.extract("out0", m_digit_left);
            const int rc_op    = ex.extract("out1", m_operator);
            const int rc_right = ex.extract("out2", m_digit_right);
            if (rc_left != 0 || rc_op != 0 || rc_right != 0) {
                result.success = false;
                result.error = "V2 model output extraction failed "
                               "(expected blobs: out0=10, out1=3, out2=10)";
                return result;
            }

            const int digit_left = argmax(m_digit_left);
            const int op_v2 = argmax(m_operator);
            const int digit_right = argmax(m_digit_right);
            if (digit_left < 0 || op_v2 < 0 || digit_right < 0) {
                result.success = false;
                result.error = "V2 model produced empty logits";
                return result;
            }

            result.digit1 = digit_left;
            result.digit2 = digit_right;
            result.op = map_v2_op_to_legacy(op_v2);
            result.result = compute_result_v2(digit_left, digit_right, op_v2);
            result.equal_symbol = -1;  // N/A in V2
            result.expression = std::to_string(digit_left) + " " +
                                operator_char_v2(op_v2) + " " +
                                std::to_string(digit_right) + " = " +
                                std::to_string(result.result);
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.error = std::string("V2 prediction exception: ") + e.what();
        }
        return result;
    }

    void release() {
        net.clear();
        blob_allocator.clear();
        workspace_allocator.clear();
    }
};

V2Engine::V2Engine() : impl_(new Impl()) {}
V2Engine::~V2Engine() {
    if (impl_) {
        impl_->release();
        delete impl_;
    }
}

bool V2Engine::load(const std::string& model_dir,
                    const std::string_view precision,
                    const bool use_gpu,
                    const int num_threads) {
    if (model_dir.empty() || !impl_) {
        return false;
    }
    const std::string precision_str = precision.empty() ? std::string("fp16")
                                                       : std::string(precision);
    const bool actual_use_gpu = Impl::resolve_use_gpu(use_gpu);
    const int actual_num_threads = Impl::resolve_num_threads(num_threads, actual_use_gpu);

    release();
    impl_->configure(actual_use_gpu, actual_num_threads);
    if (!impl_->load_models(model_dir, precision_str)) {
        release();
        return false;
    }
    is_init_ = true;
    status_ = actual_use_gpu ? ModelStatus::LoadedGPU : ModelStatus::LoadedCPU;
    return true;
}

void V2Engine::release() {
    if (impl_) {
        impl_->release();
    }
    is_init_ = false;
    status_ = ModelStatus::NotLoaded;
}

PredictResult V2Engine::predict(const cv::Mat& bgr_image) {
    if (!is_init_ || !impl_) {
        PredictResult result;
        result.success = false;
        result.error = "V2 model not loaded";
        result.model_version = static_cast<int>(ModelVersion::V2);
        return result;
    }
    return impl_->predict_image(bgr_image);
}

}  // namespace shmtu::cas::ocr
