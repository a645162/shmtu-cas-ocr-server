#include <shmtu/cas_ocr/gui/main_window.h>

#include <shmtu/cas_ocr/cas_ocr.h>
#include <shmtu/cas_ocr/version.h>
#include <shmtu/cas_ocr/gui/image_view.h>
#include <shmtu/cas_ocr/gui/launch_options.h>
#include <shmtu/cas_ocr/gui/logging.h>
#include <shmtu/cas_ocr/gui/model_download.h>
#include <shmtu/cas_ocr/manifest.h>

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QColor>
#include <QFontMetrics>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace shmtu::cas::ocr::gui {
namespace {

constexpr auto APP_TITLE_CN = "海大验证码识别 - NCNN";
constexpr auto COLOR_WINDOW_BG = "#f3f6fb";
constexpr auto COLOR_CARD_BG = "#ffffff";
constexpr auto COLOR_CARD_BORDER = "#d7e2f0";
constexpr auto COLOR_INNER_BG = "#f8fbff";
constexpr auto COLOR_TEXT = "#18212f";
constexpr auto COLOR_DIM_TEXT = "#66758a";
constexpr auto COLOR_ACCENT = "#0f6cbd";
constexpr auto COLOR_ACCENT_HOVER = "#115ea3";
constexpr auto COLOR_ACCENT_PRESSED = "#0c3b67";
constexpr auto COLOR_SUCCESS = "#4CAF50";
constexpr auto COLOR_ERROR = "#F44336";
constexpr auto COLOR_BADGE_IDLE = "#7b8797";
constexpr auto COLOR_DISABLED_BG = "#edf2f8";
constexpr auto COLOR_DISABLED_TEXT = "#94a3b8";

QString qs(const char* text) {
    return QString::fromUtf8(text);
}

std::string boolToString(const bool value) {
    return value ? "true" : "false";
}

struct GpuAvailabilityState {
    bool built_with_vulkan = false;
    bool runtime_available = false;
};

GpuAvailabilityState detectGpuAvailability() {
    GpuAvailabilityState state;
#ifdef NCNN_SUPPORT_VULKAN
    state.built_with_vulkan = true;
    state.runtime_available = shmtu::cas::ocr::CasOcr::is_vulkan_supported();
#endif
    return state;
}

QString gpuCheckboxText(const GpuAvailabilityState& state) {
    if (state.built_with_vulkan && state.runtime_available) {
        return qs("GPU加速（Vulkan可用）");
    }
    return qs("GPU加速（Vulkan不可用）");
}

QString gpuTooltipText(const GpuAvailabilityState& state) {
    if (!state.built_with_vulkan) {
        return qs("当前 GUI 不是 Vulkan 构建，请使用 Vulkan 启动脚本。");
    }
    if (!state.runtime_available) {
        return qs("当前构建已启用 Vulkan，但未检测到可用 Vulkan 设备。");
    }
    return qs("已检测到可用 Vulkan 设备，可以启用 GPU 加速。");
}

QString gpuStatusText(const GpuAvailabilityState& state, const bool use_gpu) {
    if (!state.built_with_vulkan) {
        return qs("Vulkan不可用，GPU加速已禁用，请使用 Vulkan 启动脚本。");
    }
    if (!state.runtime_available) {
        return qs("Vulkan不可用，GPU加速已禁用，未检测到可用设备。");
    }
    if (use_gpu) {
        return qs("Vulkan可用，GPU加速已启用。");
    }
    return qs("Vulkan可用，可手动启用 GPU 加速。");
}

QString buildWindowStyleSheet() {
    return QString::fromLatin1(R"(
QMainWindow {
    background: %1;
}
QWidget#card {
    background: %2;
    border: 1px solid %3;
    border-radius: 10px;
}
QWidget#innerCard {
    background: %4;
    border: 1px solid %3;
    border-radius: 8px;
}
QLabel#sectionLabel {
    color: %5;
    font-size: 13px;
    font-weight: 600;
}
QLabel#statusTitle {
    color: %6;
    font-size: 11px;
}
QLabel#statusMessage {
    color: %5;
    font-size: 14px;
    font-weight: 600;
}
QLabel#dimText {
    color: %6;
    font-size: 11px;
}
QLabel#statusBadge {
    background: %7;
    color: white;
    border-radius: 12px;
    padding: 4px 10px;
    font-size: 12px;
    font-weight: 600;
}
QLineEdit,
QComboBox {
    min-height: 34px;
    padding: 0 10px;
    border: 1px solid %3;
    border-radius: 8px;
    background: white;
    color: %5;
}
QLineEdit:focus,
QComboBox:focus {
    border: 1px solid %7;
}
QLineEdit#resultExpr,
QLineEdit#elapsedText {
    border: none;
    background: transparent;
    padding: 0;
}
QLineEdit#resultExpr {
    color: %7;
}
QLineEdit#resultExpr[error='true'] {
    color: %10;
}
QPushButton {
    min-height: 36px;
    padding: 0 14px;
    border-radius: 8px;
    border: 1px solid %3;
    background: white;
    color: %5;
}
QPushButton:hover {
    background: %4;
}
QPushButton:pressed {
    background: #e8f0fb;
}
QPushButton[accent='true'] {
    background: %7;
    color: white;
    border: 1px solid %7;
}
QPushButton[accent='true']:hover {
    background: %8;
    border-color: %8;
}
QPushButton[accent='true']:pressed {
    background: %9;
    border-color: %9;
}
QPushButton:disabled,
QPushButton[accent='true']:disabled {
    background: %11;
    color: %12;
    border-color: %11;
}
QToolButton {
    min-height: 36px;
    padding: 0 6px;
    border: none;
    color: %5;
    font-size: 13px;
    font-weight: 600;
    text-align: left;
}
QToolButton:hover {
    color: %7;
}
QProgressBar {
    min-height: 6px;
    max-height: 6px;
    border: none;
    border-radius: 3px;
    background: #e7eef8;
}
QProgressBar::chunk {
    border-radius: 3px;
    background: %7;
}
QWidget#previewSurface {
    background: transparent;
    border: none;
}
QScrollArea {
    background: transparent;
    border: none;
}
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 2px 0 2px 0;
}
QScrollBar::handle:vertical {
    background: #c6d5e5;
    border-radius: 4px;
    min-height: 28px;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: transparent;
    height: 0;
}
QWidget#batchItemCard {
    background: %4;
    border: 1px solid %3;
    border-radius: 8px;
}
QWidget#batchThumbCard {
    background: white;
    border: 1px solid %3;
    border-radius: 4px;
}
QCheckBox {
    color: %5;
}
QMenuBar {
    background: %1;
}
QMenuBar::item:selected {
    background: %4;
    border-radius: 6px;
}
    )")
        .arg(COLOR_WINDOW_BG, COLOR_CARD_BG, COLOR_CARD_BORDER, COLOR_INNER_BG, COLOR_TEXT,
             COLOR_DIM_TEXT, COLOR_ACCENT, COLOR_ACCENT_HOVER, COLOR_ACCENT_PRESSED,
             COLOR_ERROR, COLOR_DISABLED_BG, COLOR_DISABLED_TEXT);
}

bool isSupportedPrecision(const QString& precision) {
    return precision == "fp16" || precision == "fp32";
}

void setSectionLabelFont(QLabel* label) {
    label->setObjectName(QStringLiteral("sectionLabel"));
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

QLabel* createDimLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("dimText"));
    return label;
}

QFrame* createCard(QWidget* parent, const char* object_name = "card") {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QString::fromLatin1(object_name));
    return frame;
}

QVBoxLayout* createSectionCard(QWidget* parent, const QString& title) {
    auto* section_card = createCard(parent, "innerCard");
    auto* section_layout = new QVBoxLayout(section_card);
    section_layout->setContentsMargins(12, 12, 12, 12);
    section_layout->setSpacing(8);

    auto* title_label = new QLabel(title, section_card);
    setSectionLabelFont(title_label);
    section_layout->addWidget(title_label);

    auto* parent_layout = qobject_cast<QVBoxLayout*>(parent->layout());
    if (parent_layout) {
        parent_layout->addWidget(section_card);
    }

    return section_layout;
}

void setAccentButton(QPushButton* button) {
    button->setProperty("accent", true);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

}  // namespace

MainWindow::MainWindow(const LaunchOptions& launch_options)
    : launch_options_(launch_options),
      current_model_version_(launch_options.model_version),
      current_v2_tag_(launch_options.v2_tag),
      current_v2_backbone_(launch_options.v2_backbone),
      ocr_(std::make_unique<shmtu::cas::ocr::CasOcr>()) {
    setWindowTitle(qs(APP_TITLE_CN));
    resize(980, 720);
    setMinimumSize(820, 600);
    setStyleSheet(buildWindowStyleSheet());

    buildMenuBar();
    buildUi();
    updateModelStatusUi();
    setStatusText(gpuStatusText(detectGpuAvailability(), use_gpu_checkbox_->isChecked()));
    updateResultTextLayout();
    {
        std::ostringstream oss;
        oss << "MainWindow: initialized"
            << ", model_dir=" << launch_options_.model_dir
            << ", precision=" << launch_options_.precision
            << ", use_gpu=" << boolToString(launch_options_.use_gpu)
            << ", model_version=" << shmtu::cas::ocr::model_version_to_string(current_model_version_)
            << ", v2_tag=" << current_v2_tag_
            << ", v2_backbone=" << current_v2_backbone_;
        logMessage(oss.str());
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateResultTextLayout();
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
    root_layout->setContentsMargins(12, 12, 12, 12);
    root_layout->setSpacing(10);

    buildTopBar(root_layout);
    buildMainArea(root_layout);
    buildBottomBar(root_layout);

    setCentralWidget(central);
}

void MainWindow::buildTopBar(QVBoxLayout* root_layout) {
    top_bar_panel_ = createCard(this);

    auto* panel_layout = new QVBoxLayout(top_bar_panel_);
    panel_layout->setContentsMargins(12, 12, 12, 12);
    panel_layout->setSpacing(8);

    auto* row_layout = new QHBoxLayout();
    row_layout->setSpacing(8);

    auto* model_dir_label = new QLabel(qs("模型目录："), top_bar_panel_);
    setSectionLabelFont(model_dir_label);
    row_layout->addWidget(model_dir_label);

    model_dir_edit_ = new QLineEdit(QString::fromStdString(launch_options_.model_dir), top_bar_panel_);
    model_dir_edit_->setMinimumWidth(520);
    row_layout->addWidget(model_dir_edit_, 1);

    check_download_button_ = new QPushButton(qs("检查 / 下载模型"), top_bar_panel_);
    row_layout->addWidget(check_download_button_);

    model_status_label_ = new QLabel(qs("模型未就绪"), top_bar_panel_);
    model_status_label_->setObjectName(QStringLiteral("statusBadge"));
    row_layout->addWidget(model_status_label_);

    panel_layout->addLayout(row_layout);

    // --- Model version row ---
    auto* version_row = new QHBoxLayout();
    version_row->setSpacing(8);

    model_version_combo_ = new QComboBox(top_bar_panel_);
    model_version_combo_->addItem(qs("模型版本: V2 (默认)"));
    model_version_combo_->addItem(qs("模型版本: V1 (经典)"));
    model_version_combo_->setCurrentIndex(
        current_model_version_ == shmtu::cas::ocr::ModelVersion::V1 ? 1 : 0);
    model_version_combo_->setToolTip(qs("V2: 单模型 TriSlot Decoder (默认) | V1: 三模型 ResNet"));
    version_row->addWidget(model_version_combo_);

    v2_model_label_ = new QLabel(qs("V2 模型: 未选择"), top_bar_panel_);
    v2_model_label_->setObjectName(QStringLiteral("dimText"));
    if (!current_v2_tag_.empty()) {
        v2_model_label_->setText(QString::fromStdString("V2: " + current_v2_tag_ +
                                    " / " + (current_v2_backbone_.empty() ? "默认" : current_v2_backbone_)));
    }
    version_row->addWidget(v2_model_label_, 1);

    download_v2_button_ = new QPushButton(qs("下载 V2 模型"), top_bar_panel_);
    if (current_model_version_ != shmtu::cas::ocr::ModelVersion::V2) {
        download_v2_button_->setVisible(false);
        v2_model_label_->setVisible(false);
    }
    version_row->addWidget(download_v2_button_);

    panel_layout->addLayout(version_row);

    // --- Option row ---
    auto* option_row = new QHBoxLayout();
    option_row->setSpacing(8);
    option_row->addWidget(createDimLabel(qs("精度:"), top_bar_panel_));

    precision_combo_ = new QComboBox(top_bar_panel_);
    precision_combo_->addItems(QStringList() << QStringLiteral("fp16") << QStringLiteral("fp32"));
    precision_combo_->setCurrentText(
        isSupportedPrecision(QString::fromStdString(launch_options_.precision))
            ? QString::fromStdString(launch_options_.precision)
            : QStringLiteral("fp16"));
    option_row->addWidget(precision_combo_);

    use_gpu_checkbox_ = new QCheckBox(qs("GPU加速"), top_bar_panel_);
    use_gpu_checkbox_->setChecked(launch_options_.use_gpu);
    option_row->addWidget(use_gpu_checkbox_);
    option_row->addStretch(1);

    panel_layout->addLayout(option_row);

    auto* progress_card = createCard(top_bar_panel_, "innerCard");
    auto* progress_layout = new QVBoxLayout(progress_card);
    progress_layout->setContentsMargins(12, 10, 12, 10);
    progress_layout->setSpacing(6);

    auto* progress_title = new QLabel(qs("模型文件状态"), progress_card);
    progress_title->setObjectName(QStringLiteral("statusTitle"));
    progress_layout->addWidget(progress_title);

    download_progress_bar_ = new QProgressBar(progress_card);
    download_progress_bar_->setRange(0, 100);
    download_progress_bar_->setValue(0);
    download_progress_bar_->setTextVisible(false);
    progress_layout->addWidget(download_progress_bar_);

    auto* progress_hint = createDimLabel(
        qs("缺少模型时可直接下载，已就绪时会自动尝试加载当前精度配置。"),
        progress_card);
    progress_hint->setWordWrap(true);
    progress_layout->addWidget(progress_hint);

    panel_layout->addWidget(progress_card);

    // --- Tag browsing section ---
    auto* tag_card = createCard(top_bar_panel_, "innerCard");
    auto* tag_layout = new QVBoxLayout(tag_card);
    tag_layout->setContentsMargins(12, 10, 12, 10);
    tag_layout->setSpacing(6);

    auto* tag_title = new QLabel(qs("V2 Release 标签"), tag_card);
    tag_title->setObjectName(QStringLiteral("statusTitle"));
    tag_layout->addWidget(tag_title);

    auto* tag_row = new QHBoxLayout();
    tag_row->setSpacing(8);

    tag_combo_ = new QComboBox(tag_card);
    tag_combo_->setMinimumWidth(140);
    tag_combo_->setPlaceholderText(qs("点击刷新获取标签..."));
    tag_row->addWidget(tag_combo_, 1);

    refresh_tags_button_ = new QPushButton(qs("刷新标签"), tag_card);
    tag_row->addWidget(refresh_tags_button_);
    tag_layout->addLayout(tag_row);

    // Model table per tag
    model_table_ = new QTableWidget(0, 5, tag_card);
    model_table_->setHorizontalHeaderLabels(
        QStringList() << qs("模型名称") << qs("Backbone") << qs("参数量(M)")
                      << qs("Val Acc") << qs("Test Acc"));
    model_table_->horizontalHeader()->setStretchLastSection(true);
    model_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    model_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    model_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    model_table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    model_table_->setMaximumHeight(160);
    model_table_->verticalHeader()->setVisible(false);
    tag_layout->addWidget(model_table_);

    auto* tag_hint = createDimLabel(
        qs("选择标签后自动获取模型清单，双击模型行可下载。"), tag_card);
    tag_hint->setWordWrap(true);
    tag_layout->addWidget(tag_hint);

    panel_layout->addWidget(tag_card);

    // --- Local model scanning section ---
    auto* local_card = createCard(top_bar_panel_, "innerCard");
    auto* local_layout = new QVBoxLayout(local_card);
    local_layout->setContentsMargins(12, 10, 12, 10);
    local_layout->setSpacing(6);

    auto* local_title = new QLabel(qs("本地模型"), local_card);
    local_title->setObjectName(QStringLiteral("statusTitle"));
    local_layout->addWidget(local_title);

    scan_local_button_ = new QPushButton(qs("扫描本地模型"), local_card);
    local_layout->addWidget(scan_local_button_);

    local_model_table_ = new QTableWidget(0, 5, local_card);
    local_model_table_->setHorizontalHeaderLabels(
        QStringList() << qs("版本") << qs("模型名称") << qs("精度")
                      << qs("状态") << qs("操作"));
    local_model_table_->horizontalHeader()->setStretchLastSection(true);
    local_model_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    local_model_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    local_model_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    local_model_table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    local_model_table_->setMaximumHeight(180);
    local_model_table_->verticalHeader()->setVisible(false);
    local_layout->addWidget(local_model_table_);

    auto* local_hint = createDimLabel(
        qs("扫描模型目录中的 .param + .bin 文件对，点击[加载]可直接使用。"), local_card);
    local_hint->setWordWrap(true);
    local_layout->addWidget(local_hint);

    panel_layout->addWidget(local_card);

    root_layout->addWidget(top_bar_panel_);

    connect(check_download_button_, &QPushButton::clicked, this,
            [this]() { onCheckDownloadModels(); });
    connect(download_v2_button_, &QPushButton::clicked, this,
            [this]() { onDownloadV2Model(); });
    connect(model_version_combo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onModelVersionChanged);
    connect(use_gpu_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        logMessage("use_gpu_checkbox toggled: " + boolToString(checked));
        if (!model_loaded_) {
            setStatusText(gpuStatusText(detectGpuAvailability(), checked));
        }
    });
    connect(refresh_tags_button_, &QPushButton::clicked, this,
            [this]() { onRefreshTags(); });
    connect(tag_combo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::onTagSelected);
    connect(scan_local_button_, &QPushButton::clicked, this,
            [this]() { onScanLocalModels(); });
    connect(model_table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int /*col*/) {
                // Double-click triggers download of the selected model
                if (cached_manifest_.models.empty()) return;
                const auto model_list = shmtu::cas::ocr::list_models(cached_manifest_);
                if (row >= 0 && row < static_cast<int>(model_list.size())) {
                    current_v2_backbone_ = model_list[row]->backbone;
                    if (!cached_manifest_tag_.empty()) {
                        current_v2_tag_ = cached_manifest_tag_;
                    }
                    updateV2ModelSettings();
                    onDownloadV2Model();
                }
            });
}

void MainWindow::buildMainArea(QVBoxLayout* root_layout) {
    main_panel_ = new QWidget(this);
    auto* main_layout = new QVBoxLayout(main_panel_);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(10);

    auto* grid_layout = new QHBoxLayout();
    grid_layout->setSpacing(10);

    buildLeftColumn(grid_layout);
    buildRightColumn(grid_layout);

    main_layout->addLayout(grid_layout, 1);
    buildBatchPane(main_layout);

    root_layout->addWidget(main_panel_, 1);
}

void MainWindow::buildLeftColumn(QHBoxLayout* parent_layout) {
    auto* left_panel = createCard(main_panel_);
    auto* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(12, 12, 12, 12);
    left_layout->setSpacing(10);

    auto* preview_frame = createCard(left_panel, "innerCard");
    auto* preview_layout = new QVBoxLayout(preview_frame);
    preview_layout->setContentsMargins(4, 4, 4, 4);

    preview_label_ = new ImageView(preview_frame);
    preview_label_->setObjectName(QStringLiteral("previewSurface"));
    preview_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_layout->addWidget(preview_label_, 1);

    auto* preview_mode_row = new QHBoxLayout();
    preview_mode_row->setContentsMargins(2, 0, 2, 0);
    preview_mode_row->setSpacing(8);
    preview_mode_row->addWidget(createDimLabel(qs("显示模式："), left_panel));

    preview_mode_combo_ = new QComboBox(left_panel);
    preview_mode_combo_->addItem(qs("自适应"), static_cast<int>(ImageView::DisplayMode::Fit));
    preview_mode_combo_->addItem(qs("填满"), static_cast<int>(ImageView::DisplayMode::Fill));
    preview_mode_combo_->addItem(qs("1:1"), static_cast<int>(ImageView::DisplayMode::Original));
    preview_mode_combo_->addItem(qs("平铺"), static_cast<int>(ImageView::DisplayMode::Tile));
    preview_mode_combo_->setCurrentIndex(0);
    preview_mode_row->addWidget(preview_mode_combo_);
    preview_mode_row->addStretch(1);

    source_path_label_ = createDimLabel(qs("未选择图片"), left_panel);
    source_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source_path_label_->setWordWrap(false);
    QFont dim_font = source_path_label_->font();
    dim_font.setPointSize(dim_font.pointSize() - 1);
    source_path_label_->setFont(dim_font);
    source_path_label_->setContentsMargins(2, 0, 2, 0);

    auto* result_group = createCard(left_panel, "innerCard");
    auto* result_layout = new QVBoxLayout(result_group);
    result_layout->setContentsMargins(14, 10, 14, 10);
    result_layout->setSpacing(2);

    result_expr_edit_ = new QLineEdit(qs("（暂无识别结果）"), result_group);
    result_expr_edit_->setObjectName(QStringLiteral("resultExpr"));
    setResultFieldStyle(result_expr_edit_, 34, true);
    result_expr_edit_->setReadOnly(true);
    result_expr_edit_->setAlignment(Qt::AlignCenter);

    elapsed_ms_edit_ = new QLineEdit(qs("用时：0.0 毫秒"), result_group);
    elapsed_ms_edit_->setObjectName(QStringLiteral("elapsedText"));
    setResultFieldStyle(elapsed_ms_edit_, std::max(8, elapsed_ms_edit_->font().pointSize() - 1),
                        false);
    elapsed_ms_edit_->setReadOnly(true);
    elapsed_ms_edit_->setAlignment(Qt::AlignCenter);

    result_layout->addWidget(createDimLabel(qs("识别结果"), result_group));
    result_layout->addWidget(result_expr_edit_);
    result_layout->addWidget(elapsed_ms_edit_);

    left_layout->addWidget(preview_frame, 1);
    left_layout->addLayout(preview_mode_row);
    left_layout->addWidget(source_path_label_);
    left_layout->addWidget(result_group);
    parent_layout->addWidget(left_panel, 1);

    connect(preview_mode_combo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (index < 0) {
            return;
        }

        const auto mode = static_cast<ImageView::DisplayMode>(
            preview_mode_combo_->itemData(index).toInt());
        preview_label_->setDisplayMode(mode);
        logMessage("preview mode changed, index=" + std::to_string(index));
    });
}

void MainWindow::buildRightColumn(QHBoxLayout* parent_layout) {
    auto* right_panel = createCard(main_panel_);
    right_panel->setMinimumWidth(240);
    right_panel->setMaximumWidth(260);

    auto* right_panel_layout = new QVBoxLayout(right_panel);
    right_panel_layout->setContentsMargins(12, 12, 12, 12);
    right_panel_layout->setSpacing(0);

    auto* action_scroll = new QScrollArea(right_panel);
    action_scroll->setWidgetResizable(true);
    action_scroll->setFrameShape(QFrame::NoFrame);
    action_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    action_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* action_container = new QWidget(action_scroll);
    auto* action_layout = new QVBoxLayout(action_container);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setSpacing(10);

    auto* acquire_layout = createSectionCard(action_container, qs("获取图片"));

    captcha_url_edit_ = new QLineEdit(QStringLiteral("https://cas.shmtu.edu.cn/cas/captcha"),
                                      action_container);
    captcha_url_edit_->setPlaceholderText(qs("验证码 URL"));
    captcha_url_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    acquire_layout->addWidget(captcha_url_edit_);

    download_captcha_button_ = new QPushButton(qs("下载验证码"), action_container);
    download_captcha_button_->setMinimumHeight(40);
    download_captcha_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont action_button_font = download_captcha_button_->font();
    action_button_font.setPointSize(16);
    download_captcha_button_->setFont(action_button_font);
    acquire_layout->addWidget(download_captcha_button_);

    open_local_button_ = new QPushButton(qs("打开本地图片"), action_container);
    open_local_button_->setMinimumHeight(40);
    open_local_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    open_local_button_->setFont(action_button_font);
    acquire_layout->addWidget(open_local_button_);

    auto* recognize_layout = createSectionCard(action_container, qs("识别"));

    recognize_button_ = new QPushButton(qs("▶ OCR 识别"), action_container);
    recognize_button_->setMinimumHeight(56);
    recognize_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont recognize_font = recognize_button_->font();
    recognize_font.setPointSize(20);
    recognize_font.setBold(true);
    recognize_button_->setFont(recognize_font);
    setAccentButton(recognize_button_);
    recognize_layout->addWidget(recognize_button_);

    auto* batch_layout = createSectionCard(action_container, qs("批量"));

    add_to_batch_button_ = new QPushButton(qs("加入批量列表"), action_container);
    add_to_batch_button_->setMinimumHeight(40);
    add_to_batch_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    batch_layout->addWidget(add_to_batch_button_);

    auto* model_layout = createSectionCard(action_container, qs("模型"));

    release_model_button_ = new QPushButton(qs("释放模型"), action_container);
    release_model_button_->setMinimumHeight(40);
    release_model_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    model_layout->addWidget(release_model_button_);
    action_layout->addStretch(1);

    auto* author_label = createDimLabel(QStringLiteral("Author: Haomin Kong"), action_container);
    QFont author_font = author_label->font();
    author_font.setPointSize(author_font.pointSize() - 2);
    author_label->setFont(author_font);
    author_label->setAlignment(Qt::AlignHCenter);
    action_layout->addWidget(author_label);

    action_scroll->setWidget(action_container);
    right_panel_layout->addWidget(action_scroll);

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
    bottom_bar_panel_ = createCard(this);

    auto* bottom_layout = new QHBoxLayout(bottom_bar_panel_);
    bottom_layout->setContentsMargins(12, 12, 12, 12);
    bottom_layout->setSpacing(12);

    auto* status_block = new QWidget(bottom_bar_panel_);
    auto* status_block_layout = new QVBoxLayout(status_block);
    status_block_layout->setContentsMargins(0, 0, 0, 0);
    status_block_layout->setSpacing(2);

    auto* status_title = new QLabel(qs("状态"), status_block);
    status_title->setObjectName(QStringLiteral("statusTitle"));
    status_block_layout->addWidget(status_title);

    status_label_ = new QLabel(qs("等待操作"), status_block);
    status_label_->setObjectName(QStringLiteral("statusMessage"));
    status_block_layout->addWidget(status_label_);
    bottom_layout->addWidget(status_block, 1);

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

    auto* batch_group = createCard(batch_content_widget_);
    auto* batch_group_layout = new QVBoxLayout(batch_group);
    batch_group_layout->setContentsMargins(12, 12, 12, 12);

    auto* button_row = new QHBoxLayout();
    batch_select_button_ = new QPushButton(qs("选择多张本地图片..."), batch_group);
    button_row->addWidget(batch_select_button_);

    batch_recognize_button_ = new QPushButton(qs("批量识别"), batch_group);
    setAccentButton(batch_recognize_button_);
    button_row->addWidget(batch_recognize_button_);

    batch_clear_button_ = new QPushButton(qs("清空列表"), batch_group);
    button_row->addWidget(batch_clear_button_);

    batch_stats_label_ = createDimLabel(qs("共 0 项 · 平均 0.0 毫秒"), batch_group);
    button_row->addWidget(batch_stats_label_, 1);
    batch_group_layout->addLayout(button_row);
    batch_group_layout->addSpacing(4);

    batch_scroll_area_ = new QScrollArea(batch_group);
    batch_scroll_area_->setWidgetResizable(true);
    batch_scroll_area_->setFrameShape(QFrame::NoFrame);
    batch_scroll_area_->setMinimumHeight(220);

    batch_list_widget_ = new QWidget(batch_scroll_area_);
    batch_list_layout_ = new QVBoxLayout(batch_list_widget_);
    batch_list_layout_->setContentsMargins(0, 0, 0, 0);
    batch_list_layout_->setSpacing(6);
    batch_list_layout_->addStretch(1);

    batch_scroll_area_->setWidget(batch_list_widget_);
    batch_group_layout->addWidget(batch_scroll_area_);

    batch_content_layout->addWidget(batch_group);
    wrapper_layout->addWidget(batch_content_widget_);
    parent_layout->addWidget(wrapper);

    connect(batch_toggle_button_, &QToolButton::toggled, this, [this](bool checked) {
        batch_toggle_button_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        batch_content_widget_->setVisible(checked);
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
        qs("海大验证码识别 - NCNN\n版本：") + QString::fromUtf8(SHMTU_CAS_OCR_GUI_VERSION) + qs(
            "\n\n上海海事大学 CAS 验证码 OCR 识别工具\n基于 NCNN 推理引擎，支持 CPU / Vulkan GPU 加速"));
}

void MainWindow::onCheckDownloadModels() {
    logMessage("onCheckDownloadModels: entered, model_version=" +
               shmtu::cas::ocr::model_version_to_string(current_model_version_));
    if (download_active_) {
        QMessageBox::information(this, qs("下载"), qs("模型正在下载中，请稍候..."));
        return;
    }

    // For V2, redirect to V2 download handler
    if (current_model_version_ == shmtu::cas::ocr::ModelVersion::V2) {
        onDownloadV2Model();
        return;
    }

    // V1 flow (original)
    const auto model_dir = model_dir_edit_->text().toStdString();
    const auto precision = precision_combo_->currentText().toStdString();
    const auto missing_files = missingModelFiles(model_dir, precision);
    {
        std::ostringstream oss;
        oss << "onCheckDownloadModels: settings"
            << ", model_dir=" << model_dir
            << ", precision=" << precision
            << ", use_gpu=" << boolToString(use_gpu_checkbox_->isChecked())
            << ", missing_count=" << missing_files.size();
        logMessage(oss.str());
    }

    if (missing_files.empty()) {
        logMessage("onCheckDownloadModels: all model files present, loading directly");
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
        logMessage("onCheckDownloadModels: user cancelled model download");
        return;
    }

    logMessage("onCheckDownloadModels: user accepted model download");
    startModelDownload(missing_files, true);
}

void MainWindow::onDownloadV2Model() {
    logMessage("onDownloadV2Model: entered");
    if (download_active_) {
        QMessageBox::information(this, qs("下载"), qs("模型正在下载中，请稍候..."));
        return;
    }

    const auto model_dir = model_dir_edit_->text().toStdString();
    const auto tag = current_v2_tag_.empty() ? std::string(DEFAULT_RELEASE_TAG) : current_v2_tag_;
    const auto precision = precision_combo_->currentText().toStdString();

    // Ask user: GitHub or Gitee
    QString prompt = qs("正在下载 V2 模型: ") + QString::fromStdString(tag) +
                     QStringLiteral("\n\n") +
                     qs("请选择下载源：\nGitHub - 国际\nGitee - 国内（推荐）");
    const auto answer = QMessageBox::question(this, qs("下载 V2 模型"), prompt,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::Yes);
    const bool use_gitee = (answer == QMessageBox::Yes);

    // Download manifest
    setStatusText(qs("正在获取模型清单..."));
    QApplication::processEvents();

    const auto source = use_gitee ? "gitee" : "github";
    long http_status = 0;
    std::string error_message;
    const auto manifest_json = downloadReleaseManifest(source, tag, http_status, error_message);
    if (manifest_json.empty()) {
        logMessage("onDownloadV2Model: manifest download failed, status=" +
                   std::to_string(http_status) + ", error=" + error_message);
        setStatusText(qs("获取模型清单失败"));
        QMessageBox::critical(this, qs("下载 V2 模型"),
                              qs("获取模型清单失败：\n") + QString::fromStdString(error_message));
        return;
    }

    auto manifest = shmtu::cas::ocr::parse_release_manifest(manifest_json);
    if (manifest.models.empty()) {
        logMessage("onDownloadV2Model: manifest parse failed or empty models");
        setStatusText(qs("模型清单解析失败"));
        QMessageBox::critical(this, qs("下载 V2 模型"), qs("模型清单解析失败或为空。"));
        return;
    }

    // Find the model matching backbone preference
    const shmtu::cas::ocr::ModelInfo* target_model = nullptr;
    if (!current_v2_backbone_.empty()) {
        for (const auto& model : manifest.models) {
            if (model.backbone == current_v2_backbone_) {
                target_model = &model;
                break;
            }
        }
    }
    if (!target_model) {
        target_model = &manifest.models[0];  // default to first model
    }

    logMessage("onDownloadV2Model: selected model=" + target_model->display_name +
               ", backbone=" + target_model->backbone);

    download_active_ = true;
    check_download_button_->setEnabled(false);
    download_v2_button_->setEnabled(false);

    const bool ok = downloadV2Artifact(
        *target_model, "ncnn", precision, model_dir, tag, use_gitee,
        [this](std::int64_t /*bytes_now*/, std::int64_t /*bytes_total*/) {
            QApplication::processEvents();
            return true;
        },
        error_message);

    download_active_ = false;
    check_download_button_->setEnabled(true);
    download_v2_button_->setEnabled(true);

    if (ok) {
        logMessage("onDownloadV2Model: download succeeded");
        download_progress_bar_->setValue(100);
        setStatusText(qs("V2 模型下载完成，正在加载..."));
        loadModelFromCurrentSettings();
    } else {
        logMessage("onDownloadV2Model: download failed, error=" + error_message);
        download_progress_bar_->setValue(0);
        setStatusText(qs("V2 模型下载失败"));
        QMessageBox::critical(this, qs("下载失败"),
                              qs("V2 模型下载失败：\n") + QString::fromStdString(error_message));
    }
}

void MainWindow::onModelVersionChanged(int index) {
    current_model_version_ =
        (index == 1) ? shmtu::cas::ocr::ModelVersion::V1 : shmtu::cas::ocr::ModelVersion::V2;

    const bool is_v2 = (current_model_version_ == shmtu::cas::ocr::ModelVersion::V2);
    if (download_v2_button_) download_v2_button_->setVisible(is_v2);
    if (v2_model_label_) v2_model_label_->setVisible(is_v2);

    switch (current_model_version_) {
        case shmtu::cas::ocr::ModelVersion::V2:
            logMessage("onModelVersionChanged: switched to V2");
            break;
        case shmtu::cas::ocr::ModelVersion::V1:
            logMessage("onModelVersionChanged: switched to V1");
            break;
    }

    // Release current model if loaded, since version changed
    if (model_loaded_) {
        logMessage("onModelVersionChanged: releasing loaded model due to version change");
        onReleaseModel();
    }
}

void MainWindow::updateV2ModelSettings() {
    if (!v2_model_label_) return;
    if (current_v2_tag_.empty() && current_v2_backbone_.empty()) {
        v2_model_label_->setText(qs("V2 模型: 默认 (最新)"));
    } else {
        v2_model_label_->setText(QString::fromStdString(
            "V2: " + (current_v2_tag_.empty() ? "最新" : current_v2_tag_) +
            " / " + (current_v2_backbone_.empty() ? "默认" : current_v2_backbone_)));
    }
}
void MainWindow::onRefreshTags() {
    logMessage("onRefreshTags: fetching v2 release tags");
    refresh_tags_button_->setEnabled(false);
    tag_combo_->clear();
    tag_combo_->addItem(qs("正在获取..."));
    QApplication::processEvents();

    long http_status = 0;
    std::string error_message;
    cached_tags_ = fetchV2ReleaseTags(http_status, error_message);

    tag_combo_->clear();
    if (cached_tags_.empty()) {
        tag_combo_->addItem(qs("获取失败"));
        logMessage("onRefreshTags: failed, status=" + std::to_string(http_status) +
                   ", error=" + error_message);
    } else {
        tag_combo_->addItem(qs("-- 选择标签 --"));
        for (const auto& tag : cached_tags_) {
            tag_combo_->addItem(QString::fromStdString(tag));
        }
        // Preserve current selection if still in the list
        if (!current_v2_tag_.empty()) {
            for (int i = 0; i < tag_combo_->count(); ++i) {
                if (tag_combo_->itemText(i).toStdString() == current_v2_tag_) {
                    tag_combo_->setCurrentIndex(i);
                    break;
                }
            }
        }
        logMessage("onRefreshTags: found " + std::to_string(cached_tags_.size()) + " tags");
    }
    refresh_tags_button_->setEnabled(true);
}

void MainWindow::onTagSelected(int index) {
    if (index <= 0 || cached_tags_.empty()) {
        // Clear model table when no tag is selected
        model_table_->setRowCount(0);
        cached_manifest_ = shmtu::cas::ocr::ReleaseManifest();
        cached_manifest_tag_.clear();
        return;
    }

    // index 0 is the placeholder, so tag index is index-1
    const int tag_idx = index - 1;
    if (tag_idx < 0 || tag_idx >= static_cast<int>(cached_tags_.size())) return;

    const auto& tag = cached_tags_[tag_idx];
    logMessage("onTagSelected: tag=" + tag);

    setStatusText(qs("正在获取 ") + QString::fromStdString(tag) + qs(" 模型清单..."));
    QApplication::processEvents();

    // Try Gitee first, then GitHub
    const char* sources[] = {"gitee", "github"};
    std::string manifest_json;
    long http_status = 0;
    std::string error_message;

    for (const char* src : sources) {
        manifest_json = downloadReleaseManifest(src, tag, http_status, error_message);
        if (!manifest_json.empty()) break;
    }

    if (manifest_json.empty()) {
        setStatusText(qs("获取模型清单失败"));
        logMessage("onTagSelected: manifest download failed for tag=" + tag);
        return;
    }

    cached_manifest_ = shmtu::cas::ocr::parse_release_manifest(manifest_json);
    cached_manifest_tag_ = tag;

    if (cached_manifest_.models.empty()) {
        setStatusText(qs("模型清单为空或解析失败"));
        logMessage("onTagSelected: manifest empty/parse failed for tag=" + tag);
        return;
    }

    refreshModelTable();
    setStatusText(qs("已加载 ") + QString::fromStdString(tag) + qs(" 模型清单 (") +
                  QString::number(cached_manifest_.models.size()) + qs(" 个模型)"));
}

void MainWindow::refreshModelTable() {
    model_table_->setRowCount(0);
    if (cached_manifest_.models.empty()) return;

    const auto model_list = shmtu::cas::ocr::list_models(cached_manifest_);
    model_table_->setRowCount(static_cast<int>(model_list.size()));

    for (int i = 0; i < static_cast<int>(model_list.size()); ++i) {
        const auto* m = model_list[i];
        if (!m) continue;

        auto* name_item = new QTableWidgetItem(QString::fromStdString(m->display_name));
        auto* backbone_item = new QTableWidgetItem(QString::fromStdString(m->backbone));

        // Highlight matching backbone
        if (!current_v2_backbone_.empty() && m->backbone == current_v2_backbone_) {
            QFont bold_font = name_item->font();
            bold_font.setBold(true);
            name_item->setFont(bold_font);
            backbone_item->setFont(bold_font);
        }

        std::string params_str = "-";
        if (m->model_size_m.has_value()) {
            std::ostringstream oss;
            oss.precision(2);
            oss << std::fixed << *m->model_size_m;
            params_str = oss.str();
        }

        auto fmt_acc = [](const std::optional<double>& v) -> QString {
            if (!v.has_value()) return qs("-");
            std::ostringstream oss;
            oss.precision(1);
            oss << std::fixed << (*v * 100.0) << "%";
            return QString::fromStdString(oss.str());
        };

        model_table_->setItem(i, 0, name_item);
        model_table_->setItem(i, 1, backbone_item);
        model_table_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(params_str)));
        model_table_->setItem(i, 3, new QTableWidgetItem(
            m->metrics.has_value() ? fmt_acc(m->metrics->val_acc_expression) : qs("-")));
        model_table_->setItem(i, 4, new QTableWidgetItem(
            m->metrics.has_value() ? fmt_acc(m->metrics->test_acc_expression) : qs("-")));
    }

    model_table_->resizeColumnsToContents();
}

void MainWindow::onScanLocalModels() {
    logMessage("onScanLocalModels: scanning model dir");
    const auto model_dir = model_dir_edit_->text().toStdString();
    local_models_.clear();
    local_model_table_->setRowCount(0);

    namespace fs = std::filesystem;
    std::error_code ec;
    if (model_dir.empty() || !fs::is_directory(model_dir, ec)) {
        setStatusText(qs("模型目录不存在或未设置"));
        return;
    }

    // --- Scan V1 models ---
    // V1 models: resnet18_equal_symbol_latest.{precision}.param/.bin
    //            resnet18_operator_latest.{precision}.param/.bin
    //            resnet34_digit_latest.{precision}.param/.bin
    static const struct {
        const char* stem_pattern;  // with %s for precision
        const char* display_name;
    } v1_model_patterns[] = {
        {"resnet18_equal_symbol_latest.%s", "ResNet18 等号符号"},
        {"resnet18_operator_latest.%s",     "ResNet18 运算符"},
        {"resnet34_digit_latest.%s",        "ResNet34 数字"},
    };

    for (const auto& pat : v1_model_patterns) {
        for (const char* prec : {"fp16", "fp32"}) {
            char stem_buf[256];
            std::snprintf(stem_buf, sizeof(stem_buf), pat.stem_pattern, prec);
            const std::string param_name = std::string(stem_buf) + ".param";
            const std::string bin_name = std::string(stem_buf) + ".bin";
            const auto param_path = fs::path(model_dir) / param_name;
            const auto bin_path = fs::path(model_dir) / bin_name;

            if (fs::exists(param_path, ec) && fs::exists(bin_path, ec)) {
                LocalModelEntry entry;
                entry.version = "v1";
                entry.display_name = pat.display_name;
                entry.backbone.clear();
                entry.precision = prec;
                entry.param_path = param_path.string();
                entry.bin_path = bin_path.string();
                local_models_.push_back(std::move(entry));
            }
        }
    }

    // --- Scan V2 models ---
    // V2 file pattern: backbone.family.version.precision.param/.bin
    // Known stems from infer_asset_stem_from_dir
    static const struct {
        const char* stem;          // e.g. "mobilenet_v3_small.trislot_decoder.v2_0"
        const char* backbone_name; // e.g. "mobilenet_v3_small"
        const char* display_name;  // e.g. "MobileNetV3-Small TriSlot"
    } v2_stems[] = {
        {"mobilenet_v3_small.trislot_decoder.v2_0",    "mobilenet_v3_small",    "MobileNetV3-Small TriSlot"},
        {"mobilenetv4_conv_small.trislot_decoder.v2_0", "mobilenetv4_conv_small", "MobileNetV4-Small TriSlot"},
    };

    for (const auto& s : v2_stems) {
        for (const char* prec : {"fp16", "fp32"}) {
            const std::string param_name = std::string(s.stem) + "." + prec + ".param";
            const std::string bin_name = std::string(s.stem) + "." + prec + ".bin";
            const auto param_path = fs::path(model_dir) / param_name;
            const auto bin_path = fs::path(model_dir) / bin_name;

            if (fs::exists(param_path, ec) && fs::exists(bin_path, ec)) {
                LocalModelEntry entry;
                entry.version = "v2";
                entry.display_name = s.display_name;
                entry.backbone = s.backbone_name;
                entry.precision = prec;
                entry.param_path = param_path.string();
                entry.bin_path = bin_path.string();
                local_models_.push_back(std::move(entry));
            }
        }
    }

    // Also do a generic .param/.bin pair scan for any files we may have missed
    // that match the pattern: *.fp16.param / *.fp32.param with matching .bin
    try {
        for (const auto& entry : fs::directory_iterator(model_dir)) {
            if (!entry.is_regular_file(ec)) continue;
            const auto path = entry.path();
            if (path.extension() != ".param") continue;

            const std::string param_filename = path.filename().string();
            // Must end with .fp16.param or .fp32.param
            for (const char* prec : {"fp16", "fp32"}) {
                const std::string suffix = std::string(".") + prec + ".param";
                if (param_filename.size() > suffix.size() &&
                    param_filename.substr(param_filename.size() - suffix.size()) == suffix) {

                    const std::string stem = param_filename.substr(0, param_filename.size() - suffix.size());
                    const std::string bin_filename = stem + "." + prec + ".bin";
                    const auto bin_path = fs::path(model_dir) / bin_filename;

                    if (fs::exists(bin_path, ec)) {
                        // Check if we already have this entry
                        bool already_have = false;
                        for (const auto& existing : local_models_) {
                            if (existing.param_path == path.string()) {
                                already_have = true;
                                break;
                            }
                        }
                        if (!already_have) {
                            LocalModelEntry lme;
                            lme.version = "v2";
                            lme.display_name = stem;
                            lme.precision = prec;
                            lme.param_path = path.string();
                            lme.bin_path = bin_path.string();
                            // Try to extract backbone from stem (first dot-delimited component)
                            auto dot_pos = stem.find('.');
                            lme.backbone = (dot_pos != std::string::npos) ? stem.substr(0, dot_pos) : stem;
                            local_models_.push_back(std::move(lme));
                        }
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        logMessage("onScanLocalModels: directory_iterator error: " + std::string(e.what()));
    }

    // Populate the table
    local_model_table_->setRowCount(static_cast<int>(local_models_.size()));

    for (int i = 0; i < static_cast<int>(local_models_.size()); ++i) {
        const auto& model = local_models_[i];

        auto* ver_item = new QTableWidgetItem(
            model.version == "v1" ? qs("V1") : qs("V2"));
        auto* name_item = new QTableWidgetItem(QString::fromStdString(model.display_name));
        auto* prec_item = new QTableWidgetItem(QString::fromStdString(model.precision));

        // Status: check if this matches current settings
        bool is_current = false;
        if (model.version == "v1" && current_model_version_ == shmtu::cas::ocr::ModelVersion::V1) {
            is_current = true;
        } else if (model.version == "v2" && current_model_version_ == shmtu::cas::ocr::ModelVersion::V2) {
            if (current_v2_backbone_.empty() || model.backbone == current_v2_backbone_) {
                is_current = (model.precision == precision_combo_->currentText().toStdString());
            }
        }

        auto* status_item = new QTableWidgetItem(
            is_current ? qs("当前") : qs("可用"));
        if (is_current) {
            status_item->setForeground(QColor(COLOR_SUCCESS));
        }

        auto* load_button = new QPushButton(qs("加载"));
        load_button->setProperty("modelRow", i);
        connect(load_button, &QPushButton::clicked, this, [this, i]() {
            onLoadLocalModel(i);
        });

        local_model_table_->setItem(i, 0, ver_item);
        local_model_table_->setItem(i, 1, name_item);
        local_model_table_->setItem(i, 2, prec_item);
        local_model_table_->setItem(i, 3, status_item);
        local_model_table_->setCellWidget(i, 4, load_button);
    }

    local_model_table_->resizeColumnsToContents();
    logMessage("onScanLocalModels: found " + std::to_string(local_models_.size()) + " models");
    setStatusText(qs("扫描完成，找到 ") + QString::number(local_models_.size()) + qs(" 个本地模型"));
}

void MainWindow::onLoadLocalModel(int row) {
    if (row < 0 || row >= static_cast<int>(local_models_.size())) return;

    const auto& model = local_models_[row];
    logMessage("onLoadLocalModel: row=" + std::to_string(row) +
               ", version=" + model.version +
               ", backbone=" + model.backbone +
               ", precision=" + model.precision);

    // Update model version
    if (model.version == "v1") {
        current_model_version_ = shmtu::cas::ocr::ModelVersion::V1;
        model_version_combo_->setCurrentIndex(1);
    } else {
        current_model_version_ = shmtu::cas::ocr::ModelVersion::V2;
        model_version_combo_->setCurrentIndex(0);
        if (!model.backbone.empty()) {
            current_v2_backbone_ = model.backbone;
        }
    }

    // Update precision
    const int prec_index = precision_combo_->findText(QString::fromStdString(model.precision));
    if (prec_index >= 0) {
        precision_combo_->setCurrentIndex(prec_index);
    }

    // Update v2 tag info
    if (model.version == "v2") {
        updateV2ModelSettings();
    }

    // Load the model
    loadModelFromCurrentSettings();

    // Refresh local model table to update status
    onScanLocalModels();
}



void MainWindow::startModelDownload(const std::vector<std::string>& missing_files, bool use_gitee) {
    {
        std::ostringstream oss;
        oss << "startModelDownload: begin"
            << ", missing_count=" << missing_files.size()
            << ", use_gitee=" << boolToString(use_gitee);
        logMessage(oss.str());
    }
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
            if (!filename.empty()) {
                logMessage("startModelDownload: progress, file=" + filename +
                           ", completed=" + std::to_string(completed_files) +
                           ", total=" + std::to_string(total_files) +
                           ", progress=" + std::to_string(pct) + "%");
            }
            QApplication::processEvents();
            return true;
        },
        error_message);

    download_active_ = false;
    check_download_button_->setEnabled(true);

    if (all_ok) {
        logMessage("startModelDownload: all files downloaded successfully");
        download_progress_bar_->setValue(100);
        setStatusText(qs("模型下载完成，正在加载模型..."));
        loadModelFromCurrentSettings();
        return;
    }

    logMessage("startModelDownload: download failed, error=" + error_message);
    download_progress_bar_->setValue(0);
    setStatusText(qs("模型下载失败"));
    QMessageBox::critical(this, qs("下载失败"),
                          qs("部分模型文件下载失败：\n\n") + QString::fromStdString(error_message));
}

void MainWindow::loadModelFromCurrentSettings() {
    const auto model_dir = model_dir_edit_->text().toStdString();
    const auto precision = precision_combo_->currentText().toStdString();
    const bool use_gpu = use_gpu_checkbox_->isChecked();
    {
        std::ostringstream oss;
        oss << "loadModelFromCurrentSettings: begin"
            << ", model_dir=" << model_dir
            << ", precision=" << precision
            << ", use_gpu=" << boolToString(use_gpu)
            << ", model_version=" << shmtu::cas::ocr::model_version_to_string(current_model_version_);
        logMessage(oss.str());
    }

    ocr_ = std::make_unique<shmtu::cas::ocr::CasOcr>(model_dir);
    if (!ocr_->load_model(precision.empty() ? "fp16" : precision, use_gpu, 0, current_model_version_)) {
        logMessage("loadModelFromCurrentSettings: failed");
        model_loaded_ = false;
        updateModelStatusUi();
        setStatusText(qs("模型加载失败"));
        QMessageBox::critical(this, qs("加载模型"),
                              qs("无法加载 NCNN 模型文件，请检查模型目录路径。\n\n"
                                 "目录应包含 .param 和 .bin 文件。"));
        return;
    }

    model_loaded_ = true;
    logMessage("loadModelFromCurrentSettings: succeeded");
    updateModelStatusUi();
}

void MainWindow::onReleaseModel() {
    logMessage("onReleaseModel: requested");
    if (ocr_) {
        ocr_->release();
        logMessage("onReleaseModel: model released");
    }
    model_loaded_ = false;
    updateModelStatusUi();
    setStatusText(qs("模型已释放"));
}

void MainWindow::updateGpuAvailabilityUi() {
    if (!use_gpu_checkbox_) {
        return;
    }

    const GpuAvailabilityState state = detectGpuAvailability();
    const bool can_use_gpu = state.built_with_vulkan && state.runtime_available;

    use_gpu_checkbox_->setText(gpuCheckboxText(state));
    use_gpu_checkbox_->setToolTip(gpuTooltipText(state));
    if (!can_use_gpu) {
        use_gpu_checkbox_->setChecked(false);
        launch_options_.use_gpu = false;
    }
    use_gpu_checkbox_->setEnabled(!model_loaded_ && can_use_gpu);

    std::ostringstream oss;
    oss << "updateGpuAvailabilityUi: applied"
        << ", built_with_vulkan=" << boolToString(state.built_with_vulkan)
        << ", runtime_available=" << boolToString(state.runtime_available)
        << ", checked=" << boolToString(use_gpu_checkbox_->isChecked())
        << ", enabled=" << boolToString(use_gpu_checkbox_->isEnabled());
    logMessage(oss.str());
}

void MainWindow::updateModelStatusUi() {
    model_status_label_->setText(model_loaded_ ? qs("模型已就绪") : qs("模型未就绪"));
    model_status_label_->setStyleSheet(QStringLiteral(
        "QLabel#statusBadge { background: %1; color: white; border-radius: 12px; padding: 4px 10px; font-size: 12px; font-weight: 600; }")
        .arg(model_loaded_ ? COLOR_SUCCESS : COLOR_BADGE_IDLE));
    precision_combo_->setEnabled(!model_loaded_);
    updateGpuAvailabilityUi();
    recognize_button_->setEnabled(model_loaded_);
    add_to_batch_button_->setEnabled(model_loaded_);
    batch_recognize_button_->setEnabled(model_loaded_);
    {
        std::ostringstream oss;
        oss << "updateModelStatusUi: applied"
            << ", model_loaded=" << boolToString(model_loaded_)
            << ", precision_enabled=" << boolToString(precision_combo_->isEnabled())
            << ", gpu_enabled=" << boolToString(use_gpu_checkbox_->isEnabled());
        logMessage(oss.str());
    }
}

void MainWindow::onDownloadCaptcha() {
    const auto url = captcha_url_edit_->text().trimmed();
    logMessage("onDownloadCaptcha: requested, url=" + url.toStdString());
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
        std::ostringstream oss;
        oss << "onDownloadCaptcha: failed"
            << ", url=" << url.toStdString()
            << ", ok=" << boolToString(ok)
            << ", http_status=" << http_status
            << ", bytes=" << image_data.size();
        if (!error_message.empty()) {
            oss << ", error=" << error_message;
        }
        logMessage(oss.str());
        setStatusText(qs("下载验证码失败"));
        QMessageBox::critical(this, qs("下载验证码"), qs("无法下载验证码图片，请检查 URL。"));
        return;
    }

    const auto unique_name =
        QStringLiteral("captcha_download_%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    const auto temp_file =
        std::filesystem::temp_directory_path() / unique_name.toStdString();
    {
        std::ofstream ofs(temp_file, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(image_data.data()),
                  static_cast<std::streamsize>(image_data.size()));
    }

    current_image_path_ = QString::fromStdString(temp_file.string());
    current_image_source_name_ = unique_name + QStringLiteral("  |  ") + url;
    current_image_data_ = std::move(image_data);
    {
        std::ostringstream oss;
        oss << "onDownloadCaptcha: succeeded"
            << ", temp_file=" << current_image_path_.toStdString()
            << ", bytes=" << current_image_data_.size();
        logMessage(oss.str());
    }
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
        logMessage("onOpenLocalImage: user cancelled file selection");
        return;
    }

    current_image_path_ = path;
    current_image_source_name_ = path;
    current_image_data_.clear();
    logMessage("onOpenLocalImage: selected path=" + path.toStdString());
    displayImage(path);
    source_path_label_->setText(path);
    setStatusText(qs("已加载本地图片"));
}

void MainWindow::onOcrRecognize() {
    logMessage("onOcrRecognize: requested");
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
        logMessage("onOcrRecognize: using in-memory image data, bytes=" +
                   std::to_string(current_image_data_.size()));
        result = ocr_->predict(current_image_data_);
    } else {
        logMessage("onOcrRecognize: using image path=" + current_image_path_.toStdString());
        result = ocr_->predict(current_image_path_.toStdString());
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    {
        std::ostringstream oss;
        oss << "onOcrRecognize: finished"
            << ", success=" << boolToString(result.success)
            << ", elapsed_ms=" << elapsed_ms;
        if (result.success) {
            oss << ", expression=" << result.expression << ", result=" << result.result;
        } else {
            oss << ", error=" << result.error;
        }
        logMessage(oss.str());
    }
    displayResult(result, elapsed_ms);
}

void MainWindow::onAddToBatch() {
    logMessage("onAddToBatch: requested");
    if (current_image_path_.isEmpty()) {
        QMessageBox::warning(this, qs("批量"), qs("请先获取验证码图片。"));
        return;
    }

    BatchItem item;
    item.file_path = current_image_path_;
    item.source_name = current_image_source_name_.isEmpty()
                           ? source_path_label_->text().trimmed()
                           : current_image_source_name_;
    item.status = qs("待识别");
    item.image_data = current_image_data_;
    {
        std::ostringstream oss;
        oss << "onAddToBatch: add item"
            << ", file_path=" << item.file_path.toStdString()
            << ", source_name=" << item.source_name.toStdString()
            << ", has_memory_data=" << boolToString(!item.image_data.empty())
            << ", bytes=" << item.image_data.size();
        logMessage(oss.str());
    }
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
        logMessage("onBatchSelectFiles: user cancelled file selection");
        return;
    }

    logMessage("onBatchSelectFiles: selected_count=" + std::to_string(paths.size()));

    for (const auto& path : paths) {
        BatchItem item;
        item.file_path = path;
        item.source_name = path;
        item.status = qs("待识别");
        batch_items_.push_back(std::move(item));
        logMessage("onBatchSelectFiles: appended path=" + path.toStdString());
    }

    refreshBatchTable();
    setStatusText(qs("已添加 ") + QString::number(paths.size()) + qs(" 个文件到批量列表"));
}

void MainWindow::onBatchRecognize() {
    {
        std::ostringstream oss;
        oss << "onBatchRecognize: requested"
            << ", item_count=" << batch_items_.size();
        logMessage(oss.str());
    }
    if (!ensureModelLoaded()) {
        return;
    }
    if (batch_items_.empty()) {
        QMessageBox::warning(this, qs("批量识别"), qs("批量列表为空，请先添加图片。"));
        return;
    }

    setStatusText(qs("批量识别中..."));

    int success_count = 0;
    for (size_t index = 0; index < batch_items_.size(); ++index) {
        auto& item = batch_items_[index];
        if (item.status == qs("成功") || item.status == qs("失败")) {
            logMessage("onBatchRecognize: skip finalized item, index=" + std::to_string(index));
            continue;
        }

        logMessage("onBatchRecognize: processing item, index=" + std::to_string(index) +
                   ", source_name=" + item.source_name.toStdString());
        const auto start = std::chrono::steady_clock::now();
        const auto result = item.image_data.empty()
                                ? ocr_->predict(item.file_path.toStdString())
                                : ocr_->predict(item.image_data);
        const auto end = std::chrono::steady_clock::now();
        item.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (result.success) {
            item.result_expr = QString::fromStdString(result.expression);
            item.status = qs("成功");
            success_count++;
            logMessage("onBatchRecognize: item succeeded, index=" + std::to_string(index) +
                       ", expression=" + result.expression +
                       ", result=" + std::to_string(result.result) +
                       ", elapsed_ms=" + std::to_string(item.elapsed_ms));
        } else {
            item.result_expr = QString::fromStdString(result.error);
            item.status = qs("失败");
            logMessage("onBatchRecognize: item failed, index=" + std::to_string(index) +
                       ", error=" + result.error +
                       ", elapsed_ms=" + std::to_string(item.elapsed_ms));
        }
    }

    logMessage("onBatchRecognize: finished, success_count=" + std::to_string(success_count) +
               ", total_count=" + std::to_string(batch_items_.size()));
    refreshBatchTable();
    setStatusText(qs("批量识别完成: ") + QString::number(success_count) + QLatin1Char('/') +
                  QString::number(batch_items_.size()) + qs(" 成功"));
}

void MainWindow::onBatchClear() {
    logMessage("onBatchClear: clearing items, previous_count=" + std::to_string(batch_items_.size()));
    batch_items_.clear();
    refreshBatchTable();
    setStatusText(qs("批量列表已清空"));
}

void MainWindow::refreshBatchTable() {
    if (!batch_list_layout_) {
        return;
    }
    logMessage("refreshBatchTable: rebuilding UI, item_count=" + std::to_string(batch_items_.size()));

    while (auto* item = batch_list_layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    for (const auto& item : batch_items_) {
        batch_list_layout_->addWidget(createBatchItemCard(item, batch_list_widget_));
    }
    batch_list_layout_->addStretch(1);

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
    {
        std::ostringstream oss;
        oss << "updateBatchStats: computed"
            << ", total_items=" << batch_items_.size()
            << ", recognized=" << recognized
            << ", avg_ms=" << avg;
        logMessage(oss.str());
    }
    batch_stats_label_->setText(qs("共 ") + QString::number(batch_items_.size()) + qs(" 项 · 平均 ") +
                                QString::number(avg, 'f', 1) + qs(" 毫秒"));
}

QWidget* MainWindow::createBatchItemCard(const BatchItem& item, QWidget* parent) {
    auto* card = createCard(parent, "batchItemCard");
    auto* card_layout = new QHBoxLayout(card);
    card_layout->setContentsMargins(8, 8, 8, 8);
    card_layout->setSpacing(10);

    auto* thumb_card = createCard(card, "batchThumbCard");
    thumb_card->setFixedSize(110, 44);
    auto* thumb_layout = new QVBoxLayout(thumb_card);
    thumb_layout->setContentsMargins(2, 2, 2, 2);

    auto* thumb_label = new QLabel(thumb_card);
    thumb_label->setAlignment(Qt::AlignCenter);
    thumb_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QPixmap pixmap;
    if (!item.image_data.empty()) {
        pixmap.loadFromData(reinterpret_cast<const uchar*>(item.image_data.data()),
                            static_cast<uint>(item.image_data.size()));
    } else if (!item.file_path.isEmpty()) {
        pixmap.load(item.file_path);
    }
    if (!pixmap.isNull()) {
        thumb_label->setPixmap(
            pixmap.scaled(QSize(104, 40), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    thumb_layout->addWidget(thumb_label);
    card_layout->addWidget(thumb_card);

    auto* center_layout = new QVBoxLayout();
    center_layout->setSpacing(2);

    auto* source_label = createDimLabel(item.source_name, card);
    source_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    source_label->setWordWrap(false);
    source_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    center_layout->addWidget(source_label);

    auto* expr_label = new QLabel(item.result_expr.isEmpty() ? qs("（待识别）") : item.result_expr, card);
    QFont expr_font = expr_label->font();
    expr_font.setPointSize(16);
    expr_font.setBold(true);
    expr_label->setFont(expr_font);
    expr_label->setStyleSheet(QStringLiteral("color: %1;").arg(COLOR_TEXT));
    center_layout->addWidget(expr_label);
    card_layout->addLayout(center_layout, 1);

    auto* right_layout = new QVBoxLayout();
    right_layout->setSpacing(2);

    auto* status_label = createDimLabel(item.status.isEmpty() ? qs("待识别") : item.status, card);
    status_label->setAlignment(Qt::AlignRight);
    if (item.status == qs("成功")) {
        status_label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(COLOR_SUCCESS));
    } else if (item.status == qs("失败")) {
        status_label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(COLOR_ERROR));
    }
    right_layout->addWidget(status_label);

    auto* elapsed_label = createDimLabel(
        item.elapsed_ms > 0.0 ? QString::number(item.elapsed_ms, 'f', 1) + qs(" ms") : QString(),
        card);
    elapsed_label->setAlignment(Qt::AlignRight);
    right_layout->addWidget(elapsed_label);

    card_layout->addLayout(right_layout);

    return card;
}

bool MainWindow::ensureModelLoaded() {
    if (!model_loaded_ || !ocr_ || !ocr_->is_loaded()) {
        logMessage("ensureModelLoaded: model not loaded");
        QMessageBox::warning(this, qs("操作"), qs("请先加载模型。"));
        return false;
    }
    logMessage("ensureModelLoaded: model ready");
    return true;
}

void MainWindow::displayImage(const QString& path) {
    logMessage("displayImage: loading path=" + path.toStdString());
    const QPixmap pixmap(path);
    if (pixmap.isNull()) {
        logMessage("displayImage: failed to load " + path.toStdString());
        return;
    }

    {
        std::ostringstream oss;
        oss << "displayImage: loaded"
            << ", width=" << pixmap.width()
            << ", height=" << pixmap.height();
        logMessage(oss.str());
    }
    preview_label_->setPixmap(pixmap);
}

void MainWindow::displayResult(const PredictResult& result, double elapsed_ms) {
    {
        std::ostringstream oss;
        oss << "displayResult: rendering"
            << ", success=" << boolToString(result.success)
            << ", elapsed_ms=" << elapsed_ms;
        if (result.success) {
            oss << ", expression=" << result.expression << ", result=" << result.result;
        } else {
            oss << ", error=" << result.error;
        }
        logMessage(oss.str());
    }
    if (result.success) {
        result_expr_edit_->setProperty("error", false);
        result_expr_edit_->style()->unpolish(result_expr_edit_);
        result_expr_edit_->style()->polish(result_expr_edit_);
        setAdaptiveLineEditText(result_expr_edit_, QString::fromStdString(result.expression), 34, 14);
        setAdaptiveLineEditText(
            elapsed_ms_edit_, qs("用时：") + QString::number(elapsed_ms, 'f', 1) + qs(" 毫秒"), 14, 9);
        setStatusText(qs("识别成功"));
        return;
    }

    result_expr_edit_->setProperty("error", true);
    result_expr_edit_->style()->unpolish(result_expr_edit_);
    result_expr_edit_->style()->polish(result_expr_edit_);
    setAdaptiveLineEditText(result_expr_edit_, QString::fromStdString(result.error), 28, 11);
    setAdaptiveLineEditText(
        elapsed_ms_edit_, qs("用时：") + QString::number(elapsed_ms, 'f', 1) + qs(" 毫秒"), 14, 9);
    setStatusText(qs("识别失败"));
}

void MainWindow::updateResultTextLayout() {
    if (!result_expr_edit_ || !elapsed_ms_edit_) {
        return;
    }

    const bool is_error = result_expr_edit_->property("error").toBool();
    setAdaptiveLineEditText(result_expr_edit_, result_expr_edit_->text(), is_error ? 28 : 34,
                            is_error ? 11 : 14);
    setAdaptiveLineEditText(elapsed_ms_edit_, elapsed_ms_edit_->text(), 14, 9);
}

void MainWindow::setAdaptiveLineEditText(QLineEdit* edit,
                                         const QString& text,
                                         const int max_point_size,
                                         const int min_point_size,
                                         const Qt::Alignment alignment) {
    if (!edit) {
        return;
    }

    edit->setText(text);
    edit->setAlignment(alignment);
    edit->setToolTip(text);
    edit->setCursorPosition(0);

    QFont font = edit->font();
    const bool is_result_field = edit == result_expr_edit_;
    const int available_width = std::max(40, edit->contentsRect().width() - 12);
    int fitted_point_size = min_point_size;

    for (int point_size = max_point_size; point_size >= min_point_size; --point_size) {
        font.setPointSize(point_size);
        font.setBold(is_result_field);
        const QFontMetrics metrics(font);
        if (metrics.horizontalAdvance(text) <= available_width) {
            fitted_point_size = point_size;
            break;
        }
    }

    font.setPointSize(fitted_point_size);
    font.setBold(is_result_field);
    edit->setFont(font);
}

void MainWindow::setStatusText(const QString& text) {
    if (status_label_) {
        status_label_->setText(text);
    }
    logMessage("setStatusText: " + text.toStdString());
}

}  // namespace shmtu::cas::ocr::gui
