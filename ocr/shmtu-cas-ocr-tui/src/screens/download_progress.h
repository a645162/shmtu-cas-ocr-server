// SPDX-License-Identifier: MIT


#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace shmtu::cas::ocr::tui {

// The right-hand / bottom screen: shows the current download
// progress for the most recent model asset.  The download itself is
// driven by the parent `App` (so it can run on a background thread);
// this screen just renders state and offers a "Cancel" button.
class DownloadProgressScreen {
public:
    using CancelCb = std::function<bool()>;

    DownloadProgressScreen();

    // Mutators (called from the network worker thread).
    void begin(const std::string& tag,
               const std::string& asset_stem,
               const std::string& engine,
               const std::string& precision,
               const std::string& url,
               const std::string& dest);
    void updateBytes(long long bytes_now, long long bytes_total);
    void finish(bool ok, const std::string& message);
    void cancelRequested() {
        cancel_flag_.store(true);
    }
    bool isActive() const { return active_.load(); }
    bool shouldCancel() const { return cancel_flag_.load(); }
    void resetCancel() { cancel_flag_.store(false); }

    ftxui::Element Render();
    ftxui::Component component();

    void setOnCancel(CancelCb cb) { on_cancel_ = std::move(cb); }

private:
    std::mutex mu_;
    std::atomic<bool> active_{false};
    std::atomic<bool> cancel_flag_{false};
    std::string tag_;
    std::string asset_stem_;
    std::string engine_;
    std::string precision_;
    std::string url_;
    std::string dest_;
    long long bytes_now_ = 0;
    long long bytes_total_ = 0;
    bool finished_ = false;
    bool last_ok_ = false;
    std::string last_message_;

    CancelCb on_cancel_;
};

}  // namespace shmtu::cas::ocr::tui
