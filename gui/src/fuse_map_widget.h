// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Grid rendering of every physical Flash cell of a JEDEC image.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_FUSE_MAP_WIDGET_H_
#define ATF150X_PROGRAMMER_GUI_SRC_FUSE_MAP_WIDGET_H_

#include <QImage>
#include <QRectF>
#include <QString>
#include <QWidget>
#include <array>
#include <vector>

#include "cli/src/jedec.h"
#include "gui/src/fuse_database.h"

namespace atf {
namespace gui {

class JedecDocument;

/**
 * @brief Draws the physical row map of a JEDEC image.
 *
 * Each column is one physical Flash word (row address) in programming order,
 * and each cell one bit of that word; the short configuration, JTAG and UES
 * words are drawn several columns wide so they remain visible. Programmed
 * (zero) fuses use their region color; erased fuses use a light tint.
 * Product terms with every input connected, which compilers use to disable
 * unused terms, can be muted so the terms in use stand out. Sections are
 * separated by a blank column. In fit mode the cell size follows the widget
 * size; otherwise the widget requests the space needed by the fixed cell
 * size.
 */
class FuseMapWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates an empty map.
     *
     * @param[in] parent Optional Qt parent that takes ownership.
     */
    explicit FuseMapWidget(QWidget* parent = nullptr);

    /**
     * @brief Shows a document, or the empty-state hint for nullptr.
     *
     * The fuses are copied; the document need not outlive the call.
     *
     * @param[in] document Parsed document, or nullptr to clear.
     */
    void SetDocument(const JedecDocument* document);

    /**
     * @brief Dims or restores one region.
     *
     * @param[in] region Region to change.
     * @param[in] visible False to draw the region in neutral gray.
     */
    void SetRegionVisible(Region region, bool visible);

    /**
     * @brief Chooses region colors or a monochrome rendering.
     *
     * @param[in] enabled True for region colors.
     */
    void SetRegionColors(bool enabled);

    /**
     * @brief Mutes disabled product terms.
     *
     * @param[in] enabled True to draw fully connected product terms in a
     * muted tone.
     */
    void SetMuteUnusedTerms(bool enabled);

    /**
     * @brief Selects a fixed cell size and leaves fit mode.
     *
     * @param[in] cell_size Pixels per cell, clamped to the supported range.
     */
    void SetZoom(int cell_size);

    /**
     * @brief Makes the cell size follow the widget size.
     */
    void SetFitToWindow();

    /** @brief Current pixels per cell, rounded for fit mode. */
    int zoom() const {
        return static_cast<int>(scale_ + 0.5);
    }

    /** @brief True while the cell size follows the widget size. */
    bool fit_to_window() const {
        return fit_;
    }

    /**
     * @brief Returns the preferred size at the current cell size.
     *
     * @return Size including the section header and margins.
     */
    QSize sizeHint() const override;

    /** Smallest supported cell size in pixels. */
    static constexpr int kMinZoom = 1;
    /** Largest supported cell size in pixels. */
    static constexpr int kMaxZoom = 24;

signals:
    /**
     * @brief Reports the cell under the mouse pointer.
     *
     * @param[in] description Readable cell description; empty when the
     * pointer leaves the map.
     */
    void CellHovered(const QString& description);

    /**
     * @brief Reports cell-size changes, including those from Ctrl+wheel.
     *
     * @param[in] cell_size Pixels per cell; fractional in fit mode.
     * @param[in] fit True while in fit mode.
     */
    void ZoomChanged(double cell_size, bool fit);

protected:
    /** @brief Draws the header, the cached image, grid and hover marker. */
    void paintEvent(QPaintEvent* event) override;
    /** @brief Recomputes the fitted cell size. */
    void resizeEvent(QResizeEvent* event) override;
    /** @brief Tracks the hovered cell. */
    void mouseMoveEvent(QMouseEvent* event) override;
    /** @brief Clears the hover marker. */
    void leaveEvent(QEvent* event) override;
    /** @brief Zooms with Ctrl+wheel; plain wheel events scroll. */
    void wheelEvent(QWheelEvent* event) override;

private:
    /** @brief A labeled run of display columns. */
    struct Section {
        int first_column = 0;
        int columns = 0;
        QString caption;
    };

    /** @brief Rebuilds column layout and cell-to-fuse lookup for a device. */
    void BuildLayout();
    /** @brief Rebuilds the one-pixel-per-cell image cache. */
    void RenderImage();
    /** @brief Fits the cell size to the current widget size. */
    void UpdateFitZoom();
    /** @brief Cell grid rectangle in widget coordinates. */
    QRectF GridRect() const;
    /**
     * @brief Converts a widget position into a cell.
     *
     * @param[in] position Widget coordinates.
     * @param[out] column Non-null display column; unchanged when outside.
     * @param[out] bit Non-null bit index; unchanged when outside.
     * @return True for a position over a present cell.
     */
    bool CellAt(const QPoint& position, int* column, int* bit) const;
    /**
     * @brief Describes one cell for CellHovered().
     *
     * @param[in] column Display column of a present cell.
     * @param[in] bit Bit index of a present cell.
     * @return Row, bit, fuse and function description.
     */
    QString DescribeCell(int column, int bit) const;

    // Cell lookup values besides fuse indices.
    static constexpr int kAbsent = -2;
    static constexpr int kPadding = -1;

    Device device_ = Device::kUnknown;
    const FuseDatabase* database_ = nullptr;
    Fuses fuses_;
    std::vector<bool> unused_term_;
    std::vector<int> column_rows_;
    std::vector<int> cells_;
    std::vector<Section> sections_;
    int grid_rows_ = 0;
    QImage image_;
    std::array<bool, kRegionCount> visible_;
    bool region_colors_ = true;
    bool mute_unused_terms_ = true;
    bool fit_ = true;
    double scale_ = 4.0;
    int hover_column_ = -1;
    int hover_bit_ = -1;
};

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_FUSE_MAP_WIDGET_H_
