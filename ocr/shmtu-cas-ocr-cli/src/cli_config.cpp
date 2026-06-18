#include "cli_config.h"

#include <shmtu/cas_ocr/manifest.h>
#include <shmtu/cas_ocr/version.h>

#include <CLI/CLI.hpp>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <set>
#include <string_view>
#include <utility>

namespace shmtu::cas::ocr::cli {

namespace {

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

}  // namespace

void print_banner() {
    std::printf("SHMTU CAS OCR CLI V%s\n", SHMTU_CAS_OCR_CLI_VERSION);
}

std::expected<CliConfig, std::string> parse_args(int argc, char* argv[]) {
    CliConfig config;
    std::string server_endpoint;

    CLI::App app{"SHMTU CAS OCR CLI"};
    app.set_help_flag("-h,--help", "Show this help");
    app.footer(
        "Examples:\n"
        "  shmtu_cas_ocr_cli captcha.png\n"
        "  shmtu_cas_ocr_cli --json ./captcha_images/\n"
        "  shmtu_cas_ocr_cli --server 127.0.0.1:21600 captcha.png\n"
        "  shmtu_cas_ocr_cli --server 127.0.0.1:21600 --compare ./captcha_images/\n"
        "  shmtu_cas_ocr_cli download --version v2 --tag v2.0.5 --precision fp32\n"
        "  shmtu_cas_ocr_cli list-tags\n"
        "  shmtu_cas_ocr_cli list-models --tag v2.0.5\n");

    // ---- Subcommand: download ----
    auto* download_cmd = app.add_subcommand("download", "Download OCR model files");
    std::string dl_version = "v2";
    std::string dl_tag;
    std::string dl_backbone;
    std::string dl_precision = "fp16";
    bool dl_gitee = false;

    download_cmd->add_option("--version", dl_version,
                             "Model version: v1 or v2 (default: v2)")
        ->check(CLI::IsMember(std::set<std::string>{"v1", "v2", "V1", "V2", "1", "2"}));
    download_cmd->add_option("--tag", dl_tag, "Release tag for v2, e.g. v2.0.5");
    download_cmd->add_option("--backbone", dl_backbone,
                             "Backbone for v2, e.g. mobilenet_v3_small");
    download_cmd->add_option("--precision", dl_precision, "Model precision (fp16 or fp32)")
        ->check(CLI::IsMember(std::set<std::string>{"fp16", "fp32"}));
    download_cmd->add_option("--model-dir", config.model_dir, "Model directory");
    download_cmd->add_flag("--use-gitee", dl_gitee, "Use Gitee as primary download source");

    // ---- Subcommand: list-tags ----
    auto* list_tags_cmd = app.add_subcommand("list-tags", "List available v2 release tags");

    // ---- Subcommand: list-models ----
    auto* list_models_cmd = app.add_subcommand("list-models", "List models from a v2 release manifest");
    std::string lm_tag;
    list_models_cmd->add_option("--tag", lm_tag, "Release tag, e.g. v2.0.5 (default: latest)");

    // ---- Main OCR command options ----
    app.add_option("input", config.input_path, "Image path or directory");
    app.add_option("--model-dir", config.model_dir, "Model directory")->capture_default_str();
    app.add_option("--precision", config.precision, "Model precision")
        ->capture_default_str()
        ->check(CLI::IsMember(std::set<std::string>{"fp16", "fp32"}));
    app.add_flag("--use-gpu", config.use_gpu, "Enable GPU acceleration");
    std::string model_version_str = "v2";
    app.add_option("--model-version", model_version_str,
                   "Model version: v1 (3-model) or v2 (TriSlot, default)")
        ->capture_default_str()
        ->check(CLI::IsMember(std::set<std::string>{"v1", "v2", "V1", "V2", "1", "2"}));
    app.add_option("--v2-tag", config.v2_tag, "V2 release tag, e.g. v2.0.5");
    app.add_option("--v2-backbone", config.v2_backbone,
                   "V2 backbone, e.g. mobilenet_v3_small");
    app.add_flag("--json", config.json_output, "Output results as JSON");
    app.add_option("--server", server_endpoint, "Use remote OCR server instead of local model");
    app.add_flag("--compare", config.compare_mode, "Compare local OCR vs remote server results");

    // Require input only for the main OCR command, not subcommands
    app.get_option("input")->required(false);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        std::exit(app.exit(error));
    }

    // ---- Handle subcommands ----
    if (download_cmd->parsed()) {
        config.v2_tag = dl_tag;
        config.v2_backbone = dl_backbone;
        config.precision = dl_precision;
        config.model_version = shmtu::cas::ocr::model_version_from_string(dl_version);
        config.input_path = "__subcmd_download__";
        config.use_gpu = dl_gitee;  // repurpose use_gpu flag for "use gitee"
        // Mark server_mode as a sentinel for download subcommand
        return config;
    }

    if (list_tags_cmd->parsed()) {
        config.input_path = "__subcmd_list_tags__";
        return config;
    }

    if (list_models_cmd->parsed()) {
        config.v2_tag = lm_tag;
        config.input_path = "__subcmd_list_models__";
        return config;
    }

    // ---- Main OCR command ----
    if (config.input_path.empty()) {
        return std::unexpected("Input path is required for OCR. Use --help for subcommands.");
    }

    config.model_version = shmtu::cas::ocr::model_version_from_string(model_version_str);

    if (!server_endpoint.empty()) {
        auto endpoint = parse_server_endpoint(server_endpoint);
        if (!endpoint) {
            return std::unexpected(endpoint.error());
        }

        auto& [host, port] = *endpoint;
        config.server_host = std::move(host);
        config.server_port = port;
        config.server_mode = true;
    }

    if (config.compare_mode && !config.server_mode) {
        return std::unexpected("--compare requires --server");
    }

    return config;
}

}  // namespace shmtu::cas::ocr::cli
