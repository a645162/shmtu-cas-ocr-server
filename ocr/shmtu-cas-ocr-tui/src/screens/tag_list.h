// SPDX-License-Identifier: MIT

#pragma once

#include "../github/github_client.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

// The leftmost screen: lists every v2 release tag fetched from the
// GitHub releases endpoint.  Selecting a tag triggers an async
// `onTagSelected` callback so the parent `App` can start fetching
// the corresponding manifest.
class TagListScreen {
public:
    using TagSelectedCb = std::function<void(const std::string& tag)>;
    using RefreshCb = std::function<void()>;

    TagListScreen();

    // Begin a refresh.  Posts results back asynchronously and the
    // next call to `Render()` will surface them.
    void refresh();
    bool isLoading() const { return loading_; }
    const std::string& lastError() const { return last_error_; }

    // Returns the currently selected tag (or empty string).
    std::string selectedTag() const;

    // Set the selected tag programmatically (e.g. after refresh
    // completes we may want to keep the previous selection if it
    // is still present).
    void setSelectedTag(const std::string& tag);

    // Provide a list of releases to the screen.  Used by the parent
    // to deliver async results.
    void setReleases(std::vector<ReleaseSummary> releases);

    ftxui::Element Render();
    ftxui::Component component();

    void setOnTagSelected(TagSelectedCb cb) { on_tag_selected_ = std::move(cb); }
    void setOnRefresh(RefreshCb cb) { on_refresh_ = std::move(cb); }

private:
    std::vector<ReleaseSummary> releases_;
    int selected_ = 0;
    bool loading_ = false;
    std::string last_error_;
    TagSelectedCb on_tag_selected_;
    RefreshCb on_refresh_;
    int spinner_frame_ = 0;
};

}  // namespace shmtu::cas::ocr::tui
