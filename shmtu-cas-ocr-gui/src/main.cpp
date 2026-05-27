#include <shmtu/cas_ocr/cas_ocr.h>

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <memory>

namespace {

class OcrFrame final : public wxFrame {
public:
    OcrFrame()
        : wxFrame(nullptr,
                  wxID_ANY,
                  "SHMTU CAS OCR GUI",
                  wxDefaultPosition,
                  wxSize(720, 420)),
          ocr_(std::make_unique<shmtu::cas_ocr::CasOcr>()) {
        auto* panel = new wxPanel(this);
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* hint = new wxStaticText(
            panel,
            wxID_ANY,
            "wxWidgets GUI placeholder. This target depends on SHMTU::CasOcrLib."
        );
        root->Add(hint, 0, wxALL | wxEXPAND, 12);

        auto* model_row = new wxBoxSizer(wxHORIZONTAL);
        model_row->Add(new wxStaticText(panel, wxID_ANY, "Model Dir:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        model_dir_ctrl_ = new wxTextCtrl(panel, wxID_ANY, "./models");
        model_row->Add(model_dir_ctrl_, 1, wxRIGHT, 8);
        auto* browse_model_btn = new wxButton(panel, wxID_ANY, "Browse");
        model_row->Add(browse_model_btn, 0);
        root->Add(model_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        auto* image_row = new wxBoxSizer(wxHORIZONTAL);
        image_row->Add(new wxStaticText(panel, wxID_ANY, "Image File:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        image_path_ctrl_ = new wxTextCtrl(panel, wxID_ANY, "");
        image_row->Add(image_path_ctrl_, 1, wxRIGHT, 8);
        auto* browse_image_btn = new wxButton(panel, wxID_ANY, "Browse");
        image_row->Add(browse_image_btn, 0);
        root->Add(image_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);

        auto* action_row = new wxBoxSizer(wxHORIZONTAL);
        auto* load_btn = new wxButton(panel, wxID_ANY, "Load Model");
        auto* predict_btn = new wxButton(panel, wxID_ANY, "Predict");
        action_row->Add(load_btn, 0, wxRIGHT, 8);
        action_row->Add(predict_btn, 0);
        root->Add(action_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

        status_ctrl_ = new wxTextCtrl(
            panel,
            wxID_ANY,
            "Model not loaded.",
            wxDefaultPosition,
            wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY
        );
        root->Add(status_ctrl_, 1, wxALL | wxEXPAND, 12);

        panel->SetSizer(root);

        browse_model_btn->Bind(wxEVT_BUTTON, &OcrFrame::OnBrowseModelDir, this);
        browse_image_btn->Bind(wxEVT_BUTTON, &OcrFrame::OnBrowseImageFile, this);
        load_btn->Bind(wxEVT_BUTTON, &OcrFrame::OnLoadModel, this);
        predict_btn->Bind(wxEVT_BUTTON, &OcrFrame::OnPredict, this);
    }

private:
    void OnBrowseModelDir(wxCommandEvent&) {
        wxDirDialog dialog(this, "Select model directory", model_dir_ctrl_->GetValue());
        if (dialog.ShowModal() == wxID_OK) {
            model_dir_ctrl_->SetValue(dialog.GetPath());
        }
    }

    void OnBrowseImageFile(wxCommandEvent&) {
        wxFileDialog dialog(
            this,
            "Select captcha image",
            wxEmptyString,
            wxEmptyString,
            "Image files (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );
        if (dialog.ShowModal() == wxID_OK) {
            image_path_ctrl_->SetValue(dialog.GetPath());
        }
    }

    void OnLoadModel(wxCommandEvent&) {
        const std::string model_dir = model_dir_ctrl_->GetValue().ToStdString();
        ocr_ = std::make_unique<shmtu::cas_ocr::CasOcr>(model_dir, false);

        if (!ocr_->load_model("fp16")) {
            status_ctrl_->SetValue("Failed to load model.");
            wxMessageBox("Failed to load NCNN model files.", "Load Model", wxOK | wxICON_ERROR, this);
            return;
        }

        status_ctrl_->SetValue("Model loaded successfully.");
    }

    void OnPredict(wxCommandEvent&) {
        if (!ocr_ || !ocr_->is_loaded()) {
            wxMessageBox("Load model first.", "Predict", wxOK | wxICON_WARNING, this);
            return;
        }

        const auto image_path = image_path_ctrl_->GetValue().ToStdString();
        if (image_path.empty()) {
            wxMessageBox("Select an image first.", "Predict", wxOK | wxICON_WARNING, this);
            return;
        }

        const auto result = ocr_->predict(image_path);
        if (!result.success) {
            status_ctrl_->SetValue("Prediction failed: " + wxString::FromUTF8(result.error));
            wxMessageBox("Prediction failed.", "Predict", wxOK | wxICON_ERROR, this);
            return;
        }

        wxString summary;
        summary << "Expression: " << wxString::FromUTF8(result.expression) << '\n'
                << "Result: " << result.result << '\n'
                << "Operator: " << result.op << '\n'
                << "Digits: " << result.digit1 << ", " << result.digit2;
        status_ctrl_->SetValue(summary);
    }

    wxTextCtrl* model_dir_ctrl_ = nullptr;
    wxTextCtrl* image_path_ctrl_ = nullptr;
    wxTextCtrl* status_ctrl_ = nullptr;
    std::unique_ptr<shmtu::cas_ocr::CasOcr> ocr_;
};

class OcrGuiApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new OcrFrame();
        frame->Show(true);
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(OcrGuiApp);
