#pragma once

#include <shmtu/cas_ocr/gui/launch_options.h>
#include <shmtu/cas_ocr/types.h>

#include <QMainWindow>
#include <QString>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QFrame;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QResizeEvent;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;
class QPixmap;

namespace shmtu::cas::ocr {
class CasOcr;
}

namespace shmtu::cas::ocr::gui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const LaunchOptions& launch_options = LaunchOptions{});
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct BatchItem {
        QString file_path;
        QString source_name;
        QString result_expr;
        QString status;
        double elapsed_ms = 0.0;
        std::vector<uint8_t> image_data;
    };

    void buildMenuBar();
    void buildUi();
    void buildTopBar(QVBoxLayout* root_layout);
    void buildMainArea(QVBoxLayout* root_layout);
    void buildLeftColumn(QHBoxLayout* parent_layout);
    void buildRightColumn(QHBoxLayout* parent_layout);
    void buildBottomBar(QVBoxLayout* root_layout);
    void buildBatchPane(QVBoxLayout* parent_layout);

    void onAbout();
    void onCheckDownloadModels();
    void startModelDownload(const std::vector<std::string>& missing_files, bool use_gitee);
    void loadModelFromCurrentSettings();
    void onReleaseModel();
    void updateModelStatusUi();

    void onDownloadCaptcha();
    void onOpenLocalImage();
    void onOcrRecognize();

    void onAddToBatch();
    void onBatchSelectFiles();
    void onBatchRecognize();
    void onBatchClear();
    void refreshBatchTable();
    void updateBatchStats();
    QWidget* createBatchItemCard(const BatchItem& item, QWidget* parent);

    bool ensureModelLoaded();
    void displayImage(const QString& path);
    void updatePreviewPixmap();
    void displayResult(const PredictResult& result, double elapsed_ms);
    void setStatusText(const QString& text);

    LaunchOptions launch_options_;
    std::unique_ptr<shmtu::cas::ocr::CasOcr> ocr_;
    bool model_loaded_ = false;
    bool download_active_ = false;

    QString current_image_path_;
    QString current_image_source_name_;
    std::vector<uint8_t> current_image_data_;
    std::vector<BatchItem> batch_items_;
    std::unique_ptr<QPixmap> preview_pixmap_;

    QWidget* top_bar_panel_ = nullptr;
    QLineEdit* model_dir_edit_ = nullptr;
    QPushButton* check_download_button_ = nullptr;
    QLabel* model_status_label_ = nullptr;
    QProgressBar* download_progress_bar_ = nullptr;

    QWidget* main_panel_ = nullptr;
    QLabel* preview_label_ = nullptr;
    QLabel* source_path_label_ = nullptr;
    QLineEdit* result_expr_edit_ = nullptr;
    QLineEdit* elapsed_ms_edit_ = nullptr;

    QLineEdit* captcha_url_edit_ = nullptr;
    QPushButton* download_captcha_button_ = nullptr;
    QPushButton* open_local_button_ = nullptr;
    QPushButton* recognize_button_ = nullptr;
    QPushButton* add_to_batch_button_ = nullptr;
    QPushButton* release_model_button_ = nullptr;

    QWidget* bottom_bar_panel_ = nullptr;
    QLabel* status_label_ = nullptr;
    QComboBox* precision_combo_ = nullptr;
    QCheckBox* use_gpu_checkbox_ = nullptr;

    QToolButton* batch_toggle_button_ = nullptr;
    QWidget* batch_content_widget_ = nullptr;
    QPushButton* batch_select_button_ = nullptr;
    QPushButton* batch_recognize_button_ = nullptr;
    QPushButton* batch_clear_button_ = nullptr;
    QLabel* batch_stats_label_ = nullptr;
    QScrollArea* batch_scroll_area_ = nullptr;
    QWidget* batch_list_widget_ = nullptr;
    QVBoxLayout* batch_list_layout_ = nullptr;
};

}  // namespace shmtu::cas::ocr::gui
