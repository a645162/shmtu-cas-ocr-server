// SPDX-License-Identifier: MIT
#pragma once

#include "github/github_client.h"
#include "http/http_client.h"
#include "screens/download_dialog.h"
#include "screens/download_progress.h"
#include "screens/model_list.h"
#include "screens/tag_list.h"

#include <shmtu/cas_ocr/manifest.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace shmtu::cas::ocr::tui {

// Top-level FTXUI application.  Owns the three screens, kicks off
// the GitHub / manifest / download network calls on worker threads
// and post their results back to the UI via `ScreenInteractive`.
class App {
public:
    App();
    ~App();

    // Run the FTXUI event loop.  Returns the process exit code.
    int run();

private:
    // -- Screens --------------------------------------------------------
    TagListScreen tag_list_;
    ModelListScreen model_list_;
    DownloadProgressScreen download_screen_;
    DownloadDialog download_dialog_;

    // -- Network state ---------------------------------------------------
    std::thread refresh_worker_;
    std::thread manifest_worker_;
    std::thread download_worker_;

    // Dedup guards: prevent re-entrancy and duplicate network calls.
    std::mutex worker_mutex_;
    bool refresh_busy_ = false;
    std::string manifest_tag_being_fetched_;   // empty when idle
    struct {
        int model_idx = -1;
        std::string engine;
        std::string precision;
    } download_being_fetched_;                  // model_idx == -1 when idle

    GitHubClient github_;
    HttpClient http_;

    // -- UI plumbing ----------------------------------------------------
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
    ftxui::Component main_component_;
    std::string last_error_;

    void startRefreshWorker();
    void startManifestWorker(const std::string& tag);
    void startDownloadWorker(int model_index,
                             const std::string& engine,
                             const std::string& precision);

    void setStatus(const std::string& msg) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_error_ = msg;
    }
    std::string status() const {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return last_error_;
    }
    mutable std::mutex status_mutex_;
};

}  // namespace shmtu::cas::ocr::tui
