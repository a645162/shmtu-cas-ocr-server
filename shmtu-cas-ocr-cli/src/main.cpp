// SHMTU CAS OCR CLI — Command-line tool for CAPTCHA OCR recognition
// Supports single image, directory batch, and JSON output

#include <shmtu/cas_ocr/cas_ocr.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <cctype>

namespace fs = std::filesystem;

#ifndef SHMTU_CAS_CLI_VERSION
#define SHMTU_CAS_CLI_VERSION "2.0.0"
#endif

static void print_banner() {
    printf("SHMTU CAS OCR CLI V%s\n", SHMTU_CAS_CLI_VERSION);
}

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS] <image_path_or_directory>\n\n", prog);
    printf("Options:\n");
    printf("  --model-dir <path>       Model directory (default: ./models)\n");
    printf("  --precision <fp16|fp32>  Model precision (default: fp16)\n");
    printf("  --use-gpu                Enable GPU acceleration\n");
    printf("  --json                   Output results as JSON\n");
    printf("  --help, -h               Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s captcha.png\n", prog);
    printf("  %s --json ./captcha_images/\n", prog);
    printf("  %s --model-dir /opt/models --precision fp32 image.jpg\n", prog);
}

struct CliConfig {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
    bool json_output = false;
    std::string input_path;
};

static CliConfig parse_args(int argc, char* argv[]) {
    CliConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--model-dir" && i + 1 < argc) {
            config.model_dir = argv[++i];
        } else if (arg == "--precision" && i + 1 < argc) {
            config.precision = argv[++i];
        } else if (arg == "--use-gpu") {
            config.use_gpu = true;
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg[0] != '-') {
            config.input_path = arg;
        } else {
            fprintf(stderr, "Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            exit(1);
        }
    }

    return config;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string predict_result_to_json(const shmtu::cas_ocr::PredictResult& r) {
    std::string json = "{";
    json += "\"success\":" + std::string(r.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(r.expression) + "\",";
    json += "\"result\":" + std::to_string(r.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(r.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(r.op) + ",";
    json += "\"digit1\":" + std::to_string(r.digit1) + ",";
    json += "\"digit2\":" + std::to_string(r.digit2);
    if (!r.error.empty()) {
        json += ",\"error\":\"" + json_escape(r.error) + "\"";
    }
    json += "}";
    return json;
}

static void process_image(
    shmtu::cas_ocr::CasOcr& ocr,
    const std::string& path,
    bool json_output
) {
    auto result = ocr.predict(path);

    if (json_output) {
        printf("{\"file\":\"%s\",\"result\":%s}\n",
               json_escape(path).c_str(),
               predict_result_to_json(result).c_str());
    } else {
        if (result.success) {
            printf("[%s] %s  =>  %d\n",
                   path.c_str(), result.expression.c_str(), result.result);
        } else {
            fprintf(stderr, "[%s] ERROR: %s\n",
                    path.c_str(), result.error.c_str());
        }
    }
}

int main(int argc, char* argv[]) {
    print_banner();

    auto config = parse_args(argc, argv);

    if (config.input_path.empty()) {
        fprintf(stderr, "Error: No input path specified.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    // Initialize OCR engine
    shmtu::cas_ocr::CasOcr ocr(config.model_dir, config.use_gpu);

    printf("Loading models from: %s (precision=%s)...\n",
           config.model_dir.c_str(), config.precision.c_str());

    if (!ocr.load_model(config.precision)) {
        fprintf(stderr, "Failed to load models.\n");
        return 1;
    }

    printf("Model loaded (%s).\n\n",
           ocr.model_status() == shmtu::cas_ocr::ModelStatus::LoadedGPU ? "GPU" : "CPU");

    fs::path input(config.input_path);

    if (fs::is_directory(input)) {
        // Batch mode: process all images in directory
        int count = 0;

        if (config.json_output) {
            printf("[\n");
        }

        for (const auto& entry : fs::directory_iterator(input)) {
            if (!entry.is_regular_file()) continue;

            auto ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(c));

            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
                ext != ".bmp" && ext != ".tif" && ext != ".tiff") {
                continue;
            }

            if (config.json_output && count > 0) {
                printf(",\n");
            }

            process_image(ocr, entry.path().string(), config.json_output);
            ++count;
        }

        if (config.json_output) {
            printf("\n]\n");
        } else {
            printf("\nProcessed %d image(s).\n", count);
        }
    } else if (fs::exists(input)) {
        // Single image mode
        process_image(ocr, config.input_path, config.json_output);
    } else {
        fprintf(stderr, "Error: Path does not exist: %s\n", config.input_path.c_str());
        return 1;
    }

    return 0;
}
