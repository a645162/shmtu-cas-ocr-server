// SPDX-License-Identifier: MIT
#include "screens/model_list.h"

#include "tui/components.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <sstream>
#include <utility>

namespace shmtu::cas::ocr::tui {

using tui_components::panel;
using tui_components::statusLine;

namespace {

std::string fmtMetric(const std::optional<double>& v, int precision = 1) {
    if (!v.has_value()) return "-";
    std::ostringstream oss;
    oss.precision(precision);
    oss << std::fixed << (*v * 100.0) << "%";
    return oss.str();
}

std::string fmtOptionalDouble(const std::optional<double>& v,
                              int precision = 2) {
    if (!v.has_value()) return "-";
    std::ostringstream oss;
    oss.precision(precision);
    oss << std::fixed << *v;
    return oss.str();
}

}  // namespace

ModelListScreen::ModelListScreen() {
    engine_choices_ = {"ncnn"};
    precision_choices_ = {"fp32", "fp16"};
}

void ModelListScreen::setManifest(
    const shmtu::cas::ocr::ReleaseManifest& manifest,
    const std::string& tag) {
    current_tag_ = tag;
    models_ = shmtu::cas::ocr::list_models(manifest);
    if (selected_model_ >= static_cast<int>(models_.size())) {
        selected_model_ = 0;
    }
    for (const auto* m : models_) {
        if (!m) continue;
        for (const auto& [engine, precisions] : m->artifacts) {
            if (std::find(engine_choices_.begin(), engine_choices_.end(),
                          engine) == engine_choices_.end()) {
                engine_choices_.push_back(engine);
            }
            for (const auto& [prec, _] : precisions) {
                if (std::find(precision_choices_.begin(),
                              precision_choices_.end(),
                              prec) == precision_choices_.end()) {
                    precision_choices_.push_back(prec);
                }
            }
        }
    }
    if (selected_engine_ >= static_cast<int>(engine_choices_.size())) {
        selected_engine_ = 0;
    }
    if (selected_precision_ >=
        static_cast<int>(precision_choices_.size())) {
        selected_precision_ = 0;
    }
}

void ModelListScreen::clear() {
    models_.clear();
    current_tag_.clear();
    selected_model_ = 0;
}

ftxui::Element ModelListScreen::Render() {
    if (models_.empty()) {
        ftxui::Elements rows;
        rows.push_back(ftxui::text("No manifest loaded.") | ftxui::dim);
        rows.push_back(ftxui::text("Select a release on the left.") |
                       ftxui::dim);
        return panel("Models", ftxui::vbox(std::move(rows)));
    }

    std::vector<std::vector<std::string>> rows;
    rows.push_back({"#", "Backbone", "Display", "Params(M)", "Val Acc",
                    "Test Acc", "Files", "Engines"});
    for (std::size_t i = 0; i < models_.size(); ++i) {
        const auto* m = models_[i];
        if (!m) continue;
        std::ostringstream idx;
        idx << i;
        std::ostringstream params;
        if (m->model_size_m.has_value()) {
            params.precision(2);
            params << std::fixed << *m->model_size_m;
        } else {
            params << "-";
        }
        std::ostringstream engines;
        bool first = true;
        for (const auto& [engine, precs] : m->artifacts) {
            if (!first) engines << ",";
            engines << engine;
            first = false;
        }
        std::ostringstream files;
        int total_files = 0;
        for (const auto& [engine, precs] : m->artifacts) {
            for (const auto& [prec, art] : precs) {
                total_files += static_cast<int>(art.files.size());
            }
        }
        files << total_files;
        std::string val_acc =
            m->metrics.has_value()
                ? fmtMetric(m->metrics->val_acc_expression)
                : "-";
        std::string test_acc =
            m->metrics.has_value()
                ? fmtMetric(m->metrics->test_acc_expression)
                : "-";
        rows.push_back({idx.str(), m->backbone, m->display_name,
                        params.str(), val_acc, test_acc, files.str(),
                        engines.str()});
    }
    auto table = ftxui::Table(rows);
    table.SelectRow(selected_model_).Border(ftxui::LIGHT);
    auto element = table.Render();

    ftxui::Element details;
    if (selected_model_ >= 0 &&
        selected_model_ < static_cast<int>(models_.size()) &&
        models_[selected_model_]) {
        const auto* m = models_[selected_model_];
        std::ostringstream det;
        det << "asset_stem: " << m->asset_stem
            << "  version: " << m->version << "  family: " << m->family;
        if (m->metrics.has_value()) {
            det << "\nval_loss: " << fmtOptionalDouble(m->metrics->val_loss)
                << "  test_loss: "
                << fmtOptionalDouble(m->metrics->test_loss);
        }
        details = ftxui::paragraph(det.str()) | ftxui::dim;
    } else {
        details = ftxui::text("");
    }

    // Compact right panel with help text only.  Engine/precision
    // selection is handled via the modal download dialog (triggered
    // by pressing 'd').
    ftxui::Elements control_rows;
    control_rows.push_back(ftxui::filler());
    control_rows.push_back(ftxui::text("Press 'd' to open download dialog.") |
                           ftxui::dim);
    ftxui::Element controls = ftxui::vbox(std::move(control_rows));

    ftxui::Elements left_rows;
    left_rows.push_back(element | ftxui::flex);
    left_rows.push_back(ftxui::separator());
    left_rows.push_back(details | ftxui::flex);
    ftxui::Element left = ftxui::vbox(std::move(left_rows)) | ftxui::flex;

    ftxui::Elements outer_rows;
    outer_rows.push_back(left);
    outer_rows.push_back(ftxui::separator());
    outer_rows.push_back(controls |
                         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 28));
    ftxui::Element outer = ftxui::hbox(std::move(outer_rows));

    return panel("Models - " + (current_tag_.empty() ? std::string("(no tag)")
                                                     : current_tag_),
                 outer);
}

ftxui::Component ModelListScreen::component() {
    return ftxui::Renderer([this] { return Render(); });
}

}  // namespace shmtu::cas::ocr::tui
