// SHMTU CAS OCR Server — RESTful API + Legacy TCP
// Refactored: C++20, PIMPL, worker pool, graceful shutdown via jthread/stop_token

#include <shmtu/cas_ocr/server.h>

#include <cstdio>
#include <csignal>
#include <string>

#ifndef SHMTU_CAS_SERVER_VERSION
#define SHMTU_CAS_SERVER_VERSION "2.0.0"
#endif

static shmtu::cas_ocr::OcrServer* g_server = nullptr;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nReceived signal %d. Shutting down gracefully...\n", sig);
        if (g_server) {
            g_server->stop();
        }
    }
}

static void print_banner() {
    printf("ShangHai Maritime University\n");
    printf("  SHMTU CAS OCR Server V%s\n", SHMTU_CAS_SERVER_VERSION);
    printf("  Author: Haomin Kong\n");
    printf("  C++20 | RESTful API + TCP\n");
    printf("\n");
}

// Minimal command-line argument parser.
// Supports: --http-port, --tcp-port, --model-dir, --precision, --workers,
//           --use-gpu, --queue-capacity, --help
static shmtu::cas_ocr::ServerConfig parse_args(int argc, char* argv[]) {
    shmtu::cas_ocr::ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printf("Usage: shmtu-cas-ocr-server [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  --http-port <port>     HTTP port (default: %d)\n", config.http_port);
            printf("  --tcp-port <port>      TCP port (default: %d)\n", config.tcp_port);
            printf("  --model-dir <path>     Model directory (default: %s)\n", config.model_dir.c_str());
            printf("  --precision <fp16|fp32> Model precision (default: %s)\n", config.precision.c_str());
            printf("  --workers <n>          Number of OCR workers (default: %d)\n", config.worker_count);
            printf("  --use-gpu              Enable GPU acceleration\n");
            printf("  --queue-capacity <n>   Max pending requests (default: %d)\n", config.queue_capacity);
            printf("  --help, -h             Show this help\n");
            exit(0);
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
            fprintf(stderr, "Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            exit(1);
        }
    }

    return config;
}

int main(int argc, char* argv[]) {
    print_banner();

    // Parse command-line arguments
    auto config = parse_args(argc, argv);

    printf("Configuration:\n");
    printf("  HTTP port:      %d\n", config.http_port);
    printf("  TCP port:       %d\n", config.tcp_port);
    printf("  Model dir:      %s\n", config.model_dir.c_str());
    printf("  Precision:      %s\n", config.precision.c_str());
    printf("  Workers:        %d\n", config.worker_count);
    printf("  Use GPU:        %s\n", config.use_gpu ? "true" : "false");
    printf("  Queue capacity: %d\n", config.queue_capacity);

#ifdef NCNN_SUPPORT_VULKAN
    if (config.use_gpu) {
        int gpu_count = shmtu::cas_ocr::CasOcr::gpu_count();
        if (gpu_count == 0) {
            fprintf(stderr, "WARNING: GPU requested but no Vulkan devices found. Falling back to CPU.\n");
            config.use_gpu = false;
        } else {
            printf("\nGPU Devices (%d):\n", gpu_count);
            auto gpus = shmtu::cas_ocr::CasOcr::all_gpu_info();
            for (const auto& gpu : gpus) {
                printf("  [%d] %s (API %u, %u MB)\n",
                       gpu.device_index, gpu.device_name.c_str(),
                       gpu.api_version, gpu.device_memory);
            }
        }
    }
#endif

    printf("\n");

    // Create server and register signal handlers
    shmtu::cas_ocr::OcrServer server(config);
    g_server = &server;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Run blocks until shutdown
    int ret = server.run();

    g_server = nullptr;
    return ret;
}
