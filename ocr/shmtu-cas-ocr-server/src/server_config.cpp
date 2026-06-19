#include "server_config.h"

#include "logging.h"

#include <shmtu/cas_ocr/cas_ocr.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace shmtu::cas::ocr {

namespace {

template <typename Int>
std::expected<Int, std::string> parse_integer_arg(std::string_view value, std::string_view name) {
    Int parsed{};
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        return std::unexpected("Invalid value for " + std::string(name) + ": " + std::string(value));
    }
    return parsed;
}

std::expected<bool, std::string> parse_bool_value(std::string_view value, std::string_view name) {
    if (value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON" ||
        value == "yes" || value == "YES") {
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF" ||
        value == "no" || value == "NO") {
        return false;
    }
    return std::unexpected("Invalid value for " + std::string(name) + ": " + std::string(value));
}

std::expected<void, std::string> apply_env_overrides(
    ServerConfig& config,
    std::vector<std::string>& override_messages) {
    auto record_override = [&](const std::string& message) {
        override_messages.push_back("Environment override: " + message);
    };
    auto read_int = [&](const char* key, int& target, std::string_view name) -> std::expected<void, std::string> {
        if (const char* value = std::getenv(key)) {
            auto parsed = parse_integer_arg<int>(value, name);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            target = *parsed;
            record_override(std::string(key) + "=" + std::to_string(target));
        }
        return {};
    };

    if (const char* value = std::getenv("SHMTU_HTTP_HOST")) {
        config.http_host = value;
        record_override("SHMTU_HTTP_HOST=" + config.http_host);
    }
    if (const char* value = std::getenv("SHMTU_TCP_HOST")) {
        config.tcp_host = value;
        record_override("SHMTU_TCP_HOST=" + config.tcp_host);
    }
    if (const char* value = std::getenv("SHMTU_MODEL_DIR")) {
        config.model_dir = value;
        record_override("SHMTU_MODEL_DIR=" + config.model_dir);
    }
    if (const char* value = std::getenv("SHMTU_PRECISION")) {
        config.precision = value;
        record_override("SHMTU_PRECISION=" + config.precision);
    }
    if (const char* value = std::getenv("SHMTU_MODEL_VERSION")) {
        config.model_version = model_version_from_string(value);
        record_override(std::string("SHMTU_MODEL_VERSION=") +
                        model_version_to_string(config.model_version));
    }
    if (const char* value = std::getenv("SHMTU_MODEL_SOURCE")) {
        std::string src = value;
        if (src == "gitee" || src == "github") {
            config.model_source = src;
            record_override("SHMTU_MODEL_SOURCE=" + config.model_source);
        } else {
            return std::unexpected(
                "Invalid value for SHMTU_MODEL_SOURCE: " + src +
                " (expected \"gitee\" or \"github\")");
        }
    }
    if (const char* value = std::getenv("SHMTU_SERVER_NAME")) {
        config.server_name = value;
        record_override("SHMTU_SERVER_NAME=" + config.server_name);
    }
    if (const char* value = std::getenv("SHMTU_LOG_DIR")) {
        config.log_dir = value;
        record_override("SHMTU_LOG_DIR=" + config.log_dir);
    }
    if (const char* value = std::getenv("SHMTU_LOG_FILE_PREFIX")) {
        config.log_file_prefix = value;
        record_override("SHMTU_LOG_FILE_PREFIX=" + config.log_file_prefix);
    }
    if (const char* value = std::getenv("SHMTU_USE_GPU")) {
        auto parsed = parse_bool_value(value, "SHMTU_USE_GPU");
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        config.use_gpu = *parsed;
        record_override(std::string("SHMTU_USE_GPU=") + (config.use_gpu ? "true" : "false"));
    }
    if (const char* value = std::getenv("SHMTU_LOG_TO_STDERR")) {
        auto parsed = parse_bool_value(value, "SHMTU_LOG_TO_STDERR");
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        config.log_to_stderr = *parsed;
        record_override(std::string("SHMTU_LOG_TO_STDERR=") + (config.log_to_stderr ? "true" : "false"));
    }
    if (const char* value = std::getenv("SHMTU_LOG_ALSO_TO_STDERR")) {
        auto parsed = parse_bool_value(value, "SHMTU_LOG_ALSO_TO_STDERR");
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        config.also_log_to_stderr = *parsed;
        record_override(std::string("SHMTU_LOG_ALSO_TO_STDERR=") +
                        (config.also_log_to_stderr ? "true" : "false"));
    }

    if (auto result = read_int("SHMTU_HTTP_PORT", config.http_port, "SHMTU_HTTP_PORT"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_TCP_PORT", config.tcp_port, "SHMTU_TCP_PORT"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_WORKERS", config.worker_count, "SHMTU_WORKERS"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_QUEUE_CAPACITY", config.queue_capacity, "SHMTU_QUEUE_CAPACITY"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_NCNN_THREADS", config.inference_threads, "SHMTU_NCNN_THREADS"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_LOG_MIN_LEVEL", config.log_min_level, "SHMTU_LOG_MIN_LEVEL"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_LOG_MAX_SIZE_MB", config.log_max_size_mb, "SHMTU_LOG_MAX_SIZE_MB"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_LOG_CLEANUP_INTERVAL_SECS", config.log_cleanup_interval_secs, "SHMTU_LOG_CLEANUP_INTERVAL_SECS"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_LOG_RETENTION_DAYS", config.log_retention_days, "SHMTU_LOG_RETENTION_DAYS"); !result) {
        return result;
    }

    return {};
}

void resolve_auto_tuning(ServerConfig& config) {
    const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());

    if (config.inference_threads <= 0) {
        config.inference_threads = config.use_gpu
            ? 1
            : static_cast<int>(std::min(hardware_threads, 4u));
    }

    if (config.worker_count <= 0) {
        if (config.use_gpu) {
            config.worker_count = static_cast<int>(std::min(hardware_threads, 2u));
        } else {
            const auto auto_workers = std::max(
                1u,
                std::min(hardware_threads / static_cast<unsigned>(std::max(1, config.inference_threads)), 8u));
            config.worker_count = static_cast<int>(auto_workers);
        }
    }

    if (config.queue_capacity <= 0) {
        config.queue_capacity = std::max(16, config.worker_count * 4);
    }
}

}  // namespace

std::expected<ServerConfig, std::string> parse_server_config(
    int argc,
    char* argv[],
    std::vector<std::string>& override_messages) {
    ServerConfig config;
    if (auto env_result = apply_env_overrides(config, override_messages); !env_result) {
        return std::unexpected(env_result.error());
    }

    constexpr int kIntMax = std::numeric_limits<int>::max();
    bool enable_gpu = false;
    bool disable_gpu = false;
    bool enable_log_to_stderr = false;
    bool disable_log_to_stderr = false;
    bool enable_also_log_to_stderr = false;
    bool disable_also_log_to_stderr = false;

    CLI::App app{"SHMTU CAS OCR Server"};
    app.set_help_flag("-h,--help", "Show this help");
    app.footer(
        "Precedence:\n"
        "  defaults -> environment variables -> CLI options\n"
        "Environment:\n"
        "  SHMTU_HTTP_HOST SHMTU_HTTP_PORT SHMTU_TCP_HOST SHMTU_TCP_PORT\n"
        "  SHMTU_MODEL_DIR SHMTU_PRECISION SHMTU_MODEL_VERSION\n"
        "  SHMTU_MODEL_SOURCE SHMTU_USE_GPU SHMTU_SERVER_NAME\n"
        "  SHMTU_WORKERS SHMTU_QUEUE_CAPACITY SHMTU_NCNN_THREADS\n"
        "  SHMTU_LOG_DIR SHMTU_LOG_FILE_PREFIX SHMTU_LOG_MIN_LEVEL\n"
        "  SHMTU_LOG_TO_STDERR SHMTU_LOG_ALSO_TO_STDERR SHMTU_LOG_MAX_SIZE_MB\n"
        "  SHMTU_LOG_CLEANUP_INTERVAL_SECS SHMTU_LOG_RETENTION_DAYS\n");

    app.add_option("--http-host", config.http_host, "HTTP bind host")->capture_default_str();
    app.add_option("--http-port", config.http_port, "HTTP port")
        ->capture_default_str()
        ->check(CLI::Range(1, 65535));
    app.add_option("--tcp-host", config.tcp_host, "TCP bind host")->capture_default_str();
    app.add_option("--tcp-port", config.tcp_port, "TCP port")
        ->capture_default_str()
        ->check(CLI::Range(1, 65535));
    app.add_option("--model-dir", config.model_dir, "Model directory")->capture_default_str();
    app.add_option("--precision", config.precision, "Model precision")
        ->capture_default_str()
        ->check(CLI::IsMember(std::set<std::string>{"fp16", "fp32"}));
    std::string model_version_str = "v2";
    app.add_option("--model-version", model_version_str,
                   "Model version: v1 (3-model) or v2 (TriSlot, default)")
        ->capture_default_str()
        ->check(CLI::IsMember(std::set<std::string>{"v1", "v2", "V1", "V2", "1", "2"}));
    app.add_option("--workers", config.worker_count, "Number of OCR workers (0 = auto)")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));
    app.add_option("--ncnn-threads", config.inference_threads, "NCNN CPU threads per worker (0 = auto)")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));
    app.add_flag("--use-gpu", enable_gpu, "Enable GPU acceleration");
    app.add_flag("--no-use-gpu", disable_gpu, "Disable GPU acceleration");
    app.add_option("--queue-capacity", config.queue_capacity, "Max pending requests (0 = auto)")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));
    app.add_option("--model-source", config.model_source,
                   "Model download source: gitee (default) or github")
        ->capture_default_str()
        ->check(CLI::IsMember(std::set<std::string>{"gitee", "github"}));
    app.add_option("--server-name", config.server_name, "Logical server name")->capture_default_str();
    app.add_option("--log-dir", config.log_dir, "Log directory")->capture_default_str();
    app.add_option("--log-file-prefix", config.log_file_prefix, "Log file prefix")->capture_default_str();
    app.add_option("--log-min-level", config.log_min_level, "glog minimum log level")
        ->capture_default_str()
        ->check(CLI::Range(0, 3));
    app.add_flag("--log-to-stderr", enable_log_to_stderr, "Write logs to stderr");
    app.add_flag("--no-log-to-stderr", disable_log_to_stderr, "Disable direct stderr logging");
    app.add_flag("--also-log-to-stderr", enable_also_log_to_stderr, "Mirror logs to stderr");
    app.add_flag("--no-also-log-to-stderr", disable_also_log_to_stderr, "Disable stderr mirroring");
    app.add_option("--log-max-size-mb", config.log_max_size_mb, "Maximum log file size in MB")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));
    app.add_option("--log-cleanup-interval-secs", config.log_cleanup_interval_secs,
                   "Log cleanup interval in seconds (0 disables periodic cleanup)")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));
    app.add_option("--log-retention-days", config.log_retention_days,
                   "Log retention days (0 disables cleanup deletion)")
        ->capture_default_str()
        ->check(CLI::Range(0, kIntMax));

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        std::printf("%s", app.help().c_str());
        std::exit(0);
    } catch (const CLI::ParseError& error) {
        return std::unexpected(error.what());
    }

    if (enable_gpu && disable_gpu) {
        return std::unexpected("Conflicting options: --use-gpu and --no-use-gpu");
    }
    if (enable_log_to_stderr && disable_log_to_stderr) {
        return std::unexpected("Conflicting options: --log-to-stderr and --no-log-to-stderr");
    }
    if (enable_also_log_to_stderr && disable_also_log_to_stderr) {
        return std::unexpected(
            "Conflicting options: --also-log-to-stderr and --no-also-log-to-stderr");
    }

    if (enable_gpu) {
        config.use_gpu = true;
    } else if (disable_gpu) {
        config.use_gpu = false;
    }
    if (enable_log_to_stderr) {
        config.log_to_stderr = true;
    } else if (disable_log_to_stderr) {
        config.log_to_stderr = false;
    }
    if (enable_also_log_to_stderr) {
        config.also_log_to_stderr = true;
    } else if (disable_also_log_to_stderr) {
        config.also_log_to_stderr = false;
    }

    if (config.precision != "fp16" && config.precision != "fp32") {
        return std::unexpected("Unsupported precision: " + config.precision);
    }
    config.model_version = model_version_from_string(model_version_str);
    if (config.http_port <= 0 || config.tcp_port <= 0) {
        return std::unexpected("Ports must be positive integers");
    }
    if (config.worker_count < 0) {
        return std::unexpected("Worker count must be zero or positive");
    }
    if (config.queue_capacity < 0) {
        return std::unexpected("Queue capacity must be zero or positive");
    }
    if (config.inference_threads < 0) {
        return std::unexpected("NCNN threads must be zero or positive");
    }

    resolve_auto_tuning(config);
    return config;
}

void print_runtime_configuration(const ServerConfig& config) {
    std::printf("Configuration:\n");
    std::printf("  HTTP host:      %s\n", config.http_host.c_str());
    std::printf("  HTTP port:      %d\n", config.http_port);
    std::printf("  TCP host:       %s\n", config.tcp_host.c_str());
    std::printf("  TCP port:       %d\n", config.tcp_port);
    std::printf("  Model dir:      %s\n", config.model_dir.c_str());
    std::printf("  Precision:      %s\n", config.precision.c_str());
    std::printf("  Model version:  %s\n", model_version_to_string(config.model_version).c_str());
    std::printf("  Workers:        %d\n", config.worker_count);
    std::printf("  NCNN threads:   %d\n", config.inference_threads);
    std::printf("  Use GPU:        %s\n", config.use_gpu ? "true" : "false");
    std::printf("  Queue capacity: %d\n", config.queue_capacity);
    std::printf("  Model source:   %s\n", config.model_source.c_str());
    std::printf("  Log dir:        %s\n", config.log_dir.c_str());
    std::printf("  Log prefix:     %s\n", config.log_file_prefix.c_str());
    std::printf("  Log level:      %d\n", config.log_min_level);
}

void inspect_gpu_runtime(ServerConfig& config) {
#ifdef NCNN_SUPPORT_VULKAN
    if (!config.use_gpu) {
        return;
    }

    const int gpu_count = CasOcr::gpu_count();
    if (gpu_count == 0) {
        std::fprintf(stderr, "WARNING: GPU requested but no Vulkan devices found. Falling back to CPU.\n");
        LOG(WARNING) << "GPU requested but no Vulkan devices found, falling back to CPU";
        config.use_gpu = false;
        return;
    }

    std::printf("\nGPU Devices (%d):\n", gpu_count);
    auto gpus = CasOcr::all_gpu_info();
    for (const auto& gpu : gpus) {
        std::printf("  [%d] %s (API %u, %u MB)\n",
                    gpu.device_index, gpu.device_name.c_str(),
                    gpu.api_version, gpu.device_memory);
        LOG(INFO) << "GPU detected"
                  << " device_index=" << gpu.device_index
                  << " device_name=" << gpu.device_name
                  << " api_version=" << gpu.api_version
                  << " device_memory_mb=" << gpu.device_memory;
    }
#else
    if (config.use_gpu) {
        LOG(WARNING) << "GPU requested but this build does not include Vulkan support";
    }
#endif
}

}  // namespace shmtu::cas::ocr
