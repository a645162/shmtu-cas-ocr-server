// SHMTU CAS OCR Server — RESTful API + Legacy TCP
// Refactored: C++20, PIMPL, worker pool, graceful shutdown via jthread/stop_token

#include <shmtu/cas_ocr/server.h>
#include <shmtu/cas_ocr/cas_ocr.h>

#include <csignal>
#include <cstdio>
#include <string>

#ifndef SHMTU_CAS_SERVER_VERSION
#define SHMTU_CAS_SERVER_VERSION "2.0.0"
#endif

static shmtu::cas::ocr::OcrServer* g_server = nullptr;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::printf("\nReceived signal %d. Shutting down gracefully...\n", sig);
        if (g_server) {
            g_server->stop();
        }
    }
}

static void print_banner() {
    std::printf("ShangHai Maritime University\n");
    std::printf("  SHMTU CAS OCR Server V%s\n", SHMTU_CAS_SERVER_VERSION);
    std::printf("  Author: Haomin Kong\n");
    std::printf("  C++20 | RESTful API + TCP\n");
    std::printf("\n");
}

static shmtu::cas::ocr::ServerConfig parse_args(int argc, char* argv[]) {
    shmtu::cas::ocr::ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            std::printf("Usage: shmtu-cas-ocr-server [OPTIONS]\n\n");
            std::printf("Options:\n");
            std::printf("  --http-port <port>      HTTP port (default: %d)\n", config.http_port);
            std::printf("  --tcp-port <port>       TCP port (default: %d)\n", config.tcp_port);
            std::printf("  --model-dir <path>      Model directory (default: %s)\n", config.model_dir.c_str());
            std::printf("  --precision <fp16|fp32> Model precision (default: %s)\n", config.precision.c_str());
            std::printf("  --workers <n>           Number of OCR workers (default: %d)\n", config.worker_count);
            std::printf("  --use-gpu               Enable GPU acceleration\n");
            std::printf("  --queue-capacity <n>    Max pending requests (default: %d)\n", config.queue_capacity);
            std::printf("  --help, -h              Show this help\n");
            std::exit(0);
        } else if (arg == "--http-port" && i + 1 < argc) {
            config.http_port = std::stoi(argv[++i]);
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            config.tcp_port = std::stoi(argv[++i]);
        } else if (arg == "--model-dir" && i + 1 < argc) {
            config.model_dir = argv[++i];
        } else if (arg == "--precision" && i + 1 < argc) {
            config.precision = argv[++i];
        } else if (arg == "--workers" && i + 1 < argc) {
            config.worker_count = std::stoi(argv[++i]);
        } else if (arg == "--use-gpu") {
            config.use_gpu = true;
        } else if (arg == "--queue-capacity" && i + 1 < argc) {
            config.queue_capacity = std::stoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            std::exit(1);
        }
    }

    return config;
}

int main(int argc, char* argv[]) {
    print_banner();

    auto config = parse_args(argc, argv);

    std::printf("Configuration:\n");
    std::printf("  HTTP port:      %d\n", config.http_port);
    std::printf("  TCP port:       %d\n", config.tcp_port);
    std::printf("  Model dir:      %s\n", config.model_dir.c_str());
    std::printf("  Precision:      %s\n", config.precision.c_str());
    std::printf("  Workers:        %d\n", config.worker_count);
    std::printf("  Use GPU:        %s\n", config.use_gpu ? "true" : "false");
    std::printf("  Queue capacity: %d\n", config.queue_capacity);

#ifdef NCNN_SUPPORT_VULKAN
    if (config.use_gpu) {
        int gpu_count = shmtu::cas::ocr::CasOcr::gpu_count();
        if (gpu_count == 0) {
            std::fprintf(stderr, "WARNING: GPU requested but no Vulkan devices found. Falling back to CPU.\n");
            config.use_gpu = false;
        } else {
            std::printf("\nGPU Devices (%d):\n", gpu_count);
            auto gpus = shmtu::cas::ocr::CasOcr::all_gpu_info();
            for (const auto& gpu : gpus) {
                std::printf("  [%d] %s (API %u, %u MB)\n",
                            gpu.device_index, gpu.device_name.c_str(),
                            gpu.api_version, gpu.device_memory);
            }
        }
    }
#endif

    std::printf("\n");

    shmtu::cas::ocr::OcrServer server(config);
    g_server = &server;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int ret = server.run();

    g_server = nullptr;
    return ret;
}
