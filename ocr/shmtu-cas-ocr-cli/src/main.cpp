#include "cli_config.h"
#include "cli_runner.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    shmtu::cas::ocr::cli::print_banner();

    const auto config = shmtu::cas::ocr::cli::parse_args(argc, argv);
    if (!config) {
        std::fprintf(stderr, "Error: %s\n", config.error().c_str());
        return 1;
    }

    return shmtu::cas::ocr::cli::run_cli(*config);
}
