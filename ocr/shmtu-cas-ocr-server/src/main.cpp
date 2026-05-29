#include <shmtu/cas_ocr/server.h>

#include "server_config.h"
#include "logging.h"
#include "server_startup.h"

#include <cstdio>
#include <vector>

int main(int argc, char* argv[]) {
    shmtu::cas::ocr::print_banner();

    std::vector<std::string> override_messages;
    auto config = shmtu::cas::ocr::parse_server_config(argc, argv, override_messages);
    if (!config) {
        std::fprintf(stderr, "Error: %s\nUse --help for usage.\n", config.error().c_str());
        return 1;
    }

    shmtu::cas::ocr::init_server_logging(*config, argv[0]);
    LOG(INFO) << "Server process starting, argc=" << argc;
    if (override_messages.empty()) {
        LOG(INFO) << "Environment override: none";
    } else {
        for (const auto& message : override_messages) {
            LOG(INFO) << message;
        }
    }

    shmtu::cas::ocr::print_runtime_configuration(*config);
    LOG(INFO) << "Configuration resolved"
              << " http=" << config->http_host << ":" << config->http_port
              << " tcp=" << config->tcp_host << ":" << config->tcp_port
              << " model_dir=" << config->model_dir
              << " precision=" << config->precision
              << " workers=" << config->worker_count
              << " ncnn_threads=" << config->inference_threads
              << " use_gpu=" << config->use_gpu
              << " queue_capacity=" << config->queue_capacity
              << " log_dir=" << config->log_dir
              << " log_min_level=" << config->log_min_level;

    shmtu::cas::ocr::inspect_gpu_runtime(*config);
    std::printf("\n");

    shmtu::cas::ocr::OcrServer server(*config);
    shmtu::cas::ocr::install_signal_handlers(server);
    LOG(INFO) << "OCR server instance created";
    LOG(INFO) << "Signal handlers installed";

    const int ret = server.run();

    shmtu::cas::ocr::clear_signal_handler_target();
    LOG(INFO) << "Server exited with rc=" << ret;
    shmtu::cas::ocr::shutdown_server_logging();
    return ret;
}
