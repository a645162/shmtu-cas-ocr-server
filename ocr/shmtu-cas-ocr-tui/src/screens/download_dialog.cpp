// SPDX-License-Identifier: MIT
#include "screens/download_dialog.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace shmtu::cas::ocr::tui {

DownloadDialog::DownloadDialog() = default;

void DownloadDialog::show(const std::vector<std::string>& engines,
                          const std::vector<std::string>& precisions,
                          int default_engine,
                          int default_precision) {
    engine_choices_ = engines;
    precision_choices_ = precisions;
    selected_engine_ = std::max(0, std::min(default_engine,
                                            static_cast<int>(engines.size()) - 1));
    selected_precision_ = std::max(0, std::min(default_precision,
                                               static_cast<int>(precisions.size()) - 1));

    engine_component_ = ftxui::Menu(&engine_choices_, &selected_engine_);
    precision_component_ = ftxui::Menu(&precision_choices_, &selected_precision_);

    // Put both menus in a container so FTXUI routes keyboard events to them.
    dialog_container_ = ftxui::Container::Horizontal({
        engine_component_,
        precision_component_,
    });

    visible_ = true;
}

ftxui::Element DownloadDialog::Render() {
    if (!visible_) return ftxui::emptyElement();

    ftxui::Elements left;
    left.push_back(ftxui::text("Engine:") | ftxui::bold);
    left.push_back(ftxui::separator());
    left.push_back(engine_component_->Render() | ftxui::frame);

    ftxui::Elements right;
    right.push_back(ftxui::text("Precision:") | ftxui::bold);
    right.push_back(ftxui::separator());
    right.push_back(precision_component_->Render() | ftxui::frame);

    ftxui::Elements body;
    body.push_back(ftxui::hbox(std::move(left), ftxui::separator(),
                               std::move(right)));
    body.push_back(ftxui::separator());
    ftxui::Elements footer;
    footer.push_back(ftxui::text("d = confirm download   q = cancel   Tab = switch") |
                     ftxui::dim);
    footer.push_back(ftxui::filler());
    body.push_back(ftxui::hbox(std::move(footer)));

    ftxui::Element popup = ftxui::vbox(std::move(body)) |
                           ftxui::border |
                           ftxui::center |
                           ftxui::clear_under;

    return ftxui::vbox({
        ftxui::text("[Download Configuration]") | ftxui::bold |
            ftxui::color(ftxui::Color::Yellow),
        ftxui::separator(),
        popup | ftxui::flex,
    });
}

bool DownloadDialog::handleEvent(ftxui::Event e) {
    if (!visible_) return false;

    if (e == ftxui::Event::Character('q') || e == ftxui::Event::Character('Q')) {
        visible_ = false;
        if (on_cancel_) on_cancel_();
        return true;
    }
    if (e == ftxui::Event::Character('d') || e == ftxui::Event::Character('D')) {
        visible_ = false;
        if (on_confirm_) {
            on_confirm_(engine_choices_[selected_engine_],
                       precision_choices_[selected_precision_]);
        }
        return true;
    }

    // Route arrow / Tab events to the FTXUI container so both Menu
    // components can receive navigation keystrokes.
    if (e == ftxui::Event::ArrowUp || e == ftxui::Event::ArrowDown ||
        e == ftxui::Event::ArrowLeft || e == ftxui::Event::ArrowRight ||
        e == ftxui::Event::Tab || e == ftxui::Event::TabReverse) {
        return dialog_container_->OnEvent(e);
    }

    return false;
}

}  // namespace shmtu::cas::ocr::tui
