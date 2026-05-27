#include <shmtu/cas_ocr/cas_ocr.h>
#include <shmtu/cas_ocr/types.h>

#include <wx/aboutdlg.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/collpane.h>
#include <wx/colour.h>
#include <wx/dataobj.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/font.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/progdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/wx.h>

#include <curl/curl.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ============================================================================
// Constants
// ============================================================================

namespace {

constexpr auto APP_TITLE = "SHMTU CAS OCR";
constexpr auto APP_TITLE_CN = "海大验证码识别 - NCNN";
constexpr auto APP_VERSION = "2.0.0";
constexpr auto DEFAULT_MODEL_DIR = "./models";
constexpr auto DEFAULT_PRECISION = "fp16";
constexpr auto LOG_PATH = "/tmp/shmtu_cas_ocr_gui.log";

std::mutex g_log_mutex;
int g_log_fd = -1;

void WriteRawLogLine(const std::string& line) {
    if (g_log_fd >= 0) {
        (void)::write(g_log_fd, line.data(), line.size());
    }
    (void)::write(STDERR_FILENO, line.data(), line.size());
}

void InitLogging() {
    if (g_log_fd >= 0) {
        return;
    }
    g_log_fd = ::open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
}

void LogMessage(const std::string& message) {
    InitLogging();

    std::lock_guard<std::mutex> lock(g_log_mutex);

    std::time_t now = std::time(nullptr);
    std::tm tm_now {};
    localtime_r(&now, &tm_now);

    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "[%04d-%02d-%02d %02d:%02d:%02d] ",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    std::string line(prefix);
    line += message;
    line += '\n';
    WriteRawLogLine(line);
}

void SignalLogHandler(int sig) {
    const char* name = "UNKNOWN";
    switch (sig) {
        case SIGABRT: name = "SIGABRT"; break;
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGILL:  name = "SIGILL"; break;
        case SIGFPE:  name = "SIGFPE"; break;
#ifdef SIGBUS
        case SIGBUS:  name = "SIGBUS"; break;
#endif
        case SIGTERM: name = "SIGTERM"; break;
        default: break;
    }

    char buf[128];
    int len = std::snprintf(buf, sizeof(buf),
                            "[fatal] received signal %s (%d)\n", name, sig);
    if (len > 0) {
        if (g_log_fd >= 0) {
            (void)::write(g_log_fd, buf, static_cast<size_t>(len));
        }
        (void)::write(STDERR_FILENO, buf, static_cast<size_t>(len));
    }

    std::_Exit(128 + sig);
}

void InstallCrashHandlers() {
    InitLogging();
    std::signal(SIGABRT, SignalLogHandler);
    std::signal(SIGSEGV, SignalLogHandler);
    std::signal(SIGILL, SignalLogHandler);
    std::signal(SIGFPE, SignalLogHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, SignalLogHandler);
#endif
    std::signal(SIGTERM, SignalLogHandler);
}

struct CurlProgressPayload {
    std::function<bool(curl_off_t, curl_off_t)> callback;
};

size_t CurlWriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<std::ofstream*>(userdata);
    const auto total = size * nmemb;
    stream->write(ptr, static_cast<std::streamsize>(total));
    return stream->good() ? total : 0;
}

size_t CurlWriteToVector(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::vector<uint8_t>*>(userdata);
    const auto total = size * nmemb;
    const auto* begin = reinterpret_cast<uint8_t*>(ptr);
    buffer->insert(buffer->end(), begin, begin + total);
    return total;
}

int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* payload = static_cast<CurlProgressPayload*>(clientp);
    if (!payload || !payload->callback) {
        return 0;
    }
    return payload->callback(dlnow, dltotal) ? 0 : 1;
}

bool CurlDownload(const std::string& url,
                  curl_write_callback write_callback,
                  void* write_userdata,
                  std::function<bool(curl_off_t, curl_off_t)> progress_callback,
                  long timeout_seconds,
                  long& http_status,
                  std::string& error_message) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error_message = "curl_easy_init failed";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    CurlProgressPayload progress_payload{std::move(progress_callback)};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "shmtu-cas-ocr-gui/1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_userdata);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_payload);

    const auto code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    if (code != CURLE_OK) {
        error_message = errbuf[0] ? errbuf : curl_easy_strerror(code);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

bool DownloadUrlToFile(const std::string& url,
                       const std::string& filepath,
                       std::function<bool(curl_off_t, curl_off_t)> progress_callback,
                       long& http_status,
                       std::string& error_message) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) {
        error_message = "failed to open output file: " + filepath;
        return false;
    }

    const bool ok = CurlDownload(url, CurlWriteToFile, &ofs, std::move(progress_callback),
                                 300L, http_status, error_message);
    ofs.close();
    return ok;
}

bool DownloadUrlToMemory(const std::string& url,
                         std::vector<uint8_t>& output,
                         long& http_status,
                         std::string& error_message) {
    output.clear();
    return CurlDownload(url, CurlWriteToVector, &output, nullptr, 30L,
                        http_status, error_message);
}

// NCNN model download URLs
constexpr auto GITHUB_BASE_URL =
    "https://github.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN";
constexpr auto GITEE_BASE_URL =
    "https://gitee.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN";

// NCNN model files (precision placeholder will be replaced)
struct ModelFileInfo {
    const char* pattern;  // e.g. "resnet18_equal_symbol_latest.{precision}.param"
    const char* github_name;
    const char* gitee_name;
};

const ModelFileInfo NCNN_MODEL_FILES[] = {
    {"resnet18_equal_symbol_latest.%s.param", "resnet18_equal_symbol_latest.%s.param",
     "resnet18_equal_symbol_latest.%s.param"},
    {"resnet18_equal_symbol_latest.%s.bin", "resnet18_equal_symbol_latest.%s.bin",
     "resnet18_equal_symbol_latest.%s.bin"},
    {"resnet18_operator_latest.%s.param", "resnet18_operator_latest.%s.param",
     "resnet18_operator_latest.%s.param"},
    {"resnet18_operator_latest.%s.bin", "resnet18_operator_latest.%s.bin",
     "resnet18_operator_latest.%s.bin"},
    {"resnet34_digit_latest.%s.param", "resnet34_digit_latest.%s.param",
     "resnet34_digit_latest.%s.param"},
    {"resnet34_digit_latest.%s.bin", "resnet34_digit_latest.%s.bin",
     "resnet34_digit_latest.%s.bin"},
};

constexpr int NCNN_MODEL_FILE_COUNT = 6;

struct LaunchOptions {
    std::string model_dir = DEFAULT_MODEL_DIR;
    std::string precision = DEFAULT_PRECISION;
    bool use_gpu = false;
};

wxString U8(const char* text) {
    return wxString::FromUTF8(text);
}

// Colours
constexpr auto COLOR_ACCENT = "#1976D2";
constexpr auto COLOR_ACCENT_LIGHT = "#BBDEFB";
constexpr auto COLOR_SUCCESS = "#4CAF50";
constexpr auto COLOR_ERROR = "#F44336";
constexpr auto COLOR_WARNING = "#FF9800";
constexpr auto COLOR_DIM_TEXT = "#757575";
constexpr auto COLOR_CARD_BG = "#FFFFFF";
constexpr auto COLOR_SECTION_LABEL = "#424242";
constexpr auto COLOR_TOP_BAR_BG = "#F5F5F5";
constexpr auto COLOR_BOTTOM_BAR_BG = "#F5F5F5";

// Widget IDs
enum {
    ID_BTN_CHECK_DOWNLOAD = wxID_HIGHEST + 1,
    ID_BTN_DOWNLOAD_CAPTCHA,
    ID_BTN_OPEN_LOCAL,
    ID_BTN_OCR_RECOGNIZE,
    ID_BTN_ADD_TO_BATCH,
    ID_BTN_RELEASE_MODEL,
    ID_BTN_BATCH_RECOGNIZE,
    ID_BTN_BATCH_SELECT_FILES,
    ID_BTN_BATCH_CLEAR,
    ID_LIST_BATCH,
    ID_CHOICE_PRECISION,
    ID_CHECKBOX_GPU,
    ID_GAUGE_DOWNLOAD,
    ID_THREAD_DOWNLOAD,
    ID_EXPANDER_BATCH,
};

}  // anonymous namespace

// Custom event for download progress (must be at file scope for linkage)
wxDECLARE_EVENT(wxEVT_DOWNLOAD_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_DOWNLOAD_COMPLETE, wxThreadEvent);

wxDEFINE_EVENT(wxEVT_DOWNLOAD_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_DOWNLOAD_COMPLETE, wxThreadEvent);

// ============================================================================
// Helper: build a model filename from precision
// ============================================================================

namespace {

std::string ModelFileName(const char* pattern, const std::string& precision) {
    char buf[256];
    snprintf(buf, sizeof(buf), pattern, precision.c_str());
    return std::string(buf);
}

wxString OperatorToString(int op) {
    switch (static_cast<shmtu::cas_ocr::Operator>(op)) {
        case shmtu::cas_ocr::Operator::Add:    return "+";
        case shmtu::cas_ocr::Operator::AddCHS: return "+ (中文)";
        case shmtu::cas_ocr::Operator::Sub:    return "-";
        case shmtu::cas_ocr::Operator::SubCHS: return "- (中文)";
        case shmtu::cas_ocr::Operator::Mul:    return "*";
        case shmtu::cas_ocr::Operator::MulCHS: return "* (中文)";
        default:                                return wxString::Format("? (%d)", op);
    }
}

}  // anonymous namespace

// ============================================================================
// MainFrame — the top-level window
// ============================================================================

class MainFrame final : public wxFrame {
public:
    explicit MainFrame(const LaunchOptions& launch_options = LaunchOptions{})
        : wxFrame(nullptr, wxID_ANY, U8(APP_TITLE_CN), wxDefaultPosition, wxSize(980, 720)),
          launch_options_(launch_options),
          ocr_(std::make_unique<shmtu::cas_ocr::CasOcr>()),
          model_loaded_(false),
          download_active_(false) {
        SetMinSize(wxSize(820, 600));

        BuildMenuBar();
        BuildTopBar();
        BuildMainArea();
        BuildBottomBar();

        // Layout: Top + Main + Bottom using vertical sizer
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(top_bar_panel_, 0, wxEXPAND);
        root->Add(main_panel_, 1, wxEXPAND);
        root->Add(bottom_bar_panel_, 0, wxEXPAND);
        SetSizer(root);

        Layout();
        CentreOnScreen();

        // Bind download thread events
        Bind(wxEVT_DOWNLOAD_PROGRESS, &MainFrame::OnDownloadProgress, this);
        Bind(wxEVT_DOWNLOAD_COMPLETE, &MainFrame::OnDownloadComplete, this);

        UpdateModelStatusUI();
    }

    ~MainFrame() override {
        // Ensure download thread is not running
        download_active_ = false;
    }

private:
    // =======================================================================
    // Menu bar
    // =======================================================================
    void BuildMenuBar() {
        auto* menuBar = new wxMenuBar();

        auto* fileMenu = new wxMenu();
        fileMenu->Append(wxID_EXIT, U8("退出\tAlt+F4"));
        menuBar->Append(fileMenu, U8("文件"));

        auto* helpMenu = new wxMenu();
        helpMenu->Append(wxID_ABOUT, U8("关于"));
        menuBar->Append(helpMenu, U8("帮助"));

        SetMenuBar(menuBar);

        Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
        Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    }

    // =======================================================================
    // Top bar: model directory + check/download + badge + progress
    // =======================================================================
    void BuildTopBar() {
        top_bar_panel_ = new wxPanel(this, wxID_ANY);
        auto* group = new wxStaticBoxSizer(wxVERTICAL, top_bar_panel_, U8("模型"));
        auto* group_box = group->GetStaticBox();

        auto* row1 = new wxBoxSizer(wxHORIZONTAL);

        auto* lblModelDir = new wxStaticText(group_box, wxID_ANY, U8("模型目录："));
        auto lblFont = lblModelDir->GetFont();
        lblFont.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        lblModelDir->SetFont(lblFont);
        row1->Add(lblModelDir, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        txt_model_dir_ = new wxTextCtrl(group_box, wxID_ANY,
                                        launch_options_.model_dir.empty()
                                            ? DEFAULT_MODEL_DIR
                                            : launch_options_.model_dir,
                                        wxDefaultPosition, wxSize(520, -1));
        row1->Add(txt_model_dir_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        btn_check_download_ = new wxButton(group_box, ID_BTN_CHECK_DOWNLOAD,
                                           U8("检查 / 下载模型"));
        row1->Add(btn_check_download_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        lbl_model_status_ = new wxStaticText(group_box, wxID_ANY, U8("模型未就绪"));
        auto badgeFont = lbl_model_status_->GetFont();
        badgeFont.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        lbl_model_status_->SetFont(badgeFont);
        row1->Add(lbl_model_status_, 0, wxALIGN_CENTER_VERTICAL);

        group->Add(row1, 0, wxEXPAND | wxALL, 6);

        gauge_download_ = new wxGauge(group_box, ID_GAUGE_DOWNLOAD, 100,
                                      wxDefaultPosition, wxDefaultSize);
        gauge_download_->SetValue(0);
        group->Add(gauge_download_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        top_bar_panel_->SetSizer(group);

        Bind(wxEVT_BUTTON, &MainFrame::OnCheckDownloadModels, this, ID_BTN_CHECK_DOWNLOAD);
    }

    // =======================================================================
    // Main area: left (preview + result) + right (buttons) + batch expander
    // =======================================================================
    void BuildMainArea() {
        main_panel_ = new wxPanel(this, wxID_ANY);

        auto* mainSizer = new wxBoxSizer(wxVERTICAL);

        // Grid: left preview area + right button column
        auto* gridSizer = new wxBoxSizer(wxHORIZONTAL);

        // --- Left column: preview + result ---
        BuildLeftColumn(gridSizer);

        // --- Right column: buttons ---
        BuildRightColumn(gridSizer);

        mainSizer->Add(gridSizer, 1, wxEXPAND | wxALL, 8);

        // --- Batch expander ---
        BuildBatchExpander(mainSizer);

        main_panel_->SetSizer(mainSizer);
    }

    void BuildLeftColumn(wxSizer* parent) {
        auto* leftPanel = new wxPanel(main_panel_, wxID_ANY);
        auto* borderSizer = new wxStaticBoxSizer(wxVERTICAL, leftPanel, U8("预览与结果"));
        auto* borderBox = borderSizer->GetStaticBox();

        // Image preview
        img_preview_ = new wxStaticBitmap(borderBox, wxID_ANY, wxBitmap(),
                                          wxDefaultPosition, wxSize(480, 260),
                                          wxBORDER_THEME);
        img_preview_->SetMinSize(wxSize(480, 260));
        borderSizer->Add(img_preview_, 1, wxEXPAND | wxALL, 8);

        // Source path text (dim)
        lbl_source_path_ = new wxStaticText(borderBox, wxID_ANY, U8("未选择图片"));
        auto dimFont = lbl_source_path_->GetFont();
        dimFont.SetPointSize(dimFont.GetPointSize() - 1);
        lbl_source_path_->SetFont(dimFont);
        borderSizer->Add(lbl_source_path_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        auto* resultSizer = new wxStaticBoxSizer(wxVERTICAL, leftPanel, U8("识别结果"));
        auto* resultBox = resultSizer->GetStaticBox();

        lbl_result_expr_ = new wxStaticText(resultBox, wxID_ANY, U8("（暂无识别结果）"));
        auto resultFont = lbl_result_expr_->GetFont();
        resultFont.SetPointSize(34);
        resultFont.SetWeight(wxFONTWEIGHT_BOLD);
        lbl_result_expr_->SetFont(resultFont);
        resultSizer->Add(lbl_result_expr_, 0, wxEXPAND | wxALL, 8);

        lbl_elapsed_ms_ = new wxStaticText(resultBox, wxID_ANY, U8("用时：0.0 毫秒"));
        auto dimFont3 = lbl_elapsed_ms_->GetFont();
        dimFont3.SetPointSize(dimFont3.GetPointSize() - 1);
        lbl_elapsed_ms_->SetFont(dimFont3);
        resultSizer->Add(lbl_elapsed_ms_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        borderSizer->Add(resultSizer, 0, wxEXPAND | wxALL, 8);
        leftPanel->SetSizer(borderSizer);
        parent->Add(leftPanel, 1, wxEXPAND | wxRIGHT, 8);
    }

    void BuildRightColumn(wxSizer* parent) {
        auto* rightPanel = new wxPanel(main_panel_, wxID_ANY);
        auto* borderSizer = new wxStaticBoxSizer(wxVERTICAL, rightPanel, U8("操作"));
        auto* rightBox = borderSizer->GetStaticBox();

        auto* rightSizer = new wxBoxSizer(wxVERTICAL);
        rightSizer->AddSpacer(8);

        // Section: 获取图片
        auto* lblGetImage = new wxStaticText(rightBox, wxID_ANY, U8("获取图片"));
        auto sectionFont = lblGetImage->GetFont();
        sectionFont.SetPointSize(sectionFont.GetPointSize() + 0);
        sectionFont.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        lblGetImage->SetFont(sectionFont);
        rightSizer->Add(lblGetImage, 0, wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(6);

        txt_captcha_url_ = new wxTextCtrl(rightBox, wxID_ANY,
                                          "https://cas.shmtu.edu.cn/cas/captcha",
                                          wxDefaultPosition, wxSize(220, -1));
        txt_captcha_url_->SetHint(U8("验证码 URL"));
        rightSizer->Add(txt_captcha_url_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(6);

        btn_download_captcha_ = new wxButton(rightBox, ID_BTN_DOWNLOAD_CAPTCHA,
                                             U8("下载验证码"));
        btn_download_captcha_->SetMinSize(wxSize(220, -1));
        rightSizer->Add(btn_download_captcha_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(4);

        btn_open_local_ = new wxButton(rightBox, ID_BTN_OPEN_LOCAL, U8("打开本地图片"));
        btn_open_local_->SetMinSize(wxSize(220, -1));
        rightSizer->Add(btn_open_local_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Separator
        auto* sep1 = new wxStaticLine(rightBox, wxID_ANY);
        rightSizer->Add(sep1, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Section: 识别
        auto* lblRecognize = new wxStaticText(rightBox, wxID_ANY, U8("识别"));
        lblRecognize->SetFont(sectionFont);
        rightSizer->Add(lblRecognize, 0, wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(6);

        btn_ocr_recognize_ = new wxButton(rightBox, ID_BTN_OCR_RECOGNIZE,
                                          U8("▶ OCR 识别"));
        btn_ocr_recognize_->SetMinSize(wxSize(220, 50));
        auto ocrBtnFont = btn_ocr_recognize_->GetFont();
        ocrBtnFont.SetPointSize(20);
        ocrBtnFont.SetWeight(wxFONTWEIGHT_BOLD);
        btn_ocr_recognize_->SetFont(ocrBtnFont);
        rightSizer->Add(btn_ocr_recognize_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Separator
        auto* sep2 = new wxStaticLine(rightBox, wxID_ANY);
        rightSizer->Add(sep2, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Section: 批量
        auto* lblBatch = new wxStaticText(rightBox, wxID_ANY, U8("批量"));
        lblBatch->SetFont(sectionFont);
        rightSizer->Add(lblBatch, 0, wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(6);

        btn_add_to_batch_ = new wxButton(rightBox, ID_BTN_ADD_TO_BATCH,
                                         U8("加入批量列表"));
        btn_add_to_batch_->SetMinSize(wxSize(220, -1));
        rightSizer->Add(btn_add_to_batch_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Separator
        auto* sep3 = new wxStaticLine(rightBox, wxID_ANY);
        rightSizer->Add(sep3, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddSpacer(8);

        // Release model
        btn_release_model_ = new wxButton(rightBox, ID_BTN_RELEASE_MODEL, U8("释放模型"));
        btn_release_model_->SetMinSize(wxSize(220, -1));
        rightSizer->Add(btn_release_model_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        rightSizer->AddStretchSpacer(1);

        // Author
        auto* lblAuthor = new wxStaticText(rightBox, wxID_ANY, "Author: Haomin Kong");
        auto authorFont = lblAuthor->GetFont();
        authorFont.SetPointSize(authorFont.GetPointSize() - 2);
        lblAuthor->SetFont(authorFont);
        rightSizer->Add(lblAuthor, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);

        borderSizer->Add(rightSizer, 1, wxEXPAND);
        rightPanel->SetSizer(borderSizer);
        rightPanel->SetMinSize(wxSize(240, -1));
        rightPanel->SetMaxSize(wxSize(260, -1));

        parent->Add(rightPanel, 0, wxEXPAND);

        // Bind button events
        Bind(wxEVT_BUTTON, &MainFrame::OnDownloadCaptcha, this, ID_BTN_DOWNLOAD_CAPTCHA);
        Bind(wxEVT_BUTTON, &MainFrame::OnOpenLocalImage, this, ID_BTN_OPEN_LOCAL);
        Bind(wxEVT_BUTTON, &MainFrame::OnOcrRecognize, this, ID_BTN_OCR_RECOGNIZE);
        Bind(wxEVT_BUTTON, &MainFrame::OnAddToBatch, this, ID_BTN_ADD_TO_BATCH);
        Bind(wxEVT_BUTTON, &MainFrame::OnReleaseModel, this, ID_BTN_RELEASE_MODEL);
    }

    // =======================================================================
    // Bottom bar: status message
    // =======================================================================
    void BuildBottomBar() {
        bottom_bar_panel_ = new wxPanel(this, wxID_ANY);
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->AddSpacer(8);

        lbl_status_ = new wxStaticText(bottom_bar_panel_, wxID_ANY, U8("等待操作"));
        auto statusFont = lbl_status_->GetFont();
        statusFont.SetPointSize(statusFont.GetPointSize());
        lbl_status_->SetFont(statusFont);
        sizer->Add(lbl_status_, 1, wxALIGN_CENTER_VERTICAL);

        // Precision choice in bottom bar
        sizer->Add(new wxStaticText(bottom_bar_panel_, wxID_ANY, U8("精度:")),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        choice_precision_ = new wxChoice(bottom_bar_panel_, ID_CHOICE_PRECISION,
                                           wxDefaultPosition, wxDefaultSize,
                                           {"fp16", "fp32"});
        choice_precision_->SetStringSelection(
            launch_options_.precision == "fp32" ? "fp32" : "fp16");
        sizer->Add(choice_precision_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

        // GPU checkbox
        chk_use_gpu_ = new wxCheckBox(bottom_bar_panel_, ID_CHECKBOX_GPU, U8("GPU加速"));
        chk_use_gpu_->SetValue(launch_options_.use_gpu);
        sizer->Add(chk_use_gpu_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

        bottom_bar_panel_->SetSizerAndFit(sizer);
    }

    // =======================================================================
    // Batch expander (collapsible)
    // =======================================================================
    void BuildBatchExpander(wxSizer* parent) {
        batch_pane_ = new wxCollapsiblePane(main_panel_, wxID_ANY, U8("批量识别 / 批量比对"));
        auto* batch_content = batch_pane_->GetPane();
        auto* contentSizer = new wxStaticBoxSizer(wxVERTICAL, batch_content, U8("批量列表"));
        auto* contentBox = contentSizer->GetStaticBox();

        // Button row
        auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
        btn_batch_select_ = new wxButton(contentBox, ID_BTN_BATCH_SELECT_FILES,
                                         U8("选择多张本地图片..."));
        btnRow->Add(btn_batch_select_, 0, wxRIGHT, 8);

        btn_batch_recognize_ = new wxButton(contentBox, ID_BTN_BATCH_RECOGNIZE,
                                            U8("批量识别"));
        btnRow->Add(btn_batch_recognize_, 0, wxRIGHT, 8);

        btn_batch_clear_ = new wxButton(contentBox, ID_BTN_BATCH_CLEAR, U8("清空列表"));
        btnRow->Add(btn_batch_clear_, 0, wxRIGHT, 8);

        lbl_batch_stats_ = new wxStaticText(contentBox, wxID_ANY, U8("共 0 项 · 平均 0.0 毫秒"));
        btnRow->Add(lbl_batch_stats_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

        contentSizer->Add(btnRow, 0, wxEXPAND | wxALL, 8);

        contentSizer->AddSpacer(4);

        // Batch list
        list_batch_ = new wxListCtrl(contentBox, ID_LIST_BATCH,
                                     wxDefaultPosition, wxSize(-1, 220),
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME);
        list_batch_->AppendColumn("#", wxLIST_FORMAT_LEFT, 36);
        list_batch_->AppendColumn(U8("来源"), wxLIST_FORMAT_LEFT, 220);
        list_batch_->AppendColumn(U8("结果"), wxLIST_FORMAT_LEFT, 160);
        list_batch_->AppendColumn(U8("状态"), wxLIST_FORMAT_CENTER, 80);
        list_batch_->AppendColumn(U8("耗时(ms)"), wxLIST_FORMAT_CENTER, 90);
        contentSizer->Add(list_batch_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        batch_content->SetSizer(contentSizer);
        batch_pane_->Collapse(true);
        parent->Add(batch_pane_, 0, wxEXPAND | wxTOP, 8);

        batch_pane_->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent&) {
            Layout();
        });

        Bind(wxEVT_BUTTON, &MainFrame::OnBatchSelectFiles, this, ID_BTN_BATCH_SELECT_FILES);
        Bind(wxEVT_BUTTON, &MainFrame::OnBatchRecognize, this, ID_BTN_BATCH_RECOGNIZE);
        Bind(wxEVT_BUTTON, &MainFrame::OnBatchClear, this, ID_BTN_BATCH_CLEAR);
    }

    // =======================================================================
    // Event handlers
    // =======================================================================

    void OnExit(wxCommandEvent&) { Close(true); }

    void OnAbout(wxCommandEvent&) {
        wxAboutDialogInfo info;
        info.SetName(APP_TITLE_CN);
        info.SetVersion(APP_VERSION);
        info.SetDescription("上海海事大学 CAS 验证码 OCR 识别工具\n"
                            "基于 NCNN 推理引擎，支持 CPU / Vulkan GPU 加速");
        info.SetCopyright("(C) SHMTU Development Team");
        info.AddDeveloper("Haomin Kong");
        wxAboutBox(info, this);
    }

    // ---- Model check / download ----

    void OnCheckDownloadModels(wxCommandEvent&) {
        LogMessage("OnCheckDownloadModels: entered");
        if (download_active_) {
            LogMessage("OnCheckDownloadModels: download already active");
            wxMessageBox("模型正在下载中，请稍候...", "下载", wxOK | wxICON_INFORMATION, this);
            return;
        }

        const auto model_dir = txt_model_dir_->GetValue().ToStdString();
        const auto precision = choice_precision_->GetStringSelection().ToStdString();
        LogMessage("OnCheckDownloadModels: model_dir=" + model_dir + ", precision=" + precision);

        // Check which files are missing
        std::vector<std::string> missing_files;
        for (int i = 0; i < NCNN_MODEL_FILE_COUNT; ++i) {
            auto filename = ModelFileName(NCNN_MODEL_FILES[i].pattern, precision);
            auto filepath = std::filesystem::path(model_dir) / filename;
            if (!std::filesystem::exists(filepath)) {
                missing_files.push_back(filename);
            }
        }

        if (missing_files.empty()) {
            LogMessage("OnCheckDownloadModels: models already present, loading");
            SetStatusText("所有模型文件已就绪，正在加载模型...");

            // Auto-load model
            LoadModelFromCurrentSettings();
            return;
        }

        // Need to download missing files
        wxString msg = "缺少以下模型文件：\n\n";
        for (const auto& f : missing_files) {
            msg << wxString::FromUTF8(f) << "\n";
        }
        LogMessage("OnCheckDownloadModels: missing_files=" + std::to_string(missing_files.size()));
        msg << "\n是否从 Gitee 下载？（国内推荐）";

        int answer = wxMessageBox(msg, "下载模型", wxYES_NO | wxICON_QUESTION, this);
        LogMessage("OnCheckDownloadModels: prompt answer=" + std::to_string(answer));
        if (answer != wxYES) return;

        // Start download in background thread
        StartModelDownload(missing_files, true /* use gitee first */);
    }

    void StartModelDownload(const std::vector<std::string>& missing_files, bool use_gitee) {
        LogMessage("StartModelDownload: missing_files=" + std::to_string(missing_files.size()) +
                   ", use_gitee=" + std::string(use_gitee ? "true" : "false"));
        download_active_ = true;
        btn_check_download_->Disable();
        gauge_download_->SetValue(0);
        SetStatusText("正在下载模型文件...");

        const auto model_dir = txt_model_dir_->GetValue().ToStdString();
        const auto precision = choice_precision_->GetStringSelection().ToStdString();
        LogMessage("StartModelDownload: model_dir=" + model_dir + ", precision=" + precision);
        const int total_files = static_cast<int>(missing_files.size());
        int completed_files = 0;
        bool all_ok = true;
        wxString error_msg;

        wxProgressDialog progress_dialog(
            U8("下载模型"),
            U8("正在下载模型文件..."),
            total_files,
            this,
            wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);

        try {
            std::filesystem::create_directories(model_dir);
        } catch (const std::exception& e) {
            const std::string err_msg = e.what();
            LogMessage("StartModelDownload: create_directories failed: " + err_msg);
            SetStatusText(wxString::Format("创建目录失败: %s", err_msg.c_str()));
            download_active_ = false;
            btn_check_download_->Enable();
            return;
        }

        for (const auto& filename : missing_files) {
            if (!download_active_) {
                break;
            }

            LogMessage("StartModelDownload: downloading " + filename);
            progress_dialog.Update(completed_files, U8("正在下载：") + wxString::FromUTF8(filename));

            auto filepath = (std::filesystem::path(model_dir) / filename).string();
            bool download_ok = false;

            auto try_download = [&](const std::string& base_url) -> bool {
                const auto url = base_url + "/" + filename;
                long http_status = 0;
                std::string curl_error;

                LogMessage("StartModelDownload: requesting " + url);
                const bool ok = DownloadUrlToFile(url, filepath, nullptr, http_status, curl_error);
                if (ok && http_status == 200) {
                    LogMessage("StartModelDownload: wrote " + filepath);
                    return true;
                }

                LogMessage("StartModelDownload: curl failure, status=" +
                           std::to_string(http_status) + ", error=" + curl_error);
                std::error_code ec;
                std::filesystem::remove(filepath, ec);
                return false;
            };

            std::string gitee_url(GITEE_BASE_URL);
            std::string github_url(GITHUB_BASE_URL);

            if (use_gitee) {
                download_ok = try_download(gitee_url);
                if (!download_ok) {
                    download_ok = try_download(github_url);
                }
            } else {
                download_ok = try_download(github_url);
                if (!download_ok) {
                    download_ok = try_download(gitee_url);
                }
            }

            if (!download_ok) {
                LogMessage("StartModelDownload: all sources failed for " + filename);
                error_msg += wxString::Format("下载失败: %s\n", filename.c_str());
                all_ok = false;
            }

            completed_files++;
            gauge_download_->SetValue((completed_files * 100) / total_files);
            progress_dialog.Update(completed_files);
        }

        download_active_ = false;
        btn_check_download_->Enable();

        if (all_ok) {
            gauge_download_->SetValue(100);
            SetStatusText("模型下载完成，正在加载模型...");
            LogMessage("StartModelDownload: completed successfully");
            LoadModelFromCurrentSettings();
        } else {
            gauge_download_->SetValue(0);
            SetStatusText("模型下载失败");
            LogMessage("StartModelDownload: completed with errors: " + error_msg.ToStdString());
            wxMessageBox("部分模型文件下载失败：\n\n" + error_msg,
                         "下载失败", wxOK | wxICON_ERROR, this);
        }
    }

    void OnDownloadProgress(wxThreadEvent& evt) {
        int pct = evt.GetInt();
        LogMessage("OnDownloadProgress: pct=" + std::to_string(pct));
        gauge_download_->SetValue(pct);
        SetStatusText(wxString::Format("正在下载模型文件... %d%%", pct));
    }

    void OnDownloadComplete(wxThreadEvent& evt) {
        LogMessage("OnDownloadComplete: entered");
        download_active_ = false;
        btn_check_download_->Enable();

        bool all_ok = (evt.GetInt() == 1);
        wxString error_msg = evt.GetString();
        LogMessage("OnDownloadComplete: all_ok=" + std::string(all_ok ? "true" : "false"));

        if (all_ok) {
            gauge_download_->SetValue(100);
            SetStatusText("模型下载完成，正在加载模型...");

            // Auto-load model
            LoadModelFromCurrentSettings();
        } else {
            gauge_download_->SetValue(0);
            SetStatusText("模型下载失败");
            LogMessage("OnDownloadComplete: error_msg=" + error_msg.ToStdString());
            wxMessageBox("部分模型文件下载失败：\n\n" + error_msg,
                         "下载失败", wxOK | wxICON_ERROR, this);
        }
    }

    // ---- Model loading / release ----

    void LoadModelFromCurrentSettings() {
        const auto model_dir = txt_model_dir_->GetValue().ToStdString();
        const auto precision = choice_precision_->GetStringSelection().ToStdString();
        const bool use_gpu = chk_use_gpu_->GetValue();
        LogMessage("LoadModelFromCurrentSettings: model_dir=" + model_dir +
                   ", precision=" + precision +
                   ", use_gpu=" + std::string(use_gpu ? "true" : "false"));

        ocr_ = std::make_unique<shmtu::cas_ocr::CasOcr>(model_dir, use_gpu);
        if (!ocr_->load_model(precision.empty() ? "fp16" : precision)) {
            LogMessage("LoadModelFromCurrentSettings: load_model failed");
            SetStatusText("模型加载失败");
            wxMessageBox("无法加载 NCNN 模型文件，请检查模型目录路径。\n\n"
                         "目录应包含 .param 和 .bin 文件。",
                         "加载模型", wxOK | wxICON_ERROR, this);

            model_loaded_ = false;
            UpdateModelStatusUI();
            return;
        }

        model_loaded_ = true;
        LogMessage("LoadModelFromCurrentSettings: load_model succeeded");
        UpdateModelStatusUI();
    }

    void OnReleaseModel(wxCommandEvent&) {
        if (ocr_) {
            ocr_->release();
        }
        model_loaded_ = false;
        UpdateModelStatusUI();
        SetStatusText("模型已释放");
    }

    void UpdateModelStatusUI() {
        LogMessage("UpdateModelStatusUI: model_loaded=" +
                   std::string(model_loaded_ ? "true" : "false"));
        if (model_loaded_) {
            LogMessage("UpdateModelStatusUI: setting model status label -> ready");
            lbl_model_status_->SetLabelText(U8("模型已就绪"));

            LogMessage("UpdateModelStatusUI: enabling recognize/add/batch buttons");
            btn_ocr_recognize_->Enable();
            btn_add_to_batch_->Enable();
            btn_batch_recognize_->Enable();
        } else {
            LogMessage("UpdateModelStatusUI: setting model status label -> not ready");
            lbl_model_status_->SetLabelText(U8("模型未就绪"));

            LogMessage("UpdateModelStatusUI: disabling recognize/add/batch buttons");
            btn_ocr_recognize_->Disable();
            btn_add_to_batch_->Disable();
            btn_batch_recognize_->Disable();
        }
        LogMessage("UpdateModelStatusUI: completed");
    }

    // ---- Image acquisition ----

    void OnDownloadCaptcha(wxCommandEvent&) {
        LogMessage("OnDownloadCaptcha: entered");
        auto url = txt_captcha_url_->GetValue().ToStdString();
        if (url.empty()) {
            LogMessage("OnDownloadCaptcha: empty url");
            wxMessageBox("请输入验证码 URL。", "下载验证码", wxOK | wxICON_WARNING, this);
            return;
        }

        LogMessage("OnDownloadCaptcha: url=" + url);
        SetStatusText("正在下载验证码...");

        try {
            // Parse URL
            std::string host;
            std::string path;
            bool use_https = false;

            if (url.find("https://") == 0) {
                use_https = true;
                auto after = url.substr(8);
                auto slash_pos = after.find('/');
                if (slash_pos == std::string::npos) {
                    host = after;
                    path = "/";
                } else {
                    host = after.substr(0, slash_pos);
                    path = after.substr(slash_pos);
                }
            } else if (url.find("http://") == 0) {
                auto after = url.substr(7);
                auto slash_pos = after.find('/');
                if (slash_pos == std::string::npos) {
                    host = after;
                    path = "/";
                } else {
                    host = after.substr(0, slash_pos);
                    path = after.substr(slash_pos);
                }
            } else {
                LogMessage("OnDownloadCaptcha: invalid url format");
                wxMessageBox("URL 格式不正确，请以 http:// 或 https:// 开头。",
                             "下载验证码", wxOK | wxICON_WARNING, this);
                return;
            }

            std::vector<uint8_t> image_data;
            LogMessage("OnDownloadCaptcha: host=" + host + ", path=" + path +
                       ", https=" + std::string(use_https ? "true" : "false"));

            long http_status = 0;
            std::string curl_error;
            const bool ok = DownloadUrlToMemory(url, image_data, http_status, curl_error);
            if (ok && http_status == 200) {
                LogMessage("OnDownloadCaptcha: download ok, bytes=" +
                           std::to_string(image_data.size()));
            } else {
                LogMessage("OnDownloadCaptcha: curl failure, status=" +
                           std::to_string(http_status) + ", error=" + curl_error);
            }

            if (image_data.empty()) {
                LogMessage("OnDownloadCaptcha: image_data empty");
                SetStatusText("下载验证码失败");
                wxMessageBox("无法下载验证码图片，请检查 URL。",
                             "下载验证码", wxOK | wxICON_ERROR, this);
                return;
            }

            // Save to temp file and display
            auto temp_dir = std::filesystem::temp_directory_path();
            auto temp_file = temp_dir / "captcha_download.png";
            {
                std::ofstream ofs(temp_file.string(), std::ios::binary);
                ofs.write(reinterpret_cast<const char*>(image_data.data()),
                          image_data.size());
            }
            LogMessage("OnDownloadCaptcha: temp file saved to " + temp_file.string());

            current_image_path_ = temp_file.string();
            current_image_data_ = std::move(image_data);
            DisplayImage(wxString::FromUTF8(temp_file.string()));
            lbl_source_path_->SetLabelText(wxString::FromUTF8(url));
            SetStatusText("验证码下载成功");
            LogMessage("OnDownloadCaptcha: completed");

        } catch (const std::exception& e) {
            LogMessage("OnDownloadCaptcha: exception: " + std::string(e.what()));
            SetStatusText("下载验证码异常");
            wxMessageBox(wxString::Format("下载失败: %s", e.what()),
                         "下载验证码", wxOK | wxICON_ERROR, this);
        }
    }

    void OnOpenLocalImage(wxCommandEvent&) {
        wxFileDialog dialog(this, "选择验证码图片", wxEmptyString, wxEmptyString,
                            "图片文件 (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|所有文件 (*.*)|*.*",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK) {
            current_image_path_ = dialog.GetPath().ToStdString();
            current_image_data_.clear();
            DisplayImage(dialog.GetPath());
            lbl_source_path_->SetLabelText(dialog.GetPath());
            SetStatusText("已加载本地图片");
        }
    }

    // ---- OCR recognition ----

    void OnOcrRecognize(wxCommandEvent&) {
        LogMessage("OnOcrRecognize: entered");
        if (!EnsureModelLoaded()) return;
        if (current_image_path_.empty()) {
            LogMessage("OnOcrRecognize: no current image");
            wxMessageBox("请先获取验证码图片。", "OCR 识别", wxOK | wxICON_WARNING, this);
            return;
        }

        LogMessage("OnOcrRecognize: image_path=" + current_image_path_ +
                   ", bytes=" + std::to_string(current_image_data_.size()));
        SetStatusText("正在识别...");

        const auto start = std::chrono::steady_clock::now();
        shmtu::cas_ocr::PredictResult result;

        if (!current_image_data_.empty()) {
            result = ocr_->predict(current_image_data_);
        } else {
            result = ocr_->predict(current_image_path_);
        }

        const auto end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        LogMessage("OnOcrRecognize: finished in " + std::to_string(elapsed_ms) + " ms");
        LogMessage("OnOcrRecognize: result.success=" +
                   std::string(result.success ? "true" : "false") +
                   ", expression=" + result.expression +
                   ", error=" + result.error);

        LogMessage("OnOcrRecognize: entering DisplayResult");
        DisplayResult(result, elapsed_ms);
        LogMessage("OnOcrRecognize: DisplayResult returned");
    }

    // ---- Batch operations ----

    struct BatchItem {
        std::string file_path;
        wxString source_name;
        wxString result_expr;
        wxString status;
        double elapsed_ms = 0.0;
        wxBitmap thumbnail;
    };

    void OnAddToBatch(wxCommandEvent&) {
        if (current_image_path_.empty()) {
            wxMessageBox("请先获取验证码图片。", "批量", wxOK | wxICON_WARNING, this);
            return;
        }

        BatchItem item;
        item.file_path = current_image_path_;
        std::filesystem::path p(current_image_path_);
        item.source_name = wxString::FromUTF8(p.filename().string());
        item.status = U8("待识别");

        // Create thumbnail
        wxImage img;
        if (img.LoadFile(wxString::FromUTF8(current_image_path_), wxBITMAP_TYPE_ANY)) {
            int w = img.GetWidth();
            int h = img.GetHeight();
            double scale = std::min(110.0 / w, 44.0 / h);
            if (scale < 1.0) {
                img.Rescale(static_cast<int>(w * scale),
                            static_cast<int>(h * scale),
                            wxIMAGE_QUALITY_HIGH);
            }
            item.thumbnail = wxBitmap(img);
        }

        batch_items_.push_back(std::move(item));
        RefreshBatchList();
        SetStatusText(wxString::Format("已加入批量列表 (共 %zu 项)", batch_items_.size()));
    }

    void OnBatchSelectFiles(wxCommandEvent&) {
        wxFileDialog dialog(this, "选择验证码图片", wxEmptyString, wxEmptyString,
                            "图片文件 (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|所有文件 (*.*)|*.*",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
        if (dialog.ShowModal() == wxID_OK) {
            wxArrayString paths;
            dialog.GetPaths(paths);

            for (const auto& path : paths) {
                BatchItem item;
                item.file_path = path.ToStdString();
                item.source_name = path.AfterLast(wxFILE_SEP_PATH);
                item.status = U8("待识别");

                wxImage img;
                if (img.LoadFile(path, wxBITMAP_TYPE_ANY)) {
                    int w = img.GetWidth();
                    int h = img.GetHeight();
                    double scale = std::min(110.0 / w, 44.0 / h);
                    if (scale < 1.0) {
                        img.Rescale(static_cast<int>(w * scale),
                                    static_cast<int>(h * scale),
                                    wxIMAGE_QUALITY_HIGH);
                    }
                    item.thumbnail = wxBitmap(img);
                }

                batch_items_.push_back(std::move(item));
            }

            RefreshBatchList();
            SetStatusText(wxString::Format("已添加 %zu 个文件到批量列表", paths.GetCount()));
        }
    }

    void OnBatchRecognize(wxCommandEvent&) {
        if (!EnsureModelLoaded()) return;
        if (batch_items_.empty()) {
            wxMessageBox("批量列表为空，请先添加图片。", "批量识别", wxOK | wxICON_WARNING, this);
            return;
        }

        SetStatusText("批量识别中...");

        double total_ms = 0.0;
        int success_count = 0;

        for (auto& item : batch_items_) {
            if (item.status == U8("成功") || item.status == U8("失败")) continue;

            const auto start = std::chrono::steady_clock::now();
            auto result = ocr_->predict(item.file_path);
            const auto end = std::chrono::steady_clock::now();
            item.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
            total_ms += item.elapsed_ms;

            if (result.success) {
                item.result_expr = wxString::FromUTF8(result.expression);
                item.status = U8("成功");
                success_count++;
            } else {
                item.result_expr = wxString::FromUTF8(result.error);
                item.status = U8("失败");
            }
        }

        RefreshBatchList();
        UpdateBatchStats();

        SetStatusText(wxString::Format("批量识别完成: %d/%d 成功",
                                         success_count, static_cast<int>(batch_items_.size())));
    }

    void OnBatchClear(wxCommandEvent&) {
        batch_items_.clear();
        RefreshBatchList();
        SetStatusText("批量列表已清空");
    }

    void RefreshBatchList() {
        list_batch_->DeleteAllItems();
        for (size_t i = 0; i < batch_items_.size(); ++i) {
            const auto& item = batch_items_[i];
            long idx = list_batch_->InsertItem(static_cast<long>(i),
                                                wxString::Format("%zu", i + 1));
            list_batch_->SetItem(idx, 1, item.source_name);
            list_batch_->SetItem(idx, 2, item.result_expr);
            list_batch_->SetItem(idx, 3, item.status);
            if (item.elapsed_ms > 0) {
                list_batch_->SetItem(idx, 4, wxString::Format("%.1f", item.elapsed_ms));
            }

            // Color by status
            if (item.status == U8("成功")) {
                list_batch_->SetItemTextColour(idx, wxColour(COLOR_SUCCESS));
            } else if (item.status == U8("失败")) {
                list_batch_->SetItemTextColour(idx, wxColour(COLOR_ERROR));
            }
        }
        UpdateBatchStats();
    }

    void UpdateBatchStats() {
        double total_ms = 0.0;
        int recognized = 0;
        for (const auto& item : batch_items_) {
            if (item.elapsed_ms > 0) {
                total_ms += item.elapsed_ms;
                recognized++;
            }
        }
        double avg = (recognized > 0) ? total_ms / recognized : 0.0;
        lbl_batch_stats_->SetLabelText(
            wxString::Format("共 %zu 项 · 平均 %.1f 毫秒", batch_items_.size(), avg));
    }

    // ---- Helpers ----

    bool EnsureModelLoaded() {
        if (!model_loaded_ || !ocr_ || !ocr_->is_loaded()) {
            LogMessage("EnsureModelLoaded: model not loaded");
            wxMessageBox("请先加载模型。", "操作", wxOK | wxICON_WARNING, this);
            return false;
        }
        return true;
    }

    void DisplayImage(const wxString& path) {
        LogMessage("DisplayImage: path=" + path.ToStdString());
        wxImage image;
        if (image.LoadFile(path, wxBITMAP_TYPE_ANY)) {
            // Scale to fit preview area while maintaining aspect ratio
            const int max_w = 400;
            const int max_h = 160;
            int w = image.GetWidth();
            int h = image.GetHeight();
            if (w > max_w || h > max_h) {
                const double scale = std::min(static_cast<double>(max_w) / w,
                                               static_cast<double>(max_h) / h);
                w = static_cast<int>(std::round(w * scale));
                h = static_cast<int>(std::round(h * scale));
                image.Rescale(w, h, wxIMAGE_QUALITY_HIGH);
            }
            img_preview_->SetBitmap(wxBitmap(image));
            img_preview_->SetMinSize(wxSize(w, h));
            img_preview_->Refresh();
            LogMessage("DisplayImage: bitmap set, size=" + std::to_string(w) + "x" + std::to_string(h));
        } else {
            LogMessage("DisplayImage: image.LoadFile failed");
        }
    }

    void DisplayResult(const shmtu::cas_ocr::PredictResult& result, double elapsed_ms) {
        LogMessage("DisplayResult: entered");
        if (result.success) {
            LogMessage("DisplayResult: setting result expression label to: " + result.expression);
            lbl_result_expr_->SetLabelText(wxString::FromUTF8(result.expression));
            LogMessage("DisplayResult: result expression label updated");
            lbl_elapsed_ms_->SetLabelText(
                wxString::Format("用时：%.1f 毫秒", elapsed_ms));
            LogMessage("DisplayResult: elapsed label updated");
            SetStatusText("识别成功");
            LogMessage("DisplayResult: success status text updated");
        } else {
            LogMessage("DisplayResult: setting error label to: " + result.error);
            lbl_result_expr_->SetLabelText(wxString::FromUTF8(result.error));
            LogMessage("DisplayResult: error label updated");
            lbl_elapsed_ms_->SetLabelText(
                wxString::Format("用时：%.1f 毫秒", elapsed_ms));
            LogMessage("DisplayResult: elapsed label updated");
            SetStatusText("识别失败");
            LogMessage("DisplayResult: failure status text updated");
        }
        LogMessage("DisplayResult: completed");
    }

    void SetStatusText(const wxString& text) {
        LogMessage("SetStatusText: " + text.ToStdString());
        if (lbl_status_) {
            lbl_status_->SetLabelText(text);
            LogMessage("SetStatusText: label updated");
        } else {
            LogMessage("SetStatusText: lbl_status_ is null");
        }
    }

    // =======================================================================
    // Data members
    // =======================================================================

    LaunchOptions launch_options_;

    // OCR engine
    std::unique_ptr<shmtu::cas_ocr::CasOcr> ocr_;
    bool model_loaded_ = false;
    std::atomic<bool> download_active_{false};

    // Current image
    std::string current_image_path_;
    std::vector<uint8_t> current_image_data_;

    // Batch items
    std::vector<BatchItem> batch_items_;

    // Top bar controls
    wxPanel* top_bar_panel_ = nullptr;
    wxTextCtrl* txt_model_dir_ = nullptr;
    wxButton* btn_check_download_ = nullptr;
    wxStaticText* lbl_model_status_ = nullptr;
    wxGauge* gauge_download_ = nullptr;

    // Main area controls
    wxPanel* main_panel_ = nullptr;
    wxStaticBitmap* img_preview_ = nullptr;
    wxStaticText* lbl_source_path_ = nullptr;
    wxStaticText* lbl_result_expr_ = nullptr;
    wxStaticText* lbl_elapsed_ms_ = nullptr;

    // Right column controls
    wxTextCtrl* txt_captcha_url_ = nullptr;
    wxButton* btn_download_captcha_ = nullptr;
    wxButton* btn_open_local_ = nullptr;
    wxButton* btn_ocr_recognize_ = nullptr;
    wxButton* btn_add_to_batch_ = nullptr;
    wxButton* btn_release_model_ = nullptr;

    // Bottom bar controls
    wxPanel* bottom_bar_panel_ = nullptr;
    wxStaticText* lbl_status_ = nullptr;
    wxChoice* choice_precision_ = nullptr;
    wxCheckBox* chk_use_gpu_ = nullptr;

    // Batch controls
    wxCollapsiblePane* batch_pane_ = nullptr;
    wxButton* btn_batch_select_ = nullptr;
    wxButton* btn_batch_recognize_ = nullptr;
    wxButton* btn_batch_clear_ = nullptr;
    wxStaticText* lbl_batch_stats_ = nullptr;
    wxListCtrl* list_batch_ = nullptr;
};

// ============================================================================
// wxApp implementation
// ============================================================================

class OcrGuiApp final : public wxApp {
public:
    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddOption("", "model-dir", "initial model directory", wxCMD_LINE_VAL_STRING);
        parser.AddOption("", "precision", "initial model precision", wxCMD_LINE_VAL_STRING);
        parser.AddSwitch("", "use-gpu", "enable GPU acceleration by default");
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) return false;

        wxString model_dir;
        if (parser.Found("model-dir", &model_dir)) {
            launch_options_.model_dir = model_dir.ToStdString();
        }

        wxString precision;
        if (parser.Found("precision", &precision)) {
            const auto precision_str = precision.ToStdString();
            if (precision_str != "fp16" && precision_str != "fp32") {
                wxLogError("Unsupported precision '%s'. Use fp16 or fp32.",
                           precision_str.c_str());
                return false;
            }
            launch_options_.precision = precision_str;
        }

        launch_options_.use_gpu = parser.Found("use-gpu");
        return true;
    }

    bool OnInit() override {
        if (!wxApp::OnInit()) return false;

        InstallCrashHandlers();
        LogMessage(std::string("OnInit: GUI starting, log file=") + LOG_PATH);

#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif

        const auto curl_init_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curl_init_rc != CURLE_OK) {
            LogMessage("OnInit: curl_global_init failed: " +
                       std::string(curl_easy_strerror(curl_init_rc)));
            return false;
        }

        wxInitAllImageHandlers();

        auto* frame = new MainFrame(launch_options_);
        frame->Show(true);
        return true;
    }

    int OnExit() override {
        LogMessage("OnExit: cleaning up curl");
        curl_global_cleanup();
        return wxApp::OnExit();
    }

private:
    LaunchOptions launch_options_;
};

wxIMPLEMENT_APP(OcrGuiApp);
