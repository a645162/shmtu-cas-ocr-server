#include <shmtu/cas_ocr/gui/image_view.h>

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

namespace shmtu::cas::ocr::gui {

ImageView::ImageView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(480, 260);
}

void ImageView::setPixmap(const QPixmap& pixmap) {
    pixmap_ = pixmap;
    scaled_pixmap_ = QPixmap();
    scaled_target_size_ = QSize();
    updateGeometry();
    refreshScaledPixmap();
    repaint();
}

void ImageView::setDisplayMode(const DisplayMode mode) {
    if (display_mode_ == mode) {
        return;
    }

    display_mode_ = mode;
    scaled_pixmap_ = QPixmap();
    scaled_target_size_ = QSize();
    refreshScaledPixmap();
    repaint();
}

void ImageView::clear() {
    pixmap_ = QPixmap();
    scaled_pixmap_ = QPixmap();
    scaled_target_size_ = QSize();
    update();
}

bool ImageView::hasPixmap() const {
    return !pixmap_.isNull();
}

const QPixmap& ImageView::pixmap() const {
    return pixmap_;
}

ImageView::DisplayMode ImageView::displayMode() const {
    return display_mode_;
}

void ImageView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    refreshScaledPixmap();

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (pixmap_.isNull()) {
        return;
    }

    if (display_mode_ == DisplayMode::Tile) {
        painter.drawTiledPixmap(rect(), pixmap_);
        return;
    }

    if (scaled_pixmap_.isNull()) {
        return;
    }

    const QPoint top_left((width() - scaled_pixmap_.width()) / 2,
                          (height() - scaled_pixmap_.height()) / 2);
    painter.drawPixmap(top_left, scaled_pixmap_);
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshScaledPixmap();
}

QSize ImageView::sizeHint() const {
    return QSize(520, 300);
}

void ImageView::refreshScaledPixmap() {
    if (pixmap_.isNull()) {
        scaled_pixmap_ = QPixmap();
        scaled_target_size_ = QSize();
        return;
    }

    QSize target_size = contentsRect().size();
    if (target_size.isEmpty()) {
        target_size = size();
    }
    if (target_size.isEmpty()) {
        target_size = minimumSize();
    }
    if (target_size.isEmpty()) {
        target_size = sizeHint();
    }
    if (target_size.isEmpty()) {
        return;
    }

    if (display_mode_ == DisplayMode::Tile) {
        scaled_pixmap_ = QPixmap();
        scaled_target_size_ = target_size;
        return;
    }

    if (target_size == scaled_target_size_ && !scaled_pixmap_.isNull()) {
        return;
    }

    scaled_target_size_ = target_size;
    Qt::AspectRatioMode aspect_ratio_mode = Qt::KeepAspectRatio;
    if (display_mode_ == DisplayMode::Fill) {
        aspect_ratio_mode = Qt::KeepAspectRatioByExpanding;
    } else if (display_mode_ == DisplayMode::Original) {
        scaled_pixmap_ = pixmap_;
        return;
    }

    scaled_pixmap_ = pixmap_.scaled(target_size, aspect_ratio_mode, Qt::SmoothTransformation);
}

}  // namespace shmtu::cas::ocr::gui
