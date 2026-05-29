#include "server_startup.h"
#include <shmtu/cas_ocr/version.h>
#include <csignal>
#include <cstdio>

namespace shmtu::cas::ocr {

namespace {

OcrServer* g_server = nullptr;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::fprintf(stderr, "Received signal %d, shutting down gracefully\n", sig);
        if (g_server) {
            g_server->stop();
        }
    }
}

}  // namespace

void print_banner() {
    std::printf("ShangHai Maritime University\n");
    std::printf("  SHMTU CAS OCR Server V%s\n", SHMTU_CAS_OCR_SERVER_VERSION);
    std::printf("  Author: Haomin Kong\n");
    std::printf("  C++23 | RESTful API + TCP\n");
    std::printf("\n");
    std::fflush(stdout);
}

void install_signal_handlers(OcrServer& server) {
    g_server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

void clear_signal_handler_target() {
    g_server = nullptr;
}

}  // namespace shmtu::cas::ocr
