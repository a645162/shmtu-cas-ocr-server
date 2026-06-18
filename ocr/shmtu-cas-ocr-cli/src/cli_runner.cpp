#include "cli_runner.h"

#include "cli_files.h"
#include "cli_json.h"
#include "cli_remote.h"

#include <shmtu/cas_ocr/manifest.h>
#include <shmtu/cas_ocr/gui/model_download.h>

#include <algorithm>
#include <exception>
#include <cstdio>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::cli {

namespace {

bool compare_results_match(const CompareEntry& entry) {
    return entry.local_ok && entry.remote_ok &&
           entry.local_result.expression == entry.remote_result.expression &&
           entry.local_result.result == entry.remote_result.result;
}

std::string compare_entry_to_json(const CompareEntry& entry) {
    std::string json = "{";
    json += "\"file\":\"" + json_escape(entry.file_path) + "\",";
    json += "\"local\":" + predict_result_to_json(entry.local_result) + ",";
    json += "\"remote\":" + remote_result_to_json(entry.remote_result) + ",";
    json += "\"match\":" + std::string(compare_results_match(entry) ? "true" : "false");
    json += "}";
    return json;
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
                compare_results_match(entry) ? "OK" : "DIFF");
}

void print_compare_summary(const std::vector<CompareEntry>& entries) {
    const auto both_ok = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return entry.local_ok && entry.remote_ok;
    }));
    const auto matching = static_cast<int>(std::ranges::count_if(entries, [](const auto& entry) {
        return compare_results_match(entry);
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

CompareEntry process_image_compare(shmtu::cas::ocr::CasOcr& ocr,
                                   const std::string& host,
                                   const int port,
                                   const std::string& path) {
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
    return entry;
}

void print_json_array_item_prefix(const size_t index) {
    if (index > 0) {
        std::printf(",\n");
    }
}

}  // namespace

int run_cli(const CliConfig& config) {
    // ---- Subcommand dispatch ----
    if (config.input_path == "__subcmd_list_tags__") {
        long http_status = 0;
        std::string error_message;
        const auto tags = shmtu::cas::ocr::gui::fetchV2ReleaseTags(http_status, error_message);
        if (tags.empty()) {
            std::fprintf(stderr, "Failed to fetch release tags: %s\n", error_message.c_str());
            return 1;
        }
        std::printf("Available v2 release tags (newest first):\n");
        for (const auto& tag : tags) {
            std::printf("  %s\n", tag.c_str());
        }
        std::printf("\nUse 'list-models --tag <tag>' to see available models.\n");
        return 0;
    }

    if (config.input_path == "__subcmd_list_models__") {
        std::string tag;
        if (!config.v2_tag.empty()) {
            tag = config.v2_tag;
        } else {
            long http_status = 0;
            std::string error_message;
            tag = shmtu::cas::ocr::gui::fetchLatestV2Tag(http_status, error_message);
            if (tag.empty()) {
                std::fprintf(stderr, "Failed to fetch latest tag: %s\n", error_message.c_str());
                return 1;
            }
            std::printf("Using latest tag: %s\n", tag.c_str());
        }
        std::printf("Fetching manifest for tag=%s...\n", tag.c_str());
        const std::string sources[] = {"github", "gitee"};
        for (const auto& source : sources) {
            long http_status = 0;
            std::string error_message;
            const auto json_text = shmtu::cas::ocr::gui::downloadReleaseManifest(
                source, tag, http_status, error_message);
            if (!json_text.empty() && http_status == 200) {
                const auto manifest = shmtu::cas::ocr::parse_release_manifest(json_text);
                std::printf("Source: %s\n", source.c_str());
                std::printf("Schema: %d, Model count: %d\n\n",
                            manifest.schema_version, manifest.model_count);
                std::printf("%-40s  %-25s  %-10s  %s\n",
                            "Asset Stem", "Backbone", "Version", "Family");
                std::printf("%-40s  %-25s  %-10s  %s\n",
                            std::string(40, '-').c_str(),
                            std::string(25, '-').c_str(),
                            std::string(10, '-').c_str(),
                            std::string(10, '-').c_str());
                for (const auto& model : manifest.models) {
                    std::printf("%-40s  %-25s  %-10s  %s\n",
                                model.asset_stem.c_str(),
                                model.backbone.c_str(),
                                model.version.c_str(),
                                model.family.c_str());
                }
                return 0;
            }
        }
        std::fprintf(stderr, "Failed to fetch manifest for tag=%s\n", tag.c_str());
        return 1;
    }

    if (config.input_path == "__subcmd_download__") {
        // reuse use_gpu as "use gitee" flag (set in parse_args)
        const bool use_gitee = config.use_gpu;
        std::printf("Download command:\n");
        std::printf("  Version:   %s\n",
                    shmtu::cas::ocr::model_version_to_string(config.model_version).c_str());
        std::printf("  Tag:       %s\n",
                    config.v2_tag.empty() ? "(auto: latest)" : config.v2_tag.c_str());
        std::printf("  Backbone:  %s\n",
                    config.v2_backbone.empty() ? "(default)" : config.v2_backbone.c_str());
        std::printf("  Precision: %s\n", config.precision.c_str());
        std::printf("  Model dir: %s\n", config.model_dir.c_str());
        std::printf("  Source:    %s\n", use_gitee ? "Gitee (primary)" : "GitHub (primary)");

        if (config.model_version == shmtu::cas::ocr::ModelVersion::V1) {
            // V1: use original v1 downloader via existing cli_files/missing
            std::fprintf(stderr, "V1 download: reuses shmtu-cas-ocr-gui downloadModelFiles via wxWidgets Qt is unavailable in CLI; using raw v1.0-NCNN URL with SHA256SUMS verification.\n");
            // For CLI, we'll use the same logic as V2 but with V1 hard-coded paths
            // since the V1 file list is in model_download.cpp (not exposed in CLI).
            // Use the V1 NCNN base URLs.
            std::printf("V1 download not yet fully wired in CLI demo. Please use the GUI or copy files manually.\n");
            return 1;
        }

        // V2 download: fetch manifest, find model, download artifact
        std::string tag;
        if (!config.v2_tag.empty()) {
            tag = config.v2_tag;
        } else {
            long http_status = 0;
            std::string err;
            tag = shmtu::cas::ocr::gui::fetchLatestV2Tag(http_status, err);
            if (tag.empty()) {
                std::fprintf(stderr, "Failed to fetch latest tag: %s\n", err.c_str());
                return 1;
            }
            std::printf("Using latest tag: %s\n", tag.c_str());
        }

        std::string manifest_json;
        long http_status = 0;
        std::string error_message;
        const std::string dl_sources[] = {"github", "gitee"};
        for (const auto& dl_source : dl_sources) {
            manifest_json = shmtu::cas::ocr::gui::downloadReleaseManifest(
                dl_source, tag, http_status, error_message);
            if (!manifest_json.empty() && http_status == 200) {
                std::printf("Got manifest from %s\n", dl_source.c_str());
                break;
            }
        }
        if (manifest_json.empty()) {
            std::fprintf(stderr, "Failed to fetch manifest for tag=%s\n", tag.c_str());
            return 1;
        }

        const auto manifest = shmtu::cas::ocr::parse_release_manifest(manifest_json);
        if (manifest.models.empty()) {
            std::fprintf(stderr, "Manifest empty or parse error\n");
            return 1;
        }

        // Find matching model by backbone
        const shmtu::cas::ocr::ModelInfo* target = nullptr;
        if (!config.v2_backbone.empty()) {
            for (const auto& model : manifest.models) {
                if (model.backbone == config.v2_backbone) {
                    target = &model;
                    break;
                }
            }
        }
        if (!target) {
            target = &manifest.models[0];
            std::printf("Backbone not specified, using first model: %s\n",
                        target->display_name.c_str());
        }

        const bool ok = shmtu::cas::ocr::gui::downloadV2Artifact(
            *target, "ncnn", config.precision, config.model_dir, tag, use_gitee, nullptr,
            error_message);

        if (!ok) {
            std::fprintf(stderr, "Download failed: %s\n", error_message.c_str());
            return 1;
        }
        std::printf("Download completed successfully to: %s\n", config.model_dir.c_str());
        return 0;
    }

    const bool need_local = !config.server_mode || config.compare_mode;
    const bool need_remote = config.server_mode || config.compare_mode;

    std::unique_ptr<shmtu::cas::ocr::CasOcr> ocr;
    if (need_local) {
        ocr = std::make_unique<shmtu::cas::ocr::CasOcr>(config.model_dir);
        std::printf("Loading models from: %s (precision=%s, gpu=%s, version=%s)...\n",
                    config.model_dir.c_str(),
                    config.precision.c_str(),
                    config.use_gpu ? "true" : "false",
                    shmtu::cas::ocr::model_version_to_string(config.model_version).c_str());
        if (!ocr->load_model(config.precision, config.use_gpu, 0, config.model_version)) {
            std::fprintf(stderr, "Failed to load models.\n");
            return 1;
        }
        std::printf("Model loaded (%s).\n\n",
                    ocr->model_status() == shmtu::cas::ocr::ModelStatus::LoadedGPU ? "GPU" : "CPU");
    }

    if (need_remote) {
        std::printf("Remote server: %s:%d\n", config.server_host.c_str(), config.server_port);
        if (const auto health = check_remote_server(config); health) {
            std::printf("Server health: OK\n\n");
        } else {
            std::fprintf(stderr, "Warning: %s\nProceeding anyway...\n\n", health.error().c_str());
        }
    }

    const auto image_paths = collect_image_paths(config.input_path);
    if (!image_paths) {
        std::fprintf(stderr, "%s.\n", image_paths.error().c_str());
        return 1;
    }

    if (config.server_mode && !config.compare_mode) {
        if (config.json_output) {
            std::printf("[\n");
        }
        for (size_t i = 0; i < image_paths->size(); ++i) {
            if (config.json_output) {
                print_json_array_item_prefix(i);
            }
            process_image_remote(config.server_host, config.server_port, (*image_paths)[i],
                                 config.json_output);
        }
        if (config.json_output) {
            std::printf("\n]\n");
        } else {
            std::printf("\nProcessed %zu image(s) via remote server.\n", image_paths->size());
        }
        return 0;
    }

    if (config.compare_mode) {
        std::vector<CompareEntry> entries;
        entries.reserve(image_paths->size());

        if (config.json_output) {
            std::printf("[\n");
        } else {
            std::printf("Compare mode: local OCR vs remote server\n\n");
            print_compare_header();
        }

        for (size_t i = 0; i < image_paths->size(); ++i) {
            auto entry = process_image_compare(*ocr, config.server_host, config.server_port,
                                               (*image_paths)[i]);
            if (config.json_output) {
                print_json_array_item_prefix(i);
                std::printf("%s", compare_entry_to_json(entry).c_str());
            } else {
                print_compare_entry(entry);
            }
            entries.push_back(std::move(entry));
        }

        if (config.json_output) {
            std::printf("\n]\n");
        } else {
            print_compare_summary(entries);
        }
        return 0;
    }

    if (config.json_output) {
        std::printf("[\n");
    }
    for (size_t i = 0; i < image_paths->size(); ++i) {
        if (config.json_output) {
            print_json_array_item_prefix(i);
        }
        process_image_local(*ocr, (*image_paths)[i], config.json_output);
    }
    if (config.json_output) {
        std::printf("\n]\n");
    } else {
        std::printf("\nProcessed %zu image(s).\n", image_paths->size());
    }

    return 0;
}

}  // namespace shmtu::cas::ocr::cli
