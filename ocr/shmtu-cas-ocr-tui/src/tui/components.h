// SPDX-License-Identifier: MIT
#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>

namespace shmtu::cas::ocr::tui {

// Helpers used by all three screen components.  They isolate the
// small FTXUI idioms (separator drawing, labelled panel wrapping,
// status messages) so individual screens stay short.
namespace tui_components {

// A labelled, bordered panel wrapping `inner`.
ftxui::Element panel(const std::string& title, ftxui::Element inner);

// A simple status line that highlights success / error.
ftxui::Element statusLine(const std::string& message, bool is_error);

// Make a horizontal bar visualisation for `value` in [0, 1].
ftxui::Element progressBar(float value, int width = 30);

}  // namespace tui_components

}  // namespace shmtu::cas::ocr::tui
