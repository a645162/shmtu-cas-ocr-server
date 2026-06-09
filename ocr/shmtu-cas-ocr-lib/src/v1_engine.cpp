#include "v1_engine.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <net.h>

#ifdef NCNN_SUPPORT_VULKAN
#include <gpu.h>
#endif

namespace shmtu::cas::ocr {

namespace {

// ImageNet mean/std for the V1 BGR pipeline.
constexpr float kMeanValues[3] = {123.675f, 116.28f, 103.53f};
constexpr float kNormValues[3] = {
    1.0f / 58.395f,
    1.0f / 57.12f,
    1.0f / 57.375f
};

constexpr float kEqualSymbolKeyStart = 0.7f;
constexpr float kEqualSymbolKeyEnd = 1.0f;
constexpr float kKeyPointSymbol[3] = {0.25f, 0.58f, 0.75f};
constexpr float kKeyPointChs[3] = {0.15f, 0.33f, 0.46f};
constexpr int kConfigThresh = 200;

constexpr int kInputSize = 224;

cv::Mat split_img_by_ratio(const cv::Mat& image,
                           float start_ratio = 0.7f,
                           float end_ratio = 1.0f) {
    const int height = image.rows;
    const int width = image.cols;
    if (start_ratio > end_ratio) {
        std::swap(start_ratio, end_ratio);
    }
    const int horizontal_start = static_cast<int>(static_cast<float>(width) * start_ratio);
    int horizontal_end = static_cast<int>(static_cast<float>(width) * end_ratio);
    if (end_ratio >= 1.0f) {
        horizontal_end = width;
    }
    return image(cv::Rect(horizontal_start, 0, horizontal_end - horizontal_start, height)).clone();
}

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

Operator simplify_operator(const int raw_op) {
    switch (raw_op) {
        case 0:
        case 1:
            return Operator::Add;
        case 2:
        case 3:
            return Operator::Sub;
        case 4:
        case 5:
            return Operator::Mul;
        default:
            return Operator::Add;
    }
}

std::string operator_to_string(const int raw_op) {
    switch (raw_op) {
        case 0:
        case 1:
            return "+";
        case 2:
        case 3:
            return "-";
        case 4:
        case 5:
            return "*";
        default:
            return "";
    }
}

int compute_result(const int left, const int right, const int raw_op) {
    switch (simplify_operator(raw_op)) {
        case Operator::Add:
            return left + right;
        case Operator::Sub:
            return left - right;
        case Operator::Mul:
            return left * right;
        default:
            return 0;
    }
}

}  // namespace

struct V1Engine::Impl {
    ncnn::UnlockedPoolAllocator blob_allocator;
    ncnn::PoolAllocator workspace_allocator;

    ncnn::Net net_equal_symbol;
    ncnn::Net net_operator;
    ncnn::Net net_digit;

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

    void set_net_opt(ncnn::Net& net, bool use_gpu, int num_threads) {
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

    bool init_model_for_net(ncnn::Net& net,
                            const std::string& dir_path,
                            const std::string& name,
                            const std::string& precision) {
        std::string dir = dir_path;
        path_ensure_slash(dir);
        const std::string file_name_param = dir + name + "." + precision + ".param";
        const std::string file_name_model = dir + name + "." + precision + ".bin";
        const int ret_param = net.load_param(file_name_param.c_str());
        const int ret_model = net.load_model(file_name_model.c_str());
        return ret_param == 0 && ret_model == 0;
    }

    int predict_by_model(const ncnn::Net& net, const cv::Mat& input_image) const {
        cv::Mat image = input_image.clone();
        cv::resize(image, image, cv::Size(kInputSize, kInputSize));
        if (image.channels() != 3) {
            return -1;
        }
        ncnn::Mat in = ncnn::Mat::from_pixels(
            image.data, ncnn::Mat::PIXEL_BGR, image.cols, image.rows);
        in.substract_mean_normalize(kMeanValues, kNormValues);

        ncnn::Extractor ex = net.create_extractor();
        ex.input("input", in);
        ncnn::Mat out;
        ex.extract("output", out);

        const int output_count = out.w;
        int max_index = 0;
        for (int j = 0; j < output_count; ++j) {
            if (out[j] > out[max_index]) {
                max_index = j;
            }
        }
        return max_index;
    }

    PredictResult predict_validate_code(const cv::Mat& image_input) {
        PredictResult result;
        result.model_version = static_cast<int>(ModelVersion::V1);

        try {
            cv::Mat image_gray;
            cv::cvtColor(image_input, image_gray, cv::COLOR_BGR2GRAY);
            cv::threshold(image_gray, image_gray, kConfigThresh, 255, cv::THRESH_BINARY);

            cv::Mat image(image_gray.size(), CV_8UC3);
            cv::merge(std::vector<cv::Mat>{image_gray, image_gray, image_gray}, image);

            const auto image_equal_symbol =
                split_img_by_ratio(image, kEqualSymbolKeyStart, kEqualSymbolKeyEnd);
            const int predicted_equal_symbol = predict_by_model(net_equal_symbol, image_equal_symbol);
            if (predicted_equal_symbol < 0) {
                result.success = false;
                result.error = "Failed to predict equal symbol";
                return result;
            }

            const float* key_point =
                predicted_equal_symbol == static_cast<int>(EqualSymbol::CHS) ? kKeyPointChs
                                                                             : kKeyPointSymbol;

            const auto image_digit_1 = split_img_by_ratio(image, 0.0f, *(key_point + 0));
            const auto image_operator =
                split_img_by_ratio(image, *(key_point + 0), *(key_point + 1));
            const auto image_digit_2 =
                split_img_by_ratio(image, *(key_point + 1), *(key_point + 2));

            const int predicted_operator = predict_by_model(net_operator, image_operator);
            const int predicted_digit_1 = predict_by_model(net_digit, image_digit_1);
            const int predicted_digit_2 = predict_by_model(net_digit, image_digit_2);

            result.equal_symbol = predicted_equal_symbol;
            result.op = predicted_operator;
            result.digit1 = predicted_digit_1;
            result.digit2 = predicted_digit_2;
            result.result =
                compute_result(predicted_digit_1, predicted_digit_2, predicted_operator);
            result.expression = std::to_string(predicted_digit_1) + " " +
                                operator_to_string(predicted_operator) + " " +
                                std::to_string(predicted_digit_2) + " = " +
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
        blob_allocator.clear();
        workspace_allocator.clear();
    }
};

V1Engine::V1Engine() : impl_(new Impl()) {}
V1Engine::~V1Engine() {
    if (impl_) {
        impl_->release();
        delete impl_;
    }
}

bool V1Engine::load(const std::string& model_dir,
                    const std::string_view precision,
                    const bool use_gpu,
                    const int num_threads) {
    if (model_dir.empty()) {
        return false;
    }
    if (!impl_) {
        return false;
    }

    const std::string precision_str = precision.empty() ? std::string("fp16")
                                                       : std::string(precision);
    const bool actual_use_gpu = Impl::resolve_use_gpu(use_gpu);
    const int actual_num_threads = Impl::resolve_num_threads(num_threads, actual_use_gpu);

    release();
    impl_->set_net_opt(impl_->net_equal_symbol, actual_use_gpu, actual_num_threads);
    impl_->set_net_opt(impl_->net_operator, actual_use_gpu, actual_num_threads);
    impl_->set_net_opt(impl_->net_digit, actual_use_gpu, actual_num_threads);

    bool ok = true;
    ok = impl_->init_model_for_net(
             impl_->net_equal_symbol, model_dir,
             "resnet18_equal_symbol_latest", precision_str) && ok;
    ok = impl_->init_model_for_net(
             impl_->net_operator, model_dir,
             "resnet18_operator_latest", precision_str) && ok;
    ok = impl_->init_model_for_net(
             impl_->net_digit, model_dir,
             "resnet34_digit_latest", precision_str) && ok;
    if (!ok) {
        release();
        return false;
    }

    is_init_ = true;
    status_ = actual_use_gpu ? ModelStatus::LoadedGPU : ModelStatus::LoadedCPU;
    return true;
}

void V1Engine::release() {
    if (impl_) {
        impl_->release();
    }
    is_init_ = false;
    status_ = ModelStatus::NotLoaded;
}

PredictResult V1Engine::predict(const cv::Mat& bgr_image) {
    if (!is_init_ || !impl_) {
        PredictResult result;
        result.success = false;
        result.error = "V1 model not loaded";
        result.model_version = static_cast<int>(ModelVersion::V1);
        return result;
    }
    return impl_->predict_validate_code(bgr_image);
}

}  // namespace shmtu::cas::ocr
