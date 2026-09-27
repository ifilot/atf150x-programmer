// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Physical row map rendering, zooming and hover descriptions.
 */
#include "gui/src/fuse_map_widget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

#include "gui/src/jedec_document.h"

namespace atf {
namespace gui {
namespace {

constexpr int kMargin = 8;
constexpr int kHeaderHeight = 20;
// Display columns per configuration, JTAG and UES word.
constexpr int kSpecialRowWidth = 4;

/**
 * @brief Blends a color toward white.
 *
 * @param[in] color Base color.
 * @param[in] amount Fraction of white from 0.0 to 1.0.
 * @return Blended opaque color.
 */
QColor Tint(const QColor& color, double amount) {
    return QColor::fromRgbF(
        static_cast<float>(color.redF() + (1.0 - color.redF()) * amount),
        static_cast<float>(color.greenF() + (1.0 - color.greenF()) * amount),
        static_cast<float>(color.blueF() + (1.0 - color.blueF()) * amount));
}

/**
 * @brief Formats a physical row address as the protocol documents it.
 *
 * @param[in] row Physical row address.
 * @return Text such as "0x0E4".
 */
QString FormatRow(int row) {
    return QStringLiteral("0x") + QString::number(row, 16)
                                      .rightJustified(3, QLatin1Char('0'))
                                      .toUpper();
}

}  // namespace

/**
 * @brief Enables hover tracking; every region starts visible.
 */
FuseMapWidget::FuseMapWidget(QWidget* parent) : QWidget(parent) {
    visible_.fill(true);
    setMouseTracking(true);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumSize(240, 160);
}

/**
 * @brief Copies the fuses and rebuilds the layout for the document's device.
 */
void FuseMapWidget::SetDocument(const JedecDocument* document) {
    hover_column_ = -1;
    hover_bit_ = -1;
    if (document == nullptr || document->empty()) {
        device_ = Device::kUnknown;
        database_ = nullptr;
        fuses_.clear();
    } else {
        device_ = document->device();
        database_ = FuseDatabase::ForDevice(device_);
        fuses_ = document->fuses();
    }
    unused_term_.assign(fuses_.size(), false);
    if (database_ != nullptr) {
        for (const auto& term : database_->product_terms()) {
            bool all_connected = true;
            for (unsigned int i = term.first; i < term.end && all_connected;
                 ++i) {
                all_connected = !fuses_[i];
            }
            if (all_connected) {
                std::fill(unused_term_.begin() + term.first,
                          unused_term_.begin() + term.end, true);
            }
        }
    }
    BuildLayout();
    RenderImage();
    if (fit_) {
        UpdateFitZoom();
    }
    updateGeometry();
    update();
}

/**
 * @brief Re-renders the cache after a visibility change.
 */
void FuseMapWidget::SetRegionVisible(Region region, bool visible) {
    visible_[static_cast<int>(region)] = visible;
    RenderImage();
    update();
}

/**
 * @brief Re-renders the cache after a color-mode change.
 */
void FuseMapWidget::SetRegionColors(bool enabled) {
    region_colors_ = enabled;
    RenderImage();
    update();
}

/**
 * @brief Re-renders the cache after toggling muted product terms.
 */
void FuseMapWidget::SetMuteUnusedTerms(bool enabled) {
    mute_unused_terms_ = enabled;
    RenderImage();
    update();
}

/**
 * @brief Applies a clamped fixed cell size.
 */
void FuseMapWidget::SetZoom(int cell_size) {
    fit_ = false;
    scale_ = std::clamp(cell_size, kMinZoom, kMaxZoom);
    QSize hint = sizeHint();
    setMinimumSize(hint);
    updateGeometry();
    update();
    emit ZoomChanged(scale_, fit_);
}

/**
 * @brief Drops the fixed minimum size so the scroll area can shrink us.
 */
void FuseMapWidget::SetFitToWindow() {
    fit_ = true;
    setMinimumSize(240, 160);
    UpdateFitZoom();
    updateGeometry();
    update();
    emit ZoomChanged(scale_, fit_);
}

/**
 * @brief Content size at the current cell size.
 */
QSize FuseMapWidget::sizeHint() const {
    int columns = std::max<int>(1, static_cast<int>(column_rows_.size()));
    int rows = std::max(1, grid_rows_);
    return QSize(static_cast<int>(columns * scale_) + 2 * kMargin,
                 static_cast<int>(rows * scale_) + kHeaderHeight + 2 * kMargin);
}

/**
 * @brief Lays out bank 0, bank 1 and the three special rows left to right.
 */
void FuseMapWidget::BuildLayout() {
    column_rows_.clear();
    cells_.clear();
    sections_.clear();
    grid_rows_ = 0;
    if (device_ == Device::kUnknown) {
        return;
    }
    auto add_section = [this](int first_row, int end_row,
                              const QString& caption, int width) {
        if (!column_rows_.empty()) {
            column_rows_.push_back(-1);
        }
        Section section;
        section.first_column = static_cast<int>(column_rows_.size());
        for (int row = first_row; row < end_row; ++row) {
            if (WordBits(device_, static_cast<unsigned int>(row)) > 0) {
                column_rows_.insert(column_rows_.end(), width, row);
            }
        }
        section.columns =
            static_cast<int>(column_rows_.size()) - section.first_column;
        section.caption = caption;
        sections_.push_back(section);
    };
    add_section(0x000, 0x080, QStringLiteral("Logic rows 0x000–0x06B"), 1);
    add_section(0x080, 0x100,
                device_ == Device::kAtf1502as
                    ? QStringLiteral("Logic rows 0x080–0x0E4")
                    : QStringLiteral("Logic rows 0x080–0x0E8"),
                1);
    add_section(0x100, 0x101, QStringLiteral("Config"), kSpecialRowWidth);
    add_section(0x200, 0x201, QStringLiteral("JTAG"), kSpecialRowWidth);
    add_section(0x300, 0x301, QStringLiteral("UES"), kSpecialRowWidth);
    grid_rows_ = static_cast<int>(WordBits(device_, 0));

    int columns = static_cast<int>(column_rows_.size());
    cells_.assign(static_cast<size_t>(columns * grid_rows_), kAbsent);
    // Physical cell (row, bit) to JEDEC fuse, or kPadding when unmapped.
    std::vector<int> cell_fuse(0x301 * grid_rows_, kPadding);
    for (unsigned int fuse = 0; fuse < FuseCount(device_); ++fuse) {
        unsigned int row;
        unsigned int bit;
        if (FuseLocation(device_, fuse, &row, &bit)) {
            cell_fuse[row * grid_rows_ + bit] = static_cast<int>(fuse);
        }
    }
    for (int column = 0; column < columns; ++column) {
        int row = column_rows_[column];
        if (row < 0) {
            continue;
        }
        int bits = static_cast<int>(WordBits(device_, row));
        for (int bit = 0; bit < bits; ++bit) {
            cells_[bit * columns + column] = cell_fuse[row * grid_rows_ + bit];
        }
    }
}

/**
 * @brief Colors each cell by region and value into a 1:1 image.
 */
void FuseMapWidget::RenderImage() {
    int columns = static_cast<int>(column_rows_.size());
    if (columns == 0 || database_ == nullptr) {
        image_ = QImage();
        return;
    }
    image_ = QImage(columns, grid_rows_, QImage::Format_RGB32);
    const QColor background = palette().color(QPalette::Base);
    const QColor hidden_programmed(0xc9, 0xcd, 0xd3);
    const QColor hidden_erased(0xef, 0xf1, 0xf3);
    const QColor mono_programmed(0x1f, 0x29, 0x37);
    const QColor mono_erased(0xf3, 0xf4, 0xf6);
    const QColor mono_muted(0x9c, 0xa3, 0xaf);
    const QColor padding = Tint(RegionColor(Region::kPadding), 0.45);
    for (int bit = 0; bit < grid_rows_; ++bit) {
        QRgb* line = reinterpret_cast<QRgb*>(image_.scanLine(bit));
        for (int column = 0; column < columns; ++column) {
            int cell = cells_[bit * columns + column];
            QColor color;
            if (cell == kAbsent) {
                color = background;
            } else if (cell == kPadding) {
                color = visible_[static_cast<int>(Region::kPadding)]
                            ? padding
                            : hidden_erased;
            } else {
                Region region = database_->RegionOf(cell);
                bool programmed = !fuses_[cell];
                if (!visible_[static_cast<int>(region)]) {
                    color = programmed ? hidden_programmed : hidden_erased;
                } else if (!region_colors_) {
                    color = !programmed ? mono_erased
                            : mute_unused_terms_ && unused_term_[cell]
                                ? mono_muted
                                : mono_programmed;
                } else {
                    QColor base = RegionColor(region);
                    if (!programmed) {
                        color = Tint(base, 0.84);
                    } else if (mute_unused_terms_ && unused_term_[cell]) {
                        color = Tint(base, 0.55);
                    } else {
                        color = base;
                    }
                }
            }
            line[column] = color.rgb();
        }
    }
}

/**
 * @brief Largest fractional cell size that fits the widget.
 */
void FuseMapWidget::UpdateFitZoom() {
    int columns = static_cast<int>(column_rows_.size());
    if (columns == 0 || grid_rows_ == 0) {
        return;
    }
    double by_width = static_cast<double>(width() - 2 * kMargin) / columns;
    double by_height =
        static_cast<double>(height() - 2 * kMargin - kHeaderHeight) /
        grid_rows_;
    double scale =
        std::clamp(std::min(by_width, by_height), static_cast<double>(kMinZoom),
                   static_cast<double>(kMaxZoom));
    if (scale != scale_) {
        scale_ = scale;
        emit ZoomChanged(scale_, fit_);
    }
}

/**
 * @brief Centers the grid when there is spare room.
 */
QRectF FuseMapWidget::GridRect() const {
    double grid_width = static_cast<double>(column_rows_.size()) * scale_;
    double grid_height = grid_rows_ * scale_;
    double x = std::max<double>(kMargin, (width() - grid_width) / 2);
    double y =
        std::max<double>(kMargin, (height() - grid_height - kHeaderHeight) / 2);
    return QRectF(x, y + kHeaderHeight, grid_width, grid_height);
}

/**
 * @brief Maps widget coordinates onto present cells only.
 */
bool FuseMapWidget::CellAt(const QPoint& position, int* column,
                           int* bit) const {
    if (image_.isNull()) {
        return false;
    }
    QRectF grid = GridRect();
    if (!grid.contains(position)) {
        return false;
    }
    int columns = static_cast<int>(column_rows_.size());
    int c = std::min(columns - 1,
                     static_cast<int>((position.x() - grid.left()) / scale_));
    int b = std::min(grid_rows_ - 1,
                     static_cast<int>((position.y() - grid.top()) / scale_));
    if (cells_[b * columns + c] == kAbsent) {
        return false;
    }
    *column = c;
    *bit = b;
    return true;
}

/**
 * @brief Combines physical coordinates with the database description.
 */
QString FuseMapWidget::DescribeCell(int column, int bit) const {
    int columns = static_cast<int>(column_rows_.size());
    int cell = cells_[bit * columns + column];
    QString where = QStringLiteral("Row %1, bit %2")
                        .arg(FormatRow(column_rows_[column]))
                        .arg(bit);
    if (cell == kPadding) {
        return where + QStringLiteral(" · unmapped cell, always erased");
    }
    QString value = fuses_[cell] ? QStringLiteral("1 (erased)")
                                 : QStringLiteral("0 (programmed)");
    if (unused_term_[cell]) {
        value += QStringLiteral(", unused term");
    }
    return QStringLiteral("%1 · fuse L%2 = %3 · %4 · %5")
        .arg(where)
        .arg(cell)
        .arg(value, RegionName(database_->RegionOf(cell)),
             database_->Describe(cell));
}

/**
 * @brief Paints the empty state or the scaled map with overlays.
 */
void FuseMapWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (image_.isNull()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Open a JEDEC file to see its fuse map.\n"
                            "You can also drop a .jed file here."));
        return;
    }
    QRectF grid = GridRect();
    int columns = static_cast<int>(column_rows_.size());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(grid, image_);

    // Section captions sit above their columns when they fit.
    QFont caption_font = font();
    caption_font.setPointSizeF(caption_font.pointSizeF() * 0.85);
    painter.setFont(caption_font);
    QFontMetrics metrics(caption_font);
    for (const Section& section : sections_) {
        QRectF band(grid.left() + section.first_column * scale_,
                    grid.top() - kHeaderHeight, section.columns * scale_,
                    kHeaderHeight - 4);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(band.bottomLeft(), band.bottomRight());
        if (metrics.horizontalAdvance(section.caption) + 4 <= band.width()) {
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(band, Qt::AlignLeft | Qt::AlignVCenter,
                             section.caption);
        }
    }

    if (scale_ >= 6) {
        painter.setPen(QColor(255, 255, 255, 110));
        for (int column = 0; column <= columns; ++column) {
            double x = grid.left() + column * scale_;
            painter.drawLine(QPointF(x, grid.top()), QPointF(x, grid.bottom()));
        }
        for (int bit = 0; bit <= grid_rows_; ++bit) {
            double y = grid.top() + bit * scale_;
            painter.drawLine(QPointF(grid.left(), y), QPointF(grid.right(), y));
        }
    }

    if (hover_column_ >= 0) {
        QRectF cell(grid.left() + hover_column_ * scale_,
                    grid.top() + hover_bit_ * scale_, scale_, scale_);
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.drawLine(QPointF(grid.left(), cell.center().y()),
                         QPointF(grid.right(), cell.center().y()));
        painter.drawLine(QPointF(cell.center().x(), grid.top()),
                         QPointF(cell.center().x(), grid.bottom()));
        painter.setPen(QPen(Qt::black, 2));
        painter.drawRect(cell.adjusted(-1, -1, 1, 1));
    }
}

/**
 * @brief Keeps the fitted cell size current.
 */
void FuseMapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_) {
        UpdateFitZoom();
    }
}

/**
 * @brief Emits a description whenever the hovered cell changes.
 */
void FuseMapWidget::mouseMoveEvent(QMouseEvent* event) {
    int column = -1;
    int bit = -1;
    if (!CellAt(event->position().toPoint(), &column, &bit)) {
        column = -1;
        bit = -1;
    }
    if (column == hover_column_ && bit == hover_bit_) {
        return;
    }
    hover_column_ = column;
    hover_bit_ = bit;
    emit CellHovered(column >= 0 ? DescribeCell(column, bit) : QString());
    update();
}

/**
 * @brief Removes the hover marker when the pointer leaves.
 */
void FuseMapWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hover_column_ >= 0) {
        hover_column_ = -1;
        hover_bit_ = -1;
        emit CellHovered(QString());
        update();
    }
}

/**
 * @brief Steps the cell size by one pixel per wheel notch with Ctrl held.
 */
void FuseMapWidget::wheelEvent(QWheelEvent* event) {
    if (!(event->modifiers() & Qt::ControlModifier) || image_.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }
    int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        SetZoom(zoom() + steps);
    }
    event->accept();
}

}  // namespace gui
}  // namespace atf
