// SPDX-License-Identifier: MIT
#include "screens/download_progress.h"

#include "tui/components.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <utility>

namespace shmtu::cas::ocr::tui {

using tui_components::panel;
using tui_components::progressBar;
using tui_components::statusLine;

DownloadProgressScreen::DownloadProgressScreen() = default;

void DownloadProgressScreen::begin(const std::string& tag,
                                   const std::string& asset_stem,
                                   const std::string& engine,
                                   const std::string& precision,
                                   const std::string& url,
                                   const std::string& dest) {
    std::lock_guard<std::mutex> lock(mu_);
    tag_ = tag;
    asset_stem_ = asset_stem;
    engine_ = engine;
    precision_ = precision;
    url_ = url;
    dest_ = dest;
    bytes_now_ = 0;
    bytes_total_ = 0;
    finished_ = false;
    last_ok_ = false;
    last_message_.clear();
    cancel_flag_.store(false);
    active_.store(true);
}

void DownloadProgressScreen::updateBytes(long long bytes_now,
                                         long long bytes_total) {
    std::lock_guard<std::mutex> lock(mu_);
    bytes_now_ = bytes_now;
    if (bytes_total > 0) bytes_total_ = bytes_total;
}

void DownloadProgressScreen::finish(bool ok, const std::string& message) {
    std::lock_guard<std::mutex> lock(mu_);
    finished_ = true;
    last_ok_ = ok;
    last_message_ = message;
    if (ok && bytes_total_ == 0 && bytes_now_ > 0) {
        bytes_total_ = bytes_now_;
    }
    active_.store(false);
}

ftxui::Element DownloadProgressScreen::Render() {
    std::lock_guard<std::mutex> lock(mu_);

    ftxui::Element body;
    if (!active_.load() && tag_.empty()) {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("No active download.") | ftxui::dim);
        rows.push_back(ftxui::text("") | ftxui::dim);
        rows.push_back(ftxui::text("Select a model and press 'd'.") | ftxui::dim);
        body = ftxui::vbox(std::move(rows));
    } else {
        std::ostringstream info;
        info << "tag: " << tag_ << "\n"
             << "asset: " << asset_stem_ << "\n"
             << "engine/precision: " << engine_ << "/" << precision_ << "\n"
             << "url: " << url_ << "\n"
             << "dest: " << dest_;

        float fraction = 0.0f;
        if (bytes_total_ > 0) {
            fraction = static_cast<float>(bytes_now_) /
                       static_cast<float>(bytes_total_);
        }
        std::ostringstream size;
        size << bytes_now_ << " / " << bytes_total_ << " bytes";

        ftxui::Elements rows;
        rows.push_back(ftxui::paragraph(info.str()) | ftxui::dim);
        rows.push_back(ftxui::separator());
        rows.push_back(progressBar(fraction, 40));
        rows.push_back(ftxui::text(size.str()) | ftxui::dim);
        rows.push_back(ftxui::separator());
        rows.push_back(finished_ ? statusLine(last_message_, !last_ok_)
                                : ftxui::text(""));
        body = ftxui::vbox(std::move(rows));
    }

    return panel("Download", body);
}

ftxui::Component DownloadProgressScreen::component() {
    return ftxui::Renderer([this] { return Render(); });
}

}  // namespace shmtu::cas::ocr::tui
