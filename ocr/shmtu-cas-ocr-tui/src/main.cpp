// SPDX-License-Identifier: MIT
//
// shmtu-cas-ocr-tui entry point.
//
// The TUI itself lives in `App`.  `main` simply constructs an
// `App`, runs it and forwards its exit code.
//
// Command-line flags:
//   --help        Print this text and exit.
//   --version     Print the version string and exit.
//   --model-dir   Override model cache directory (default ~/.cache/shmtu-cas-ocr).

#include "app.h"

#include <shmtu/cas_ocr/version.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s [--help] [--version] [--model-dir DIR]\n"
        "\n"
        "Interactive terminal TUI for browsing and downloading\n"
        "shmtu-cas-ocr-model releases from GitHub.\n"
        "\n"
        "Flags:\n"
        "  --help       Show this message and exit.\n"
        "  --version    Print version string and exit.\n"
        "  --model-dir  Override default model cache directory.\n"
        "\n"
        "Environment:\n"
        "  SHMTU_USE_GITEE=1   Prefer Gitee over GitHub for asset downloads.\n"
        "  XDG_CACHE_HOME      Override the cache-base directory.\n",
        argv0);
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--version") == 0 ||
            std::strcmp(argv[i], "-v") == 0) {
            // The build system defines SHMTU_CAS_OCR_VERSION via
            // version.h (generated from version.h.in).
            std::fprintf(stdout, "shmtu-cas-ocr-tui %s\n",
                         SHMTU_CAS_OCR_VERSION);
            return 0;
        }
    }

    shmtu::cas::ocr::tui::App app;
    return app.run();
}
