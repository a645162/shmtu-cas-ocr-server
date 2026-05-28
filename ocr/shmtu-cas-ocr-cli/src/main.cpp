// SHMTU CAS OCR CLI — local/remote/compare modes with C++23-style plumbing

#include <shmtu/cas_ocr/cas_ocr.h>

#include <httplib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

#ifndef SHMTU_CAS_CLI_VERSION
#define SHMTU_CAS_CLI_VERSION "2.2.0"
#endif

namespace {

using ByteBuffer = std::vector<uint8_t>;

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
    bool request_ok = false;
};

struct CliConfig {
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
    bool json_output = false;
    std::string input_path;
    std::string server_host;
    int server_port = 21600;
    bool server_mode = false;
    bool compare_mode = false;
};

struct CompareEntry {
    std::string file_path;
    shmtu::cas::ocr::PredictResult local_result;
    RemoteOcrResult remote_result;
    bool local_ok = false;
    bool remote_ok = false;
};

template <typename Int>
std::expected<Int, std::string> parse_integer(std::string_view value, std::string_view name) {
    Int parsed{};
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        return std::unexpected("Invalid value for " + std::string(name) + ": " + std::string(value));
    }
    return parsed;
}

std::expected<std::pair<std::string, int>, std::string> parse_server_endpoint(
    std::string_view endpoint) {
    const auto colon_pos = endpoint.rfind(':');
    if (colon_pos == std::string_view::npos) {
        return std::pair<std::string, int>{std::string(endpoint), 21600};
    }

    auto port = parse_integer<int>(endpoint.substr(colon_pos + 1), "server port");
    if (!port || *port <= 0) {
        return std::unexpected(port ? "Server port must be positive" : port.error());
    }

    return std::pair<std::string, int>{std::string(endpoint.substr(0, colon_pos)), *port};
}

void print_banner() {
    std::printf("SHMTU CAS OCR CLI V%s\n", SHMTU_CAS_CLI_VERSION);
}

void print_usage(const char* prog) {
    std::printf("Usage: %s [OPTIONS] <image_path_or_directory>\n\n", prog);
    std::printf("Options:\n");
    std::printf("  --model-dir <path>       Model directory (default: ./models)\n");
    std::printf("  --precision <fp16|fp32>  Model precision (default: fp16)\n");
    std::printf("  --use-gpu                Enable GPU acceleration\n");
    std::printf("  --json                   Output results as JSON\n");
    std::printf("  --server <host:port>     Use remote OCR server instead of local model\n");
    std::printf("  --compare                Compare local OCR vs remote server results\n");
    std::printf("  --help, -h               Show this help\n\n");
    std::printf("Examples:\n");
    std::printf("  %s captcha.png\n", prog);
    std::printf("  %s --json ./captcha_images/\n", prog);
    std::printf("  %s --server 127.0.0.1:21600 captcha.png\n", prog);
    std::printf("  %s --server 127.0.0.1:21600 --compare ./captcha_images/\n", prog);
}

std::expected<CliConfig, std::string> parse_args(int argc, char* argv[]) {
    CliConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }

        if (arg == "--model-dir" && i + 1 < argc) {
            config.model_dir = argv[++i];
            continue;
        }
        if (arg == "--precision" && i + 1 < argc) {
            config.precision = argv[++i];
            continue;
        }
        if (arg == "--use-gpu") {
            config.use_gpu = true;
            continue;
        }
        if (arg == "--json") {
            config.json_output = true;
            continue;
        }
        if (arg == "--server" && i + 1 < argc) {
            auto endpoint = parse_server_endpoint(argv[++i]);
            if (!endpoint) {
                return std::unexpected(endpoint.error());
            }
            auto& [host, port] = *endpoint;
            config.server_host = std::move(host);
            config.server_port = port;
            config.server_mode = true;
            continue;
        }
        if (arg == "--compare") {
            config.compare_mode = true;
            continue;
        }
        if (!arg.empty() && arg.front() != '-') {
            config.input_path = std::string(arg);
            continue;
        }

        return std::unexpected("Unknown argument: " + std::string(arg));
    }

    if (config.precision != "fp16" && config.precision != "fp32") {
        return std::unexpected("Unsupported precision: " + config.precision);
    }
    if (config.compare_mode && !config.server_mode) {
        return std::unexpected("--compare requires --server");
    }
    if (config.input_path.empty()) {
        return std::unexpected("No input path specified");
    }

    return config;
}

namespace json_util {

std::string extract_string(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return {};
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return {};
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return {};
    }

    std::string result;
    for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(json[pos]); break;
            }
        } else {
            result.push_back(json[pos]);
        }
    }

    return result;
}

int extract_int(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return 0;
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return 0;
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    std::string value;
    while (pos < json.size() &&
           (json[pos] == '-' || std::isdigit(static_cast<unsigned char>(json[pos])))) {
        value.push_back(json[pos++]);
    }

    if (value.empty()) {
        return 0;
    }

    return std::atoi(value.c_str());
}

bool extract_bool(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return false;
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return false;
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    return json.substr(pos, 4) == "true";
}

}  // namespace json_util

std::string json_escape(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const auto ch : input) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string predict_result_to_json(const shmtu::cas::ocr::PredictResult& result) {
    std::string json = "{";
    json += "\"success\":" + std::string(result.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(result.expression) + "\",";
    json += "\"result\":" + std::to_string(result.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(result.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(result.op) + ",";
    json += "\"digit1\":" + std::to_string(result.digit1) + ",";
    json += "\"digit2\":" + std::to_string(result.digit2);
    if (!result.error.empty()) {
        json += ",\"error\":\"" + json_escape(result.error) + "\"";
    }
    json += "}";
    return json;
}

std::string remote_result_to_json(const RemoteOcrResult& result) {
    std::string json = "{";
    json += "\"success\":" + std::string(result.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(result.expression) + "\",";
    json += "\"result\":" + std::to_string(result.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(result.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(result.op) + ",";
    json += "\"digit1\":" + std::to_string(result.digit1) + ",";
    json += "\"digit2\":" + std::to_string(result.digit2);
    if (!result.error.empty()) {
        json += ",\"error\":\"" + json_escape(result.error) + "\"";
    }
    if (!result.request_ok) {
        json += ",\"httpError\":\"request failed\"";
    } else if (result.http_status != 200) {
        json += ",\"httpStatus\":" + std::to_string(result.http_status);
    }
    json += "}";
    return json;
}

constexpr std::string_view kBase64Table =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(std::span<const uint8_t> data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += kBase64Table[(n >> 6) & 0x3F];
        result += kBase64Table[n & 0x3F];
    }

    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? kBase64Table[(n >> 6) & 0x3F] : '=';
        result += '=';
    }

    return result;
}

std::expected<ByteBuffer, std::string> read_binary_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected("Cannot open file: " + path.string());
    }

    const auto file_size = input.tellg();
    if (file_size < 0) {
        return std::unexpected("Cannot determine file size: " + path.string());
    }

    ByteBuffer buffer(static_cast<size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!buffer.empty()) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
    }

    if (!input.good() && !input.eof()) {
        return std::unexpected("Failed to read file: " + path.string());
    }

    return buffer;
}

RemoteOcrResult call_remote_ocr(const std::string& host,
                                const int port,
                                std::span<const uint8_t> image_bytes,
                                const int timeout_sec = 30) {
    RemoteOcrResult result;

    try {
        httplib::Client client(host, port);
        client.set_connection_timeout(timeout_sec);
        client.set_read_timeout(timeout_sec);

        const std::string request_body =
            "{\"imageBase64\":\"" + base64_encode(image_bytes) + "\"}";

        auto response = client.Post("/api/ocr", request_body, "application/json");
        if (!response) {
            result.request_ok = false;
            result.error =
                "HTTP request failed: " + std::to_string(static_cast<int>(response.error()));
            return result;
        }

        result.request_ok = true;
        result.http_status = response->status;
        if (response->status != 200) {
            result.error = "HTTP " + std::to_string(response->status);
            if (const auto error_message = json_util::extract_string(response->body, "error");
                !error_message.empty()) {
                result.error += ": " + error_message;
            }
            return result;
        }

        const std::string_view body = response->body;
        result.success = json_util::extract_bool(body, "success");
        result.expression = json_util::extract_string(body, "expression");
        result.result = json_util::extract_int(body, "result");
        result.equal_symbol = json_util::extract_int(body, "equalSymbol");
        result.op = json_util::extract_int(body, "operator");
        result.digit1 = json_util::extract_int(body, "digit1");
        result.digit2 = json_util::extract_int(body, "digit2");
        result.error = json_util::extract_string(body, "error");
    } catch (const std::exception& exception) {
        result.request_ok = false;
        result.error = std::string("Exception: ") + exception.what();
    }

    return result;
}

RemoteOcrResult call_remote_ocr_file(const std::string& host,
                                     const int port,
                                     const fs::path& path,
                                     const int timeout_sec = 30) {
    const auto file_data = read_binary_file(path);
    if (!file_data) {
        RemoteOcrResult result;
        result.request_ok = false;
        result.error = file_data.error();
        return result;
    }

    return call_remote_ocr(host, port, *file_data, timeout_sec);
}

std::expected<void, std::string> check_remote_server(const CliConfig& config) {
    try {
        httplib::Client client(config.server_host, config.server_port);
        client.set_connection_timeout(5);
        client.set_read_timeout(5);
        auto response = client.Get("/api/health");
        if (response && response->status == 200) {
            return {};
        }
        if (response) {
            return std::unexpected("Health check returned HTTP " + std::to_string(response->status));
        }
        return std::unexpected("Health check connection error " +
                               std::to_string(static_cast<int>(response.error())));
    } catch (const std::exception& exception) {
        return std::unexpected(exception.what());
    }
}

bool is_image_file(const fs::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".bmp" || extension == ".tif" || extension == ".tiff";
}

std::expected<std::vector<std::string>, std::string> collect_image_paths(const fs::path& input) {
    std::vector<std::string> image_paths;

    if (fs::is_directory(input)) {
        for (const auto& entry : fs::directory_iterator(input)) {
            if (entry.is_regular_file() && is_image_file(entry.path())) {
                image_paths.push_back(entry.path().string());
            }
        }
        std::ranges::sort(image_paths);
    } else if (fs::exists(input)) {
        image_paths.push_back(input.string());
    } else {
        return std::unexpected("Path does not exist: " + input.string());
    }

    if (image_paths.empty()) {
        return std::unexpected("No image files found");
    }

    return image_paths;
}

void process_image_local(shmtu::cas::ocr::CasOcr& ocr,
                         const std::string& path,
                         const bool json_output) {
    const auto result = ocr.predict(path);
    if (json_output) {
        std::printf("{\"file\":\"%s\",\"result\":%s}\n",
                    json_escape(path).c_str(),
                    predict_result_to_json(result).c_str());
        return;
    }

    if (result.success) {
        std::printf("[%s] %s  =>  %d\n", path.c_str(), result.expression.c_str(), result.result);
    } else {
        std::fprintf(stderr, "[%s] ERROR: %s\n", path.c_str(), result.error.c_str());
    }
}

void process_image_remote(const std::string& host,
                          const int port,
                          const std::string& path,
                          const bool json_output) {
    const auto result = call_remote_ocr_file(host, port, path);
    if (json_output) {
        std::printf("{\"file\":\"%s\",\"result\":%s}\n",
                    json_escape(path).c_str(),
                    remote_result_to_json(result).c_str());
        return;
    }

    if (result.request_ok && result.success) {
        std::printf("[%s] %s  =>  %d  (remote)\n",
                    path.c_str(), result.expression.c_str(), result.result);
    } else {
        std::fprintf(stderr, "[%s] ERROR: %s  (remote)\n", path.c_str(), result.error.c_str());
    }
}

void print_compare_header() {
    std::printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
                "File", "Local", "Remote", "L-Res", "R-Res", "Match");
    std::printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
                std::string(40, '-').c_str(),
                std::string(8, '-').c_str(),
                std::string(8, '-').c_str(),
                std::string(6, '-').c_str(),
                std::string(6, '-').c_str(),
                std::string(5, '-').c_str());
}

void print_compare_entry(const CompareEntry& entry) {
    const auto local_expression = entry.local_ok ? entry.local_result.expression : "FAILED";
    const auto remote_expression = entry.remote_ok ? entry.remote_result.expression : "FAILED";
    const auto local_result = entry.local_ok ? std::to_string(entry.local_result.result) : "-";
    const auto remote_result = entry.remote_ok ? std::to_string(entry.remote_result.result) : "-";
    const auto match = entry.local_ok && entry.remote_ok &&
                       entry.local_result.expression == entry.remote_result.expression &&
                       entry.local_result.result == entry.remote_result.result;

    std::string display_path = entry.file_path;
    if (display_path.size() > 38) {
        display_path = "..." + display_path.substr(display_path.size() - 35);
    }

    std::printf("%-40s  %-8s  %-8s  %-6s  %-6s  %s\n",
                display_path.c_str(),
                local_expression.c_str(),
                remote_expression.c_str(),
                local_result.c_str(),
                remote_result.c_str(),
                match ? "OK" : "DIFF");
}

void print_compare_summary(const std::vector<CompareEntry>& entries) {
    const auto both_ok = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return entry.local_ok && entry.remote_ok;
    }));
    const auto matching = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return entry.local_ok && entry.remote_ok &&
               entry.local_result.expression == entry.remote_result.expression &&
               entry.local_result.result == entry.remote_result.result;
    }));
    const auto local_only = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return entry.local_ok && !entry.remote_ok;
    }));
    const auto remote_only = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return !entry.local_ok && entry.remote_ok;
    }));
    const auto both_fail = static_cast<int>(entries.size()) - both_ok - local_only - remote_only;

    std::printf("\n=== Comparison Summary ===\n");
    std::printf("Total images:     %zu\n", entries.size());
    std::printf("Both succeeded:   %d\n", both_ok);
    std::printf("  Matching:       %d\n", matching);
    std::printf("  Differing:      %d\n", both_ok - matching);
    std::printf("Local only OK:    %d\n", local_only);
    std::printf("Remote only OK:   %d\n", remote_only);
    std::printf("Both failed:      %d\n", both_fail);
    if (both_ok > 0) {
        std::printf("Consistency rate: %.1f%% (%d/%d)\n", 100.0 * matching / both_ok, matching, both_ok);
    }
}

void process_image_compare(shmtu::cas::ocr::CasOcr& ocr,
                           const std::string& host,
                           const int port,
                           const std::string& path,
                           const bool json_output,
                           std::vector<CompareEntry>& entries) {
    CompareEntry entry;
    entry.file_path = path;

    try {
        entry.local_result = ocr.predict(path);
        entry.local_ok = entry.local_result.success;
    } catch (const std::exception& exception) {
        entry.local_result.error = exception.what();
    }

    entry.remote_result = call_remote_ocr_file(host, port, path);
    entry.remote_ok = entry.remote_result.success;

    if (json_output) {
        const auto matched = entry.local_ok && entry.remote_ok &&
                             entry.local_result.expression == entry.remote_result.expression &&
                             entry.local_result.result == entry.remote_result.result;
        std::printf("{\"file\":\"%s\",\"local\":%s,\"remote\":%s,\"match\":%s}\n",
                    json_escape(path).c_str(),
                    predict_result_to_json(entry.local_result).c_str(),
                    remote_result_to_json(entry.remote_result).c_str(),
                    matched ? "true" : "false");
    } else {
        print_compare_entry(entry);
    }

    entries.push_back(std::move(entry));
}

}  // namespace

int main(int argc, char* argv[]) {
    print_banner();

    const auto config = parse_args(argc, argv);
    if (!config) {
        std::fprintf(stderr, "Error: %s\n\n", config.error().c_str());
        print_usage(argv[0]);
        return 1;
    }

    const bool need_local = !config->server_mode || config->compare_mode;
    const bool need_remote = config->server_mode || config->compare_mode;

    std::unique_ptr<shmtu::cas::ocr::CasOcr> ocr;
    if (need_local) {
        ocr = std::make_unique<shmtu::cas::ocr::CasOcr>(config->model_dir);
        std::printf("Loading models from: %s (precision=%s, gpu=%s)...\n",
                    config->model_dir.c_str(),
                    config->precision.c_str(),
                    config->use_gpu ? "true" : "false");
        if (!ocr->load_model(config->precision, config->use_gpu)) {
            std::fprintf(stderr, "Failed to load models.\n");
            return 1;
        }
        std::printf("Model loaded (%s).\n\n",
                    ocr->model_status() == shmtu::cas::ocr::ModelStatus::LoadedGPU ? "GPU" : "CPU");
    }

    if (need_remote) {
        std::printf("Remote server: %s:%d\n", config->server_host.c_str(), config->server_port);
        if (const auto health = check_remote_server(*config); health) {
            std::printf("Server health: OK\n\n");
        } else {
            std::fprintf(stderr, "Warning: %s\nProceeding anyway...\n\n", health.error().c_str());
        }
    }

    const auto image_paths = collect_image_paths(config->input_path);
    if (!image_paths) {
        std::fprintf(stderr, "%s.\n", image_paths.error().c_str());
        return 1;
    }

    if (config->server_mode && !config->compare_mode) {
        if (config->json_output) {
            std::printf("[\n");
        }
        for (size_t i = 0; i < image_paths->size(); ++i) {
            if (config->json_output && i > 0) {
                std::printf(",\n");
            }
            process_image_remote(config->server_host, config->server_port, (*image_paths)[i],
                                 config->json_output);
        }
        if (config->json_output) {
            std::printf("\n]\n");
        } else {
            std::printf("\nProcessed %zu image(s) via remote server.\n", image_paths->size());
        }
        return 0;
    }

    if (config->compare_mode) {
        std::printf("Compare mode: local OCR vs remote server\n\n");
        print_compare_header();
        std::vector<CompareEntry> entries;
        entries.reserve(image_paths->size());
        for (const auto& path : *image_paths) {
            process_image_compare(*ocr, config->server_host, config->server_port, path,
                                  config->json_output, entries);
        }
        print_compare_summary(entries);
        return 0;
    }

    if (config->json_output) {
        std::printf("[\n");
    }
    for (size_t i = 0; i < image_paths->size(); ++i) {
        if (config->json_output && i > 0) {
            std::printf(",\n");
        }
        process_image_local(*ocr, (*image_paths)[i], config->json_output);
    }
    if (config->json_output) {
        std::printf("\n]\n");
    } else {
        std::printf("\nProcessed %zu image(s).\n", image_paths->size());
    }

    return 0;
}
