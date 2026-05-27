// SHMTU CAS OCR CLI — Command-line tool for CAPTCHA OCR recognition
// Supports single image, directory batch, JSON output, and server comparison mode

#include <shmtu/cas_ocr/cas_ocr.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <vector>
#include <algorithm>
#include <numeric>

// cpp-httplib for HTTP client mode
#include <httplib.h>

// nlohmann/json is bundled with Drogon; we use a minimal hand-rolled parser
// for the server response to keep CLI independent of Drogon.
// For full JSON parsing we include a tiny inline implementation.

namespace fs = std::filesystem;

#ifndef SHMTU_CAS_CLI_VERSION
#define SHMTU_CAS_CLI_VERSION "2.1.0"
#endif

// ===========================================================================
// Minimal JSON string extraction (no external dependency)
// ===========================================================================

namespace json_util {

// Extract a string value for a given key from a JSON object string.
// Returns "" if key not found or value is not a string.
static std::string extract_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";

    // Find the colon after the key
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;

    if (pos >= json.size() || json[pos] != '"') return "";

    // Extract string content (handle escapes minimally)
    std::string result;
    pos++; // skip opening quote
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++; // skip backslash
            switch (json[pos]) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

// Extract an integer value for a given key from a JSON object string.
// Returns 0 if key not found or value is not a number.
static int extract_int(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;

    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos) return 0;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    std::string num_str;
    while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
        num_str += json[pos];
        pos++;
    }
    if (num_str.empty()) return 0;
    return std::atoi(num_str.c_str());
}

// Extract a boolean value for a given key from a JSON object string.
static bool extract_bool(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos) return false;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos + 3 < json.size() && json.substr(pos, 4) == "true") return true;
    return false;
}

} // namespace json_util

// ===========================================================================
// Base64 encoding for HTTP client mode
// ===========================================================================

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     static_cast<uint32_t>(data[i + 2]);
        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += kBase64Table[(n >> 6) & 0x3F];
        result += kBase64Table[n & 0x3F];
    }

    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;

        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? kBase64Table[(n >> 6) & 0x3F] : '=';
        result += '=';
    }

    return result;
}

// ===========================================================================
// Remote OCR result (parsed from server JSON response)
// ===========================================================================

struct RemoteOcrResult {
    bool success = false;
    std::string expression;
    int result = 0;
    int equal_symbol = 0;
    int op = 0;
    int digit1 = 0;
    int digit2 = 0;
    std::string error;
    int http_status = 0;
    bool request_ok = false; // whether HTTP request itself succeeded
};

// ===========================================================================
// CLI configuration
// ===========================================================================

struct CliConfig {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
    bool json_output = false;
    std::string input_path;

    // HTTP client mode
    std::string server_host;
    int server_port = 21600; // default HTTP port for C++ server
    bool server_mode = false;

    // Compare mode
    bool compare_mode = false;
};

// ===========================================================================
// Banner and usage
// ===========================================================================

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
    printf("  --server <host:port>     Use remote OCR server instead of local model\n");
    printf("  --compare                Compare local OCR vs remote server results\n");
    printf("  --help, -h               Show this help\n");
    printf("\n");
    printf("Modes:\n");
    printf("  Local mode (default):\n");
    printf("    Load ONNX models locally and run OCR inference.\n");
    printf("\n");
    printf("  Server mode (--server):\n");
    printf("    Send images to a remote OCR server via HTTP API.\n");
    printf("    No local model files required.\n");
    printf("\n");
    printf("  Compare mode (--server + --compare):\n");
    printf("    Run both local OCR and remote API, then compare results.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s captcha.png\n", prog);
    printf("  %s --json ./captcha_images/\n", prog);
    printf("  %s --server 127.0.0.1:21600 captcha.png\n", prog);
    printf("  %s --server 127.0.0.1:21600 --compare ./captcha_images/\n", prog);
    printf("  %s --model-dir /opt/models --precision fp32 image.jpg\n", prog);
}

// ===========================================================================
// Argument parsing
// ===========================================================================

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
        } else if (arg == "--server" && i + 1 < argc) {
            std::string server_arg = argv[++i];
            config.server_mode = true;
            // Parse host:port
            auto colon_pos = server_arg.rfind(':');
            if (colon_pos != std::string::npos) {
                config.server_host = server_arg.substr(0, colon_pos);
                config.server_port = std::atoi(server_arg.substr(colon_pos + 1).c_str());
                if (config.server_port <= 0) config.server_port = 21600;
            } else {
                config.server_host = server_arg;
                config.server_port = 21600;
            }
        } else if (arg == "--compare") {
            config.compare_mode = true;
        } else if (arg[0] != '-') {
            config.input_path = arg;
        } else {
            fprintf(stderr, "Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            exit(1);
        }
    }

    // --compare requires --server
    if (config.compare_mode && !config.server_mode) {
        fprintf(stderr, "Error: --compare requires --server to be specified.\n\n");
        print_usage(argv[0]);
        exit(1);
    }

    return config;
}

// ===========================================================================
// JSON helpers for local results
// ===========================================================================

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

static std::string remote_result_to_json(const RemoteOcrResult& r) {
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
    if (!r.request_ok) {
        json += ",\"httpError\":\"request failed\"";
    } else if (r.http_status != 200) {
        json += ",\"httpStatus\":" + std::to_string(r.http_status);
    }
    json += "}";
    return json;
}

// ===========================================================================
// HTTP client: call remote OCR server
// ===========================================================================

static RemoteOcrResult call_remote_ocr(
    const std::string& host,
    int port,
    const std::vector<uint8_t>& image_bytes,
    int timeout_sec = 30
) {
    RemoteOcrResult result;

    try {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(timeout_sec);
        cli.set_read_timeout(timeout_sec);

        std::string b64 = base64_encode(image_bytes);
        std::string body = "{\"imageBase64\":\"" + b64 + "\"}";

        auto res = cli.Post("/api/ocr", body, "application/json");

        if (!res) {
            result.request_ok = false;
            result.error = "HTTP request failed: " + std::to_string(static_cast<int>(res.error()));
            return result;
        }

        result.request_ok = true;
        result.http_status = res->status;

        if (res->status != 200) {
            result.error = "HTTP " + std::to_string(res->status);
            // Try to extract error message from response body
            auto err_msg = json_util::extract_string(res->body, "error");
            if (!err_msg.empty()) {
                result.error += ": " + err_msg;
            }
            return result;
        }

        // Parse JSON response
        result.success = json_util::extract_bool(res->body, "success");
        result.expression = json_util::extract_string(res->body, "expression");
        result.result = json_util::extract_int(res->body, "result");
        result.equal_symbol = json_util::extract_int(res->body, "equalSymbol");
        result.op = json_util::extract_int(res->body, "operator");
        result.digit1 = json_util::extract_int(res->body, "digit1");
        result.digit2 = json_util::extract_int(res->body, "digit2");
        result.error = json_util::extract_string(res->body, "error");

    } catch (const std::exception& e) {
        result.request_ok = false;
        result.error = std::string("Exception: ") + e.what();
    }

    return result;
}

static RemoteOcrResult call_remote_ocr_file(
    const std::string& host,
    int port,
    const std::string& file_path,
    int timeout_sec = 30
) {
    // Read file into memory, then send as base64
    FILE* f = fopen(file_path.c_str(), "rb");
    if (!f) {
        RemoteOcrResult result;
        result.request_ok = false;
        result.error = "Cannot open file: " + file_path;
        return result;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> data(size);
    if (size > 0) {
        auto read_bytes = fread(data.data(), 1, static_cast<size_t>(size), f);
        (void)read_bytes;
    }
    fclose(f);

    return call_remote_ocr(host, port, data, timeout_sec);
}

// ===========================================================================
// Local OCR processing
// ===========================================================================

static void process_image_local(
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

// ===========================================================================
// Remote OCR processing (server mode)
// ===========================================================================

static void process_image_remote(
    const std::string& host,
    int port,
    const std::string& path,
    bool json_output
) {
    auto result = call_remote_ocr_file(host, port, path);

    if (json_output) {
        printf("{\"file\":\"%s\",\"result\":%s}\n",
               json_escape(path).c_str(),
               remote_result_to_json(result).c_str());
    } else {
        if (result.request_ok && result.success) {
            printf("[%s] %s  =>  %d  (remote)\n",
                   path.c_str(), result.expression.c_str(), result.result);
        } else {
            fprintf(stderr, "[%s] ERROR: %s  (remote)\n",
                    path.c_str(), result.error.c_str());
        }
    }
}

// ===========================================================================
// Compare mode: local vs remote
// ===========================================================================

struct CompareEntry {
    std::string file_path;
    shmtu::cas_ocr::PredictResult local_result;
    RemoteOcrResult remote_result;
    bool local_ok = false;
    bool remote_ok = false;
};

static void print_compare_header() {
    printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
           "File", "Local", "Remote", "L-Res", "R-Res", "Match");
    printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
           std::string(40, '-').c_str(),
           std::string(8, '-').c_str(),
           std::string(8, '-').c_str(),
           std::string(6, '-').c_str(),
           std::string(6, '-').c_str(),
           std::string(5, '-').c_str());
}

static void print_compare_entry(const CompareEntry& entry) {
    std::string local_expr = entry.local_ok ? entry.local_result.expression : "FAILED";
    std::string remote_expr = entry.remote_ok ? entry.remote_result.expression : "FAILED";
    std::string local_res = entry.local_ok ? std::to_string(entry.local_result.result) : "-";
    std::string remote_res = entry.remote_ok ? std::to_string(entry.remote_result.result) : "-";

    bool match = false;
    if (entry.local_ok && entry.remote_ok) {
        match = (entry.local_result.expression == entry.remote_result.expression &&
                 entry.local_result.result == entry.remote_result.result);
    }

    // Truncate file path for display
    std::string display_path = entry.file_path;
    if (display_path.size() > 38) {
        display_path = "..." + display_path.substr(display_path.size() - 35);
    }

    printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
           display_path.c_str(),
           local_expr.c_str(),
           remote_expr.c_str(),
           local_res.c_str(),
           remote_res.c_str(),
           match ? "OK" : "DIFF");
}

static void print_compare_summary(const std::vector<CompareEntry>& entries) {
    int total = static_cast<int>(entries.size());
    int both_ok = 0;
    int match = 0;
    int local_only = 0;
    int remote_only = 0;
    int both_fail = 0;

    for (const auto& e : entries) {
        if (e.local_ok && e.remote_ok) {
            both_ok++;
            if (e.local_result.expression == e.remote_result.expression &&
                e.local_result.result == e.remote_result.result) {
                match++;
            }
        } else if (e.local_ok && !e.remote_ok) {
            local_only++;
        } else if (!e.local_ok && e.remote_ok) {
            remote_only++;
        } else {
            both_fail++;
        }
    }

    printf("\n");
    printf("=== Comparison Summary ===\n");
    printf("Total images:     %d\n", total);
    printf("Both succeeded:   %d\n", both_ok);
    printf("  Matching:       %d\n", match);
    printf("  Differing:      %d\n", both_ok - match);
    printf("Local only OK:    %d\n", local_only);
    printf("Remote only OK:   %d\n", remote_only);
    printf("Both failed:      %d\n", both_fail);

    if (both_ok > 0) {
        printf("Consistency rate: %.1f%% (%d/%d)\n",
               100.0 * match / both_ok, match, both_ok);
    }
}

static void process_image_compare(
    shmtu::cas_ocr::CasOcr& ocr,
    const std::string& host,
    int port,
    const std::string& path,
    bool json_output,
    std::vector<CompareEntry>& entries
) {
    CompareEntry entry;
    entry.file_path = path;

    // Local OCR
    try {
        entry.local_result = ocr.predict(path);
        entry.local_ok = entry.local_result.success;
    } catch (const std::exception& e) {
        entry.local_ok = false;
        entry.local_result.error = e.what();
    }

    // Remote OCR
    entry.remote_result = call_remote_ocr_file(host, port, path);
    entry.remote_ok = entry.remote_result.success;

    if (json_output) {
        printf("{\"file\":\"%s\","
               "\"local\":%s,"
               "\"remote\":%s,"
               "\"match\":%s}\n",
               json_escape(path).c_str(),
               predict_result_to_json(entry.local_result).c_str(),
               remote_result_to_json(entry.remote_result).c_str(),
               (entry.local_ok && entry.remote_ok &&
                entry.local_result.expression == entry.remote_result.expression &&
                entry.local_result.result == entry.remote_result.result)
                   ? "true" : "false");
    } else {
        print_compare_entry(entry);
    }

    entries.push_back(std::move(entry));
}

// ===========================================================================
// Image file filter
// ===========================================================================

static bool is_image_file(const fs::path& path) {
    auto ext = path.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".tif" || ext == ".tiff";
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char* argv[]) {
    print_banner();

    auto config = parse_args(argc, argv);

    if (config.input_path.empty()) {
        fprintf(stderr, "Error: No input path specified.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    // Determine which engines we need
    const bool need_local = !config.server_mode || config.compare_mode;
    const bool need_remote = config.server_mode || config.compare_mode;

    // Initialize local OCR engine if needed
    std::unique_ptr<shmtu::cas_ocr::CasOcr> ocr;
    if (need_local) {
        ocr = std::make_unique<shmtu::cas_ocr::CasOcr>(config.model_dir, config.use_gpu);

        printf("Loading models from: %s (precision=%s)...\n",
               config.model_dir.c_str(), config.precision.c_str());

        if (!ocr->load_model(config.precision)) {
            fprintf(stderr, "Failed to load models.\n");
            return 1;
        }

        printf("Model loaded (%s).\n\n",
               ocr->model_status() == shmtu::cas_ocr::ModelStatus::LoadedGPU ? "GPU" : "CPU");
    }

    // Verify remote server connectivity if needed
    if (need_remote) {
        printf("Remote server: %s:%d\n", config.server_host.c_str(), config.server_port);

        try {
            httplib::Client cli(config.server_host, config.server_port);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(5);
            auto res = cli.Get("/api/health");
            if (res && res->status == 200) {
                printf("Server health: OK\n\n");
            } else {
                fprintf(stderr, "Warning: Server health check failed");
                if (res) {
                    fprintf(stderr, " (HTTP %d)", res->status);
                } else {
                    fprintf(stderr, " (connection error %d)", static_cast<int>(res.error()));
                }
                fprintf(stderr, "\nProceeding anyway...\n\n");
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "Warning: Cannot connect to server: %s\nProceeding anyway...\n\n", e.what());
        }
    }

    fs::path input(config.input_path);

    // Collect image file paths
    std::vector<std::string> image_paths;
    if (fs::is_directory(input)) {
        for (const auto& entry : fs::directory_iterator(input)) {
            if (entry.is_regular_file() && is_image_file(entry.path())) {
                image_paths.push_back(entry.path().string());
            }
        }
        std::sort(image_paths.begin(), image_paths.end());
    } else if (fs::exists(input)) {
        image_paths.push_back(config.input_path);
    } else {
        fprintf(stderr, "Error: Path does not exist: %s\n", config.input_path.c_str());
        return 1;
    }

    if (image_paths.empty()) {
        fprintf(stderr, "No image files found.\n");
        return 1;
    }

    // ---- Server mode only (no local OCR) ----
    if (config.server_mode && !config.compare_mode) {
        if (config.json_output) printf("[\n");
        for (size_t i = 0; i < image_paths.size(); ++i) {
            if (config.json_output && i > 0) printf(",\n");
            process_image_remote(config.server_host, config.server_port,
                                 image_paths[i], config.json_output);
        }
        if (config.json_output) printf("\n]\n");
        if (!config.json_output) printf("\nProcessed %zu image(s) via remote server.\n", image_paths.size());
        return 0;
    }

    // ---- Compare mode ----
    if (config.compare_mode) {
        printf("Compare mode: local OCR vs remote server\n\n");
        print_compare_header();

        std::vector<CompareEntry> entries;
        for (const auto& path : image_paths) {
            process_image_compare(*ocr, config.server_host, config.server_port,
                                  path, config.json_output, entries);
        }

        print_compare_summary(entries);
        return 0;
    }

    // ---- Local mode (default) ----
    if (config.json_output) {
        printf("[\n");
    }

    for (size_t i = 0; i < image_paths.size(); ++i) {
        if (config.json_output && i > 0) printf(",\n");
        process_image_local(*ocr, image_paths[i], config.json_output);
    }

    if (config.json_output) {
        printf("\n]\n");
    } else {
        printf("\nProcessed %zu image(s).\n", image_paths.size());
    }

    return 0;
}
