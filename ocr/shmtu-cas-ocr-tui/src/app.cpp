// SPDX-License-Identifier: MIT
#include "app.h"

#include "tui/components.h"

#include <shmtu/cas_ocr/gui/model_download.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <utility>

namespace shmtu::cas::ocr::tui {

using tui_components::panel;
using tui_components::statusLine;

namespace {

bool useGiteeFromEnv() {
    // Primary: SHMTU_MODEL_SOURCE=gitee|github (same as server/docker-compose).
    if (const char* src = std::getenv("SHMTU_MODEL_SOURCE")) {
        std::string_view sv(src);
        if (sv == "github") return false;
        if (sv == "gitee") return true;
    }
    // Legacy: SHMTU_USE_GITEE=0 → github, anything else → gitee (default).
    if (const char* v = std::getenv("SHMTU_USE_GITEE"); v && *v && v[0] == '0') {
        return false;
    }
    return true;
}

}  // namespace

App::App() = default;

App::~App() {
    download_screen_.cancelRequested();
    if (refresh_worker_.joinable()) refresh_worker_.join();
    if (manifest_worker_.joinable()) manifest_worker_.join();
    if (download_worker_.joinable()) download_worker_.join();
}

int App::run() {
    tag_list_.setOnRefresh([this] { startRefreshWorker(); });
    tag_list_.setOnTagSelected([this](const std::string& tag) {
        startManifestWorker(tag);
    });
    model_list_.setOnDownloadRequested(
        [this](int idx, const std::string& engine, const std::string& prec) {
            startDownloadWorker(idx, engine, prec);
        });

    // Wire download dialog callbacks.  When the user confirms, we
    // start the actual download.
    download_dialog_.setOnConfirm([this](const std::string& engine,
                                          const std::string& precision) {
        if (!model_list_.empty()) {
            startDownloadWorker(model_list_.selectedIndex(), engine, precision);
        }
    });
    download_dialog_.setOnCancel([this] { /* nothing — dialog auto-hides */ });

    auto main_renderer = ftxui::Renderer([this] {
        ftxui::Elements top;
        top.push_back(ftxui::hbox(ftxui::Elements{
            ftxui::text("[SHMTU CAS OCR]") | ftxui::bold |
                ftxui::color(ftxui::Color::Cyan),
            ftxui::filler(),
            ftxui::text("q=quit  r=refresh  d=download") | ftxui::dim,
        }));
        top.push_back(ftxui::separator());
        ftxui::Elements middle;
        middle.push_back(tag_list_.Render() | ftxui::flex);
        middle.push_back(ftxui::separator());
        middle.push_back(model_list_.Render() | ftxui::flex);
        middle.push_back(ftxui::separator());
        middle.push_back(download_screen_.Render() |
                         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 50));
        top.push_back(ftxui::hbox(std::move(middle)) | ftxui::flex);
        top.push_back(ftxui::separator());
        top.push_back(statusLine(status(), !status().empty()));

        // If the download dialog is visible, render it as a modal.
        ftxui::Element main_view = ftxui::vbox(std::move(top));
        if (download_dialog_.visible()) {
            ftxui::Element modal = download_dialog_.Render();
            return ftxui::vbox({
                main_view | ftxui::dim,
                modal,
            });
        }
        return main_view;
    });

    main_component_ = ftxui::CatchEvent(main_renderer, [this](ftxui::Event e) {
        // If the download dialog is visible, let it consume events first.
        if (download_dialog_.visible()) {
            if (download_dialog_.handleEvent(e)) return true;
            // If dialog didn't handle it, block globally so no other
            // key bindings fire while the dialog is open (except ESC).
            return true;
        }

        if (e == ftxui::Event::Character('q') ||
            e == ftxui::Event::Character('Q')) {
            screen_.Exit();
            return true;
        }
        if (e == ftxui::Event::Character('r') ||
            e == ftxui::Event::Character('R')) {
            startRefreshWorker();
            return true;
        }
        if (e == ftxui::Event::Character('d') ||
            e == ftxui::Event::Character('D')) {
            if (!model_list_.empty()) {
                // Collect available engines & precisions for the dialog.
                const auto& models = model_list_.models();
                int idx = model_list_.selectedIndex();
                if (idx >= 0 && idx < static_cast<int>(models.size()) &&
                    models[idx]) {
                    std::vector<std::string> engines;
                    std::vector<std::string> precisions;
                    for (const auto& [engine, precs] : models[idx]->artifacts) {
                        engines.push_back(engine);
                        for (const auto& [prec, _] : precs) {
                            if (std::find(precisions.begin(), precisions.end(),
                                          prec) == precisions.end())
                                precisions.push_back(prec);
                        }
                    }
                    if (engines.empty()) engines = {"ncnn"};
                    if (precisions.empty()) precisions = {"fp32", "fp16"};
                    download_dialog_.show(engines, precisions, 0, 0);
                }
            }
            return true;
        }
        return false;
    });

    startRefreshWorker();

    screen_.Loop(main_component_);
    return 0;
}

void App::startRefreshWorker() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (refresh_busy_) return;
        refresh_busy_ = true;
    }
    if (refresh_worker_.joinable()) {
        refresh_worker_.join();
    }
    setStatus("Refreshing release list...");
    refresh_worker_ = std::thread([this] {
        long http_status = 0;
        std::string err;
        auto releases = github_.listReleases(2, http_status, err);
        if (http_status != 200) {
            setStatus("API list failed: HTTP " +
                      std::to_string(http_status) + " - " + err);
        } else if (releases.empty()) {
            setStatus("No v2 releases found.");
        } else {
            // Fetch manifest summaries to populate model_count for each tag.
            for (auto& r : releases) {
                long ms = 0;
                std::string me;
                std::string body = github_.fetchManifestJson(r.tag, ms, me);
                if (ms == 200 && !body.empty()) {
                    auto summary = shmtu::cas::ocr::parse_release_manifest_summary(
                        r.tag, body);
                    r.model_count = summary.model_count;
                } else {
                    r.model_count = -1;  // unavailable
                }
            }
            setStatus("Loaded " + std::to_string(releases.size()) +
                      " release(s).");
        }
        tag_list_.setReleases(std::move(releases));
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            refresh_busy_ = false;
        }
        screen_.Post([this] { /* repaint */ });
    });
}

void App::startManifestWorker(const std::string& tag) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (manifest_tag_being_fetched_ == tag) return;
        manifest_tag_being_fetched_ = tag;
    }
    if (manifest_worker_.joinable()) {
        manifest_worker_.join();
    }
    setStatus("Loading manifest for " + tag + "...");
    manifest_worker_ = std::thread([this, tag] {
        long http_status = 0;
        std::string err;
        std::string body =
            github_.fetchManifestJson(tag, http_status, err);
        if (http_status != 200 || body.empty()) {
            setStatus("Manifest download failed: HTTP " +
                      std::to_string(http_status) + " - " + err);
        } else {
            auto manifest = shmtu::cas::ocr::parse_release_manifest(body);
            if (manifest.schema_version <= 0 || manifest.model_count == 0) {
                setStatus("Manifest parse failed for " + tag);
            } else {
                setStatus("Loaded " + std::to_string(manifest.model_count) +
                          " model(s) for " + tag);
                model_list_.setManifest(manifest, tag);
            }
        }
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            manifest_tag_being_fetched_.clear();
        }
        screen_.Post([this] { /* repaint */ });
    });
}

void App::startDownloadWorker(int model_index, const std::string& engine,
                              const std::string& precision) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (download_being_fetched_.model_idx == model_index &&
            download_being_fetched_.engine == engine &&
            download_being_fetched_.precision == precision)
            return;
        download_being_fetched_ = {model_index, engine, precision};
    }
    if (model_list_.empty()) {
        setStatus("No model selected.");
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    const auto& models = model_list_.models();
    if (model_index < 0 ||
        model_index >= static_cast<int>(models.size())) {
        setStatus("Invalid model index.");
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    const auto* model = models[model_index];
    if (!model) {
        setStatus("Null model entry.");
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    const std::string tag = model_list_.currentTag();
    if (tag.empty()) {
        setStatus("No release selected.");
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    const auto* artifact =
        shmtu::cas::ocr::find_artifact(*model, engine, precision);
    if (artifact == nullptr) {
        setStatus("Manifest has no " + engine + "/" + precision +
                  " artifact for " + model->asset_stem);
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    if (artifact->files.empty()) {
        setStatus("Artifact " + engine + "/" + precision + " has no files.");
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        return;
    }
    const std::string url = github_.buildAssetUrl(
        useGiteeFromEnv() ? "gitee" : "github", tag,
        artifact->files.front().release_asset_name);
    const std::string dest =
        model_dir_.empty()
            ? GitHubClient::defaultAssetCacheDir(tag, model->asset_stem)
            : model_dir_;

    if (download_worker_.joinable()) {
        download_worker_.join();
    }
    download_screen_.resetCancel();
    download_screen_.begin(tag, model->asset_stem, engine, precision, url,
                           dest);
    setStatus("Downloading " + model->asset_stem + "...");

    download_worker_ = std::thread([this, model, engine, precision, dest, tag] {
        long http_status = 0;
        std::string err;
        bool ok = false;
        const auto* artifact =
            shmtu::cas::ocr::find_artifact(*model, engine, precision);
        if (!artifact || artifact->files.empty()) {
            download_screen_.finish(false, "artifact not found");
            {
                std::lock_guard<std::mutex> lock(worker_mutex_);
                download_being_fetched_.model_idx = -1;
            }
            return;
        }
        for (const auto& f : artifact->files) {
            const std::string file_url = github_.buildAssetUrl(
                useGiteeFromEnv() ? "gitee" : "github", tag,
                f.release_asset_name);
            const std::string file_dest =
                (std::filesystem::path(dest) / f.release_asset_name).string();
            bool file_ok = http_.downloadToFileWithProgress(
                file_url, file_dest,
                [this](long long now, long long total) {
                    download_screen_.updateBytes(now, total);
                    return !download_screen_.shouldCancel();
                },
                http_status, err);
            if (file_ok) {
                // Verify SHA256 if the manifest provides a digest.
                if (!f.sha256.empty()) {
                    const auto actual_hash =
                        shmtu::cas::ocr::gui::computeSha256(file_dest);
                    if (!actual_hash.empty() &&
                        actual_hash != f.sha256) {
                        std::error_code ec;
                        std::filesystem::remove(file_dest, ec);
                        file_ok = false;
                        err = "SHA256 mismatch for " + f.release_asset_name;
                        // Try fallback source.
                        const std::string fallback_source =
                            useGiteeFromEnv() ? "github" : "gitee";
                        const std::string fallback_url =
                            github_.buildAssetUrl(
                                fallback_source, tag,
                                f.release_asset_name);
                        file_ok = http_.downloadToFileWithProgress(
                            fallback_url, file_dest,
                            [this](long long now, long long total) {
                                download_screen_.updateBytes(now, total);
                                return !download_screen_.shouldCancel();
                            },
                            http_status, err);
                        if (file_ok && !f.sha256.empty()) {
                            const auto retry_hash =
                                shmtu::cas::ocr::gui::computeSha256(
                                    file_dest);
                            if (!retry_hash.empty() &&
                                retry_hash != f.sha256) {
                                std::error_code ec;
                                std::filesystem::remove(file_dest, ec);
                                file_ok = false;
                                err = "SHA256 mismatch (fallback) for " +
                                      f.release_asset_name;
                            }
                        }
                    }
                }
            }
            ok = file_ok || ok;
        }
        if (ok) {
            download_screen_.finish(true, "Downloaded to " + dest);
            setStatus("Download OK -> " + dest);
        } else {
            download_screen_.finish(false, "Download failed: " + err);
            setStatus("Download FAILED: " + err);
        }
        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            download_being_fetched_.model_idx = -1;
        }
        screen_.Post([this] { /* repaint */ });
    });
}

}  // namespace shmtu::cas::ocr::tui
