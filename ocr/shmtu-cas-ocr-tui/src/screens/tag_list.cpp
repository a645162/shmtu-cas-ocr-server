// SPDX-License-Identifier: MIT
#include "screens/tag_list.h"

#include "tui/components.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <utility>

namespace shmtu::cas::ocr::tui {

using tui_components::panel;
using tui_components::statusLine;

TagListScreen::TagListScreen() = default;

std::string TagListScreen::selectedTag() const {
    if (releases_.empty()) return {};
    int idx = selected_;
    if (idx < 0 || idx >= static_cast<int>(releases_.size())) {
        return releases_.front().tag;
    }
    return releases_[idx].tag;
}

void TagListScreen::setSelectedTag(const std::string& tag) {
    for (std::size_t i = 0; i < releases_.size(); ++i) {
        if (releases_[i].tag == tag) {
            selected_ = static_cast<int>(i);
            return;
        }
    }
    selected_ = 0;
}

void TagListScreen::refresh() {
    loading_ = true;
    last_error_.clear();
    if (on_refresh_) on_refresh_();
}

void TagListScreen::setReleases(std::vector<ReleaseSummary> releases) {
    releases_ = std::move(releases);
    loading_ = false;
    if (selected_ >= static_cast<int>(releases_.size())) {
        selected_ = 0;
    }
    if (!releases_.empty() && on_tag_selected_) {
        on_tag_selected_(selectedTag());
    }
}

ftxui::Element TagListScreen::Render() {
    ftxui::Element body;
    if (loading_) {
        ftxui::Elements rows;
        rows.push_back(ftxui::spinner(21, spinner_frame_) | ftxui::center);
        spinner_frame_ = (spinner_frame_ + 1) % 256;
        rows.push_back(ftxui::text("Loading releases from GitHub...") |
                       ftxui::dim);
        body = ftxui::vbox(std::move(rows));
    } else if (releases_.empty()) {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("No releases loaded.") | ftxui::dim);
        rows.push_back(ftxui::text("") | ftxui::dim);
        rows.push_back(ftxui::text("Press 'r' to refresh.") | ftxui::dim);
        body = ftxui::vbox(std::move(rows));
    } else {
        std::vector<std::string> entries;
        entries.reserve(releases_.size());
        for (const auto& r : releases_) {
            std::ostringstream oss;
            oss << r.tag;
            if (r.prerelease) oss << "  [pre]";
            if (r.draft) oss << "  [draft]";
            entries.push_back(oss.str());
        }
        auto menu = ftxui::Menu(&entries, &selected_);
        ftxui::Elements rows;
        rows.push_back(ftxui::text("v2 release tags (newest first)") |
                       ftxui::bold);
        rows.push_back(ftxui::separator());
        rows.push_back(menu->Render() | ftxui::frame | ftxui::flex);
        body = ftxui::vbox(std::move(rows));
    }
    return panel("Releases", body);
}

ftxui::Component TagListScreen::component() {
    return ftxui::Renderer([this] { return Render(); });
}

}  // namespace shmtu::cas::ocr::tui
