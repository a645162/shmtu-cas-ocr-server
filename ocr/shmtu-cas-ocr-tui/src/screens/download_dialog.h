// SPDX-License-Identifier: MIT


#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

// Modal download dialog that lets the user pick engine + precision +
// confirm with 'd' or cancel with 'q'.
//
// The pop-up replaces the current component tree so keyboard focus
// is captured until the user cancels or confirms.
class DownloadDialog {
public:
    // Called when the user presses 'd' to confirm.
    // Arguments: engine, precision.
    using ConfirmCb = std::function<void(const std::string& engine,
                                         const std::string& precision)>;
    using CancelCb = std::function<void()>;

    DownloadDialog();

    // Populate the dialog with the available choices inferred from
    // the parsed manifest.  `default_engine` / `default_precision`
    // are the pre-selected indices.
    void show(const std::vector<std::string>& engines,
              const std::vector<std::string>& precisions,
              int default_engine,
              int default_precision);

    void hide() { visible_ = false; }
    bool visible() const { return visible_; }

    ftxui::Element Render();

    void setOnConfirm(ConfirmCb cb) { on_confirm_ = std::move(cb); }
    void setOnCancel(CancelCb cb) { on_cancel_ = std::move(cb); }

    // Handle keyboard events while the dialog is visible.
    // Returns true if the event was consumed.
    bool handleEvent(ftxui::Event e);

    std::string selectedEngine() const { return engine_choices_[selected_engine_]; }
    std::string selectedPrecision() const { return precision_choices_[selected_precision_]; }

private:
    bool visible_ = false;
    std::vector<std::string> engine_choices_;
    std::vector<std::string> precision_choices_;
    int selected_engine_ = 0;
    int selected_precision_ = 0;

    ConfirmCb on_confirm_;
    CancelCb on_cancel_;

    ftxui::Component engine_component_;
    ftxui::Component precision_component_;
    ftxui::Component dialog_container_;
};

}  // namespace shmtu::cas::ocr::tui
