#pragma once

#include <QPixmap>
#include <QWidget>

namespace shmtu::cas::ocr::gui {

class ImageView final : public QWidget {
public:
    enum class DisplayMode {
        Fit,
        Fill,
        Original,
        Tile
    };

    explicit ImageView(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pixmap);
    void setDisplayMode(DisplayMode mode);
    void clear();
    bool hasPixmap() const;
    const QPixmap& pixmap() const;
    DisplayMode displayMode() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;

private:
    void refreshScaledPixmap();

    QPixmap pixmap_;
    QPixmap scaled_pixmap_;
    QSize scaled_target_size_;
    DisplayMode display_mode_ = DisplayMode::Fit;
};

}  // namespace shmtu::cas::ocr::gui
