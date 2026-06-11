// SPDX-License-Identifier: MIT


#pragma once

#include <shmtu/cas_ocr/manifest.h>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

// The middle screen: when a release is selected this shows the list
// of models declared in its manifest along with a metrics summary.
class ModelListScreen {
public:
    using ModelSelectedCb = std::function<void(int model_index)>;
    using DownloadRequestedCb = std::function<void(int model_index,
                                                   const std::string& engine,
                                                   const std::string& precision)>;

    ModelListScreen();

    void setManifest(const shmtu::cas::ocr::ReleaseManifest& manifest,
                     const std::string& tag);
    void clear();
    bool empty() const { return models_.empty(); }
    std::string currentTag() const { return current_tag_; }
    int selectedIndex() const { return selected_model_; }
    const std::vector<const shmtu::cas::ocr::ModelInfo*>& models() const {
        return models_;
    }

    ftxui::Element Render();
    ftxui::Component component();

    void setOnModelSelected(ModelSelectedCb cb) {
        on_model_selected_ = std::move(cb);
    }
    void setOnDownloadRequested(DownloadRequestedCb cb) {
        on_download_requested_ = std::move(cb);
    }

    // The currently selected (engine, precision) pair, or empty
    // string if none has been chosen.
    std::string selectedEngine() const { return engine_choices_[selected_engine_]; }
    std::string selectedPrecision() const { return precision_choices_[selected_precision_]; }

private:
    std::vector<const shmtu::cas::ocr::ModelInfo*> models_;
    std::string current_tag_;
    int selected_model_ = 0;
    int selected_engine_ = 0;
    int selected_precision_ = 0;
    std::vector<std::string> engine_choices_;
    std::vector<std::string> precision_choices_;

    ModelSelectedCb on_model_selected_;
    DownloadRequestedCb on_download_requested_;
};

}  // namespace shmtu::cas::ocr::tui
