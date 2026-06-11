// SPDX-License-Identifier: MIT
#include "tui/components.h"

#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <string>

namespace shmtu::cas::ocr::tui::tui_components {

ftxui::Element panel(const std::string& title, ftxui::Element inner) {
    ftxui::Elements header;
    header.push_back(ftxui::text("[" + title + "]") | ftxui::bold);
    header.push_back(ftxui::filler());
    ftxui::Elements rows;
    rows.push_back(ftxui::hbox(std::move(header)) |
                   ftxui::color(ftxui::Color::Cyan));
    rows.push_back(ftxui::separator());
    rows.push_back(inner | ftxui::flex);
    return ftxui::vbox(std::move(rows)) | ftxui::border;
}

ftxui::Element statusLine(const std::string& message, bool is_error) {
    if (message.empty()) {
        return ftxui::text("");
    }
    ftxui::Elements row;
    row.push_back(ftxui::text("[") | ftxui::dim);
    row.push_back(ftxui::text(is_error ? "ERR" : "OK") |
                  (is_error ? ftxui::color(ftxui::Color::Red)
                            : ftxui::color(ftxui::Color::Green)) |
                  ftxui::bold);
    row.push_back(ftxui::text("] ") | ftxui::dim);
    row.push_back(ftxui::text(message) | ftxui::flex);
    return ftxui::hbox(std::move(row));
}

ftxui::Element progressBar(float value, int width) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    int filled = static_cast<int>(value * static_cast<float>(width));
    if (filled > width) filled = width;
    std::string bar(static_cast<std::size_t>(filled), '#');
    bar.append(static_cast<std::size_t>(width - filled), '.');
    std::ostringstream oss;
    oss << bar << ' ' << static_cast<int>(value * 100.0f) << '%';
    return ftxui::text(oss.str());
}

}  // namespace shmtu::cas::ocr::tui::tui_components
