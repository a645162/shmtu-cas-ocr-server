#include <shmtu/cas_ocr/gui/main_window.h>

#include <shmtu/cas_ocr/cas_ocr.h>
#include <shmtu/cas_ocr/gui/launch_options.h>
#include <shmtu/cas_ocr/gui/logging.h>
#include <shmtu/cas_ocr/gui/model_download.h>

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QColor>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace shmtu::cas_ocr::gui {
namespace {

constexpr auto APP_TITLE_CN = "海大验证码识别 - NCNN";
constexpr auto COLOR_SUCCESS = "#4CAF50";
constexpr auto COLOR_ERROR = "#F44336";
constexpr auto COLOR_TOP_BAR_BG = "#F5F5F5";
constexpr auto COLOR_BOTTOM_BAR_BG = "#F5F5F5";

QString qs(const char* text) {
    return QString::fromUtf8(text);
}

bool isSupportedPrecision(const QString& precision) {
    return precision == "fp16" || precision == "fp32";
}

void setSectionLabelFont(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

void setResultFieldStyle(QLineEdit* edit, int point_size, bool bold) {
    QFont font = edit->font();
    font.setPointSize(point_size);
    font.setBold(bold);
    edit->setFont(font);
    edit->setReadOnly(true);
    edit->setFrame(false);
}

QFrame* createSeparator(QWidget* parent) {
    auto* separator = new QFrame(parent);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    return separator;
}

}  // namespace

MainWindow::MainWindow(const LaunchOptions& launch_options)
    : launch_options_(launch_options),
      ocr_(std::make_unique<shmtu::cas_ocr::CasOcr>()),
      preview_pixmap_(std::make_unique<QPixmap>()) {
    setWindowTitle(qs(APP_TITLE_CN));
    resize(980, 720);
    setMinimumSize(820, 600);

    buildMenuBar();
    buildUi();
    updateModelStatusUi();
}

MainWindow::~MainWindow() = default;

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updatePreviewPixmap();
}

void MainWindow::buildMenuBar() {
    auto* file_menu = menuBar()->addMenu(qs("文件"));
    auto* exit_action = file_menu->addAction(qs("退出\tAlt+F4"));
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* help_menu = menuBar()->addMenu(qs("帮助"));
    auto* about_action = help_menu->addAction(qs("关于"));
    connect(about_action, &QAction::triggered, this, [this]() { onAbout(); });
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    buildTopBar(root_layout);
    buildMainArea(root_layout);
    buildBottomBar(root_layout);

    setCentralWidget(central);
}

void MainWindow::buildTopBar(QVBoxLayout* root_layout) {
    top_bar_panel_ = new QWidget(this);
    top_bar_panel_->setStyleSheet(QStringLiteral("background-color: %1;").arg(COLOR_TOP_BAR_BG));

    auto* panel_layout = new QVBoxLayout(top_bar_panel_);
    panel_layout->setContentsMargins(8, 8, 8, 8);

    auto* group = new QGroupBox(qs("模型"), top_bar_panel_);
    auto* group_layout = new QVBoxLayout(group);

    auto* row_layout = new QHBoxLayout();

    auto* model_dir_label = new QLabel(qs("模型目录："), group);
    setSectionLabelFont(model_dir_label);
    row_layout->addWidget(model_dir_label);

    model_dir_edit_ = new QLineEdit(QString::fromStdString(launch_options_.model_dir), group);
    model_dir_edit_->setMinimumWidth(520);
    row_layout->addWidget(model_dir_edit_, 1);

    check_download_button_ = new QPushButton(qs("检查 / 下载模型"), group);
    row_layout->addWidget(check_download_button_);

    model_status_label_ = new QLabel(qs("模型未就绪"), group);
    setSectionLabelFont(model_status_label_);
    row_layout->addWidget(model_status_label_);

    group_layout->addLayout(row_layout);

    download_progress_bar_ = new QProgressBar(group);
    download_progress_bar_->setRange(0, 100);
    download_progress_bar_->setValue(0);
    group_layout->addWidget(download_progress_bar_);

    panel_layout->addWidget(group);
    root_layout->addWidget(top_bar_panel_);

    connect(check_download_button_, &QPushButton::clicked, this,
            [this]() { onCheckDownloadModels(); });
}

void MainWindow::buildMainArea(QVBoxLayout* root_layout) {
    main_panel_ = new QWidget(this);
    auto* main_layout = new QVBoxLayout(main_panel_);
    main_layout->setContentsMargins(8, 8, 8, 8);

    auto* grid_layout = new QHBoxLayout();
    grid_layout->setSpacing(8);

    buildLeftColumn(grid_layout);
    buildRightColumn(grid_layout);

    main_layout->addLayout(grid_layout, 1);
    buildBatchPane(main_layout);

    root_layout->addWidget(main_panel_, 1);
}

void MainWindow::buildLeftColumn(QHBoxLayout* parent_layout) {
    auto* left_panel = new QWidget(main_panel_);
    auto* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(0, 0, 0, 0);

    auto* preview_group = new QGroupBox(qs("预览与结果"), left_panel);
    auto* preview_layout = new QVBoxLayout(preview_group);

    preview_label_ = new QLabel(preview_group);
    preview_label_->setAlignment(Qt::AlignCenter);
    preview_label_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    preview_label_->setMinimumSize(480, 260);
    preview_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_layout->addWidget(preview_label_, 1);

    source_path_label_ = new QLabel(qs("未选择图片"), preview_group);
    QFont dim_font = source_path_label_->font();
    dim_font.setPointSize(dim_font.pointSize() - 1);
    source_path_label_->setFont(dim_font);
    preview_layout->addWidget(source_path_label_);

    auto* result_group = new QGroupBox(qs("识别结果"), preview_group);
    auto* result_layout = new QVBoxLayout(result_group);

    result_expr_edit_ = new QLineEdit(qs("（暂无识别结果）"), result_group);
    setResultFieldStyle(result_expr_edit_, 34, true);
    result_layout->addWidget(result_expr_edit_);

    elapsed_ms_edit_ = new QLineEdit(qs("用时：0.0 毫秒"), result_group);
    setResultFieldStyle(elapsed_ms_edit_, std::max(8, elapsed_ms_edit_->font().pointSize() - 1),
                        false);
    result_layout->addWidget(elapsed_ms_edit_);

    preview_layout->addWidget(result_group);
    left_layout->addWidget(preview_group);
    parent_layout->addWidget(left_panel, 1);
}

void MainWindow::buildRightColumn(QHBoxLayout* parent_layout) {
    auto* right_panel = new QWidget(main_panel_);
    right_panel->setMinimumWidth(240);
    right_panel->setMaximumWidth(260);

    auto* right_panel_layout = new QVBoxLayout(right_panel);
    right_panel_layout->setContentsMargins(0, 0, 0, 0);

    auto* action_group = new QGroupBox(qs("操作"), right_panel);
    auto* action_layout = new QVBoxLayout(action_group);
    action_layout->addSpacing(8);

    auto* get_image_label = new QLabel(qs("获取图片"), action_group);
    setSectionLabelFont(get_image_label);
    action_layout->addWidget(get_image_label);
    action_layout->addSpacing(6);

    captcha_url_edit_ = new QLineEdit(QStringLiteral("https://cas.shmtu.edu.cn/cas/captcha"),
                                      action_group);
    captcha_url_edit_->setPlaceholderText(qs("验证码 URL"));
    action_layout->addWidget(captcha_url_edit_);
    action_layout->addSpacing(6);

    download_captcha_button_ = new QPushButton(qs("下载验证码"), action_group);
    download_captcha_button_->setMinimumWidth(220);
    action_layout->addWidget(download_captcha_button_);
    action_layout->addSpacing(4);

    open_local_button_ = new QPushButton(qs("打开本地图片"), action_group);
    open_local_button_->setMinimumWidth(220);
    action_layout->addWidget(open_local_button_);
    action_layout->addSpacing(8);
    action_layout->addWidget(createSeparator(action_group));
    action_layout->addSpacing(8);

    auto* recognize_label = new QLabel(qs("识别"), action_group);
    setSectionLabelFont(recognize_label);
    action_layout->addWidget(recognize_label);
    action_layout->addSpacing(6);

    recognize_button_ = new QPushButton(qs("▶ OCR 识别"), action_group);
    recognize_button_->setMinimumWidth(220);
    recognize_button_->setMinimumHeight(50);
    QFont recognize_font = recognize_button_->font();
    recognize_font.setPointSize(20);
    recognize_font.setBold(true);
    recognize_button_->setFont(recognize_font);
    action_layout->addWidget(recognize_button_);
    action_layout->addSpacing(8);
    action_layout->addWidget(createSeparator(action_group));
    action_layout->addSpacing(8);

    auto* batch_label = new QLabel(qs("批量"), action_group);
    setSectionLabelFont(batch_label);
    action_layout->addWidget(batch_label);
    action_layout->addSpacing(6);

    add_to_batch_button_ = new QPushButton(qs("加入批量列表"), action_group);
    add_to_batch_button_->setMinimumWidth(220);
    action_layout->addWidget(add_to_batch_button_);
    action_layout->addSpacing(8);
    action_layout->addWidget(createSeparator(action_group));
    action_layout->addSpacing(8);

    release_model_button_ = new QPushButton(qs("释放模型"), action_group);
    release_model_button_->setMinimumWidth(220);
    action_layout->addWidget(release_model_button_);
    action_layout->addStretch(1);

    auto* author_label = new QLabel(QStringLiteral("Author: Haomin Kong"), action_group);
    QFont author_font = author_label->font();
    author_font.setPointSize(author_font.pointSize() - 2);
    author_label->setFont(author_font);
    author_label->setAlignment(Qt::AlignHCenter);
    action_layout->addWidget(author_label);

    right_panel_layout->addWidget(action_group);
    parent_layout->addWidget(right_panel);

    connect(download_captcha_button_, &QPushButton::clicked, this,
            [this]() { onDownloadCaptcha(); });
    connect(open_local_button_, &QPushButton::clicked, this,
            [this]() { onOpenLocalImage(); });
    connect(recognize_button_, &QPushButton::clicked, this,
            [this]() { onOcrRecognize(); });
    connect(add_to_batch_button_, &QPushButton::clicked, this,
            [this]() { onAddToBatch(); });
    connect(release_model_button_, &QPushButton::clicked, this,
            [this]() { onReleaseModel(); });
}

void MainWindow::buildBottomBar(QVBoxLayout* root_layout) {
    bottom_bar_panel_ = new QWidget(this);
    bottom_bar_panel_->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(COLOR_BOTTOM_BAR_BG));

    auto* bottom_layout = new QHBoxLayout(bottom_bar_panel_);
    bottom_layout->setContentsMargins(8, 8, 8, 8);

    status_label_ = new QLabel(qs("等待操作"), bottom_bar_panel_);
    bottom_layout->addWidget(status_label_, 1);

    bottom_layout->addWidget(new QLabel(qs("精度:"), bottom_bar_panel_));

    precision_combo_ = new QComboBox(bottom_bar_panel_);
    precision_combo_->addItems({QStringLiteral("fp16"), QStringLiteral("fp32")});
    precision_combo_->setCurrentText(isSupportedPrecision(QString::fromStdString(
                                         launch_options_.precision))
                                         ? QString::fromStdString(launch_options_.precision)
                                         : QStringLiteral("fp16"));
    bottom_layout->addWidget(precision_combo_);
    bottom_layout->addSpacing(12);

    use_gpu_checkbox_ = new QCheckBox(qs("GPU加速"), bottom_bar_panel_);
    use_gpu_checkbox_->setChecked(launch_options_.use_gpu);
    bottom_layout->addWidget(use_gpu_checkbox_);

    root_layout->addWidget(bottom_bar_panel_);
}

void MainWindow::buildBatchPane(QVBoxLayout* parent_layout) {
    auto* wrapper = new QWidget(main_panel_);
    auto* wrapper_layout = new QVBoxLayout(wrapper);
    wrapper_layout->setContentsMargins(0, 8, 0, 0);

    batch_toggle_button_ = new QToolButton(wrapper);
    batch_toggle_button_->setText(qs("批量识别 / 批量比对"));
    batch_toggle_button_->setCheckable(true);
    batch_toggle_button_->setChecked(false);
    batch_toggle_button_->setArrowType(Qt::RightArrow);
    batch_toggle_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    wrapper_layout->addWidget(batch_toggle_button_);

    batch_content_widget_ = new QWidget(wrapper);
    batch_content_widget_->setVisible(false);
    auto* batch_content_layout = new QVBoxLayout(batch_content_widget_);
    batch_content_layout->setContentsMargins(0, 4, 0, 0);

    auto* batch_group = new QGroupBox(qs("批量列表"), batch_content_widget_);
    auto* batch_group_layout = new QVBoxLayout(batch_group);

    auto* button_row = new QHBoxLayout();
    batch_select_button_ = new QPushButton(qs("选择多张本地图片..."), batch_group);
    button_row->addWidget(batch_select_button_);

    batch_recognize_button_ = new QPushButton(qs("批量识别"), batch_group);
    button_row->addWidget(batch_recognize_button_);

    batch_clear_button_ = new QPushButton(qs("清空列表"), batch_group);
    button_row->addWidget(batch_clear_button_);

    batch_stats_label_ = new QLabel(qs("共 0 项 · 平均 0.0 毫秒"), batch_group);
    button_row->addWidget(batch_stats_label_, 1);
    batch_group_layout->addLayout(button_row);
    batch_group_layout->addSpacing(4);

    batch_table_ = new QTableWidget(0, 5, batch_group);
    batch_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    batch_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    batch_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    batch_table_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), qs("来源"), qs("结果"), qs("状态"), qs("耗时(ms)")});
    batch_table_->horizontalHeader()->setStretchLastSection(false);
    batch_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    batch_table_->setColumnWidth(0, 36);
    batch_table_->setColumnWidth(2, 160);
    batch_table_->setColumnWidth(3, 80);
    batch_table_->setColumnWidth(4, 90);
    batch_table_->setMinimumHeight(220);
    batch_group_layout->addWidget(batch_table_);

    batch_content_layout->addWidget(batch_group);
    wrapper_layout->addWidget(batch_content_widget_);
    parent_layout->addWidget(wrapper);

    connect(batch_toggle_button_, &QToolButton::toggled, this, [this](bool checked) {
        batch_toggle_button_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        batch_content_widget_->setVisible(checked);
        adjustSize();
    });
    connect(batch_select_button_, &QPushButton::clicked, this,
            [this]() { onBatchSelectFiles(); });
    connect(batch_recognize_button_, &QPushButton::clicked, this,
            [this]() { onBatchRecognize(); });
    connect(batch_clear_button_, &QPushButton::clicked, this,
            [this]() { onBatchClear(); });
}

void MainWindow::onAbout() {
    QMessageBox::about(
        this,
        qs("关于"),
        qs("海大验证码识别 - NCNN\n版本：") + QStringLiteral("2.0.0") + qs(
            "\n\n上海海事大学 CAS 验证码 OCR 识别工具\n基于 NCNN 推理引擎，支持 CPU / Vulkan GPU 加速"));
}

void MainWindow::onCheckDownloadModels() {
    logMessage("onCheckDownloadModels: entered");
    if (download_active_) {
        QMessageBox::information(this, qs("下载"), qs("模型正在下载中，请稍候..."));
        return;
    }

    const auto model_dir = model_dir_edit_->text().toStdString();
    const auto precision = precision_combo_->currentText().toStdString();
    const auto missing_files = missingModelFiles(model_dir, precision);

    if (missing_files.empty()) {
        setStatusText(qs("所有模型文件已就绪，正在加载模型..."));
        loadModelFromCurrentSettings();
        return;
    }

    QString message = qs("缺少以下模型文件：\n\n");
    for (const auto& file : missing_files) {
        message += QString::fromStdString(file) + QLatin1Char('\n');
    }
    message += qs("\n是否从 Gitee 下载？（国内推荐）");

    const auto answer = QMessageBox::question(this, qs("下载模型"), message,
                                              QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    startModelDownload(missing_files, true);
}

void MainWindow::startModelDownload(const std::vector<std::string>& missing_files, bool use_gitee) {
    download_active_ = true;
    check_download_button_->setEnabled(false);
    download_progress_bar_->setValue(0);
    setStatusText(qs("正在下载模型文件..."));

    QProgressDialog progress_dialog(qs("正在下载模型文件..."), QString(), 0,
                                    static_cast<int>(missing_files.size()), this);
    progress_dialog.setWindowTitle(qs("下载模型"));
    progress_dialog.setCancelButton(nullptr);
    progress_dialog.setAutoClose(true);
    progress_dialog.setMinimumDuration(0);

    std::string error_message;
    const bool all_ok = downloadModelFiles(
        model_dir_edit_->text().toStdString(),
        missing_files,
        use_gitee,
        [this, &progress_dialog](int completed_files, int total_files, const std::string& filename) {
            progress_dialog.setMaximum(total_files);
            progress_dialog.setValue(completed_files);
            if (!filename.empty()) {
                progress_dialog.setLabelText(qs("正在下载：") + QString::fromStdString(filename));
            }
            const int pct =
                total_files > 0 ? static_cast<int>((completed_files * 100.0) / total_files) : 100;
            download_progress_bar_->setValue(pct);
            QApplication::processEvents();
            return true;
        },
        error_message);

    download_active_ = false;
    check_download_button_->setEnabled(true);

    if (all_ok) {
        download_progress_bar_->setValue(100);
        setStatusText(qs("模型下载完成，正在加载模型..."));
        loadModelFromCurrentSettings();
        return;
    }

    download_progress_bar_->setValue(0);
    setStatusText(qs("模型下载失败"));
    QMessageBox::critical(this, qs("下载失败"),
                          qs("部分模型文件下载失败：\n\n") + QString::fromStdString(error_message));
}

void MainWindow::loadModelFromCurrentSettings() {
    const auto model_dir = model_dir_edit_->text().toStdString();
    const auto precision = precision_combo_->currentText().toStdString();
    const bool use_gpu = use_gpu_checkbox_->isChecked();

    ocr_ = std::make_unique<shmtu::cas_ocr::CasOcr>(model_dir, use_gpu);
    if (!ocr_->load_model(precision.empty() ? "fp16" : precision)) {
        model_loaded_ = false;
        updateModelStatusUi();
        setStatusText(qs("模型加载失败"));
        QMessageBox::critical(this, qs("加载模型"),
                              qs("无法加载 NCNN 模型文件，请检查模型目录路径。\n\n"
                                 "目录应包含 .param 和 .bin 文件。"));
        return;
    }

    model_loaded_ = true;
    updateModelStatusUi();
}

void MainWindow::onReleaseModel() {
    if (ocr_) {
        ocr_->release();
    }
    model_loaded_ = false;
    updateModelStatusUi();
    setStatusText(qs("模型已释放"));
}

void MainWindow::updateModelStatusUi() {
    model_status_label_->setText(model_loaded_ ? qs("模型已就绪") : qs("模型未就绪"));
    recognize_button_->setEnabled(model_loaded_);
    add_to_batch_button_->setEnabled(model_loaded_);
    batch_recognize_button_->setEnabled(model_loaded_);
}

void MainWindow::onDownloadCaptcha() {
    const auto url = captcha_url_edit_->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, qs("下载验证码"), qs("请输入验证码 URL。"));
        return;
    }
    if (!url.startsWith(QStringLiteral("http://")) &&
        !url.startsWith(QStringLiteral("https://"))) {
        QMessageBox::warning(this, qs("下载验证码"),
                             qs("URL 格式不正确，请以 http:// 或 https:// 开头。"));
        return;
    }

    setStatusText(qs("正在下载验证码..."));

    long http_status = 0;
    std::string error_message;
    std::vector<uint8_t> image_data;
    const bool ok = downloadUrlToMemory(url.toStdString(), image_data, http_status, error_message);
    if (!ok || http_status != 200 || image_data.empty()) {
        setStatusText(qs("下载验证码失败"));
        QMessageBox::critical(this, qs("下载验证码"), qs("无法下载验证码图片，请检查 URL。"));
        return;
    }

    const auto temp_file = std::filesystem::temp_directory_path() / "captcha_download.png";
    {
        std::ofstream ofs(temp_file, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(image_data.data()),
                  static_cast<std::streamsize>(image_data.size()));
    }

    current_image_path_ = QString::fromStdString(temp_file.string());
    current_image_data_ = std::move(image_data);
    displayImage(current_image_path_);
    source_path_label_->setText(url);
    setStatusText(qs("验证码下载成功"));
}

void MainWindow::onOpenLocalImage() {
    const auto path = QFileDialog::getOpenFileName(
        this,
        qs("选择验证码图片"),
        QString(),
        qs("图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    current_image_path_ = path;
    current_image_data_.clear();
    displayImage(path);
    source_path_label_->setText(path);
    setStatusText(qs("已加载本地图片"));
}

void MainWindow::onOcrRecognize() {
    if (!ensureModelLoaded()) {
        return;
    }
    if (current_image_path_.isEmpty()) {
        QMessageBox::warning(this, qs("OCR 识别"), qs("请先获取验证码图片。"));
        return;
    }

    setStatusText(qs("正在识别..."));

    const auto start = std::chrono::steady_clock::now();
    PredictResult result;

    if (!current_image_data_.empty()) {
        result = ocr_->predict(current_image_data_);
    } else {
        result = ocr_->predict(current_image_path_.toStdString());
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    displayResult(result, elapsed_ms);
}

void MainWindow::onAddToBatch() {
    if (current_image_path_.isEmpty()) {
        QMessageBox::warning(this, qs("批量"), qs("请先获取验证码图片。"));
        return;
    }

    BatchItem item;
    item.file_path = current_image_path_;
    item.source_name = QFileInfo(current_image_path_).fileName();
    item.status = qs("待识别");
    batch_items_.push_back(std::move(item));

    refreshBatchTable();
    setStatusText(qs("已加入批量列表 (共 ") + QString::number(batch_items_.size()) + qs(" 项)"));
}

void MainWindow::onBatchSelectFiles() {
    const auto paths = QFileDialog::getOpenFileNames(
        this,
        qs("选择验证码图片"),
        QString(),
        qs("图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*.*)"));
    if (paths.isEmpty()) {
        return;
    }

    for (const auto& path : paths) {
        BatchItem item;
        item.file_path = path;
        item.source_name = QFileInfo(path).fileName();
        item.status = qs("待识别");
        batch_items_.push_back(std::move(item));
    }

    refreshBatchTable();
    setStatusText(qs("已添加 ") + QString::number(paths.size()) + qs(" 个文件到批量列表"));
}

void MainWindow::onBatchRecognize() {
    if (!ensureModelLoaded()) {
        return;
    }
    if (batch_items_.empty()) {
        QMessageBox::warning(this, qs("批量识别"), qs("批量列表为空，请先添加图片。"));
        return;
    }

    setStatusText(qs("批量识别中..."));

    int success_count = 0;
    for (auto& item : batch_items_) {
        if (item.status == qs("成功") || item.status == qs("失败")) {
            continue;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto result = ocr_->predict(item.file_path.toStdString());
        const auto end = std::chrono::steady_clock::now();
        item.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (result.success) {
            item.result_expr = QString::fromStdString(result.expression);
            item.status = qs("成功");
            success_count++;
        } else {
            item.result_expr = QString::fromStdString(result.error);
            item.status = qs("失败");
        }
    }

    refreshBatchTable();
    setStatusText(qs("批量识别完成: ") + QString::number(success_count) + QLatin1Char('/') +
                  QString::number(batch_items_.size()) + qs(" 成功"));
}

void MainWindow::onBatchClear() {
    batch_items_.clear();
    refreshBatchTable();
    setStatusText(qs("批量列表已清空"));
}

void MainWindow::refreshBatchTable() {
    batch_table_->setRowCount(static_cast<int>(batch_items_.size()));

    for (int row = 0; row < static_cast<int>(batch_items_.size()); ++row) {
        const auto& item = batch_items_[static_cast<size_t>(row)];
        const QColor color = item.status == qs("成功")
                                 ? QColor(COLOR_SUCCESS)
                                 : item.status == qs("失败") ? QColor(COLOR_ERROR) : QColor();

        auto set_item = [&](int column, const QString& text) {
            auto* cell = new QTableWidgetItem(text);
            if (color.isValid()) {
                cell->setForeground(color);
            }
            batch_table_->setItem(row, column, cell);
        };

        set_item(0, QString::number(row + 1));
        set_item(1, item.source_name);
        set_item(2, item.result_expr);
        set_item(3, item.status);
        set_item(4, item.elapsed_ms > 0.0 ? QString::number(item.elapsed_ms, 'f', 1) : QString());
    }

    updateBatchStats();
}

void MainWindow::updateBatchStats() {
    double total_ms = 0.0;
    int recognized = 0;
    for (const auto& item : batch_items_) {
        if (item.elapsed_ms > 0.0) {
            total_ms += item.elapsed_ms;
            recognized++;
        }
    }

    const double avg = recognized > 0 ? total_ms / recognized : 0.0;
    batch_stats_label_->setText(qs("共 ") + QString::number(batch_items_.size()) + qs(" 项 · 平均 ") +
                                QString::number(avg, 'f', 1) + qs(" 毫秒"));
}

bool MainWindow::ensureModelLoaded() {
    if (!model_loaded_ || !ocr_ || !ocr_->is_loaded()) {
        QMessageBox::warning(this, qs("操作"), qs("请先加载模型。"));
        return false;
    }
    return true;
}

void MainWindow::displayImage(const QString& path) {
    const QPixmap pixmap(path);
    if (pixmap.isNull()) {
        logMessage("displayImage: failed to load " + path.toStdString());
        return;
    }

    *preview_pixmap_ = pixmap;
    updatePreviewPixmap();
}

void MainWindow::updatePreviewPixmap() {
    if (!preview_label_ || !preview_pixmap_ || preview_pixmap_->isNull()) {
        return;
    }

    QSize target_size = preview_label_->contentsRect().size();
    if (target_size.width() <= 0 || target_size.height() <= 0) {
        target_size = preview_label_->size();
    }

    const auto scaled =
        preview_pixmap_->scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_label_->setPixmap(scaled);
}

void MainWindow::displayResult(const PredictResult& result, double elapsed_ms) {
    if (result.success) {
        result_expr_edit_->setText(QString::fromStdString(result.expression));
        elapsed_ms_edit_->setText(qs("用时：") + QString::number(elapsed_ms, 'f', 1) + qs(" 毫秒"));
        setStatusText(qs("识别成功"));
        return;
    }

    result_expr_edit_->setText(QString::fromStdString(result.error));
    elapsed_ms_edit_->setText(qs("用时：") + QString::number(elapsed_ms, 'f', 1) + qs(" 毫秒"));
    setStatusText(qs("识别失败"));
}

void MainWindow::setStatusText(const QString& text) {
    if (status_label_) {
        status_label_->setText(text);
    }
    logMessage("setStatusText: " + text.toStdString());
}

}  // namespace shmtu::cas_ocr::gui
