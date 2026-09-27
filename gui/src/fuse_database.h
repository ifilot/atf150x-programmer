// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Functional regions and signal names of ATF150x JEDEC fuses.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_FUSE_DATABASE_H_
#define ATF150X_PROGRAMMER_GUI_SRC_FUSE_DATABASE_H_

#include <QColor>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <map>
#include <vector>

#include "firmware/atf1502_programmer/protocol.h"

namespace atf {
namespace gui {

/**
 * @brief Coloring categories shown in the fuse map legend.
 *
 * Product terms are split per logic block so the block structure is visible.
 * kPadding marks physical cells that no JEDEC fuse maps to.
 */
enum class Region : uint8_t {
    kProductTermsA,
    kProductTermsB,
    kProductTermsC,
    kProductTermsD,
    kMacrocells,
    kInterconnect,
    kGlobalOe,
    kConfiguration,
    kUserSignature,
    kReserved,
    kPadding,
};

/** Number of Region enumerators. */
constexpr int kRegionCount = static_cast<int>(Region::kPadding) + 1;

/**
 * @brief Returns the legend caption of a region.
 *
 * @param[in] region Region to describe.
 * @return Short capitalized caption.
 */
QString RegionName(Region region);

/**
 * @brief Returns the color used for programmed (zero) fuses of a region.
 *
 * @param[in] region Region to color.
 * @return Opaque color; erased fuses use a light tint of it.
 */
QColor RegionColor(Region region);

/**
 * @brief Read-only fuse description of one supported device.
 *
 * Instances are loaded from the generated Qt resources under :/fusemap and
 * shared for the lifetime of the process. Not modified after loading, so
 * concurrent reads are safe.
 */
class FuseDatabase {
public:
    /** @brief One product term: a contiguous run of JEDEC fuses. */
    struct ProductTerm {
        unsigned int first = 0;
        unsigned int end = 0;
        int block = 0;
        QString macrocell;
        QString name;
    };

    /**
     * @brief Returns the shared database of a device, loading it on demand.
     *
     * Call from the GUI thread before sharing the pointer with other threads.
     *
     * @param[in] device Supported device selector.
     * @return Database pointer, or nullptr for unknown devices or a damaged
     * resource.
     */
    static const FuseDatabase* ForDevice(Device device);

    /**
     * @brief Returns the device's JEDEC fuse count.
     *
     * @return QF value, including reserved fuses.
     */
    unsigned int fuse_count() const {
        return static_cast<unsigned int>(regions_.size());
    }

    /**
     * @brief Returns the coloring region of a fuse.
     *
     * @param[in] fuse JEDEC fuse index below fuse_count().
     * @return Region of the fuse.
     */
    Region RegionOf(unsigned int fuse) const {
        return regions_[fuse];
    }

    /**
     * @brief Describes the function of a fuse for tooltips.
     *
     * @param[in] fuse JEDEC fuse index below fuse_count().
     * @return Text such as "MC3 (pin 6) PT2 · input UIM14 complement".
     */
    QString Describe(unsigned int fuse) const;

    /**
     * @brief Lists all product terms ordered by first fuse.
     *
     * @return Product terms covering the whole product-term region.
     */
    const std::vector<ProductTerm>& product_terms() const {
        return product_terms_;
    }

    /**
     * @brief Returns the macrocell names in database order.
     *
     * @return Names such as "MC1".
     */
    const QStringList& macrocells() const {
        return macrocells_;
    }

    /**
     * @brief Returns the upstream database revision for attribution.
     *
     * @return Text such as "whitequark/prjbureau@8b8a97122ec2".
     */
    const QString& source() const {
        return source_;
    }

private:
    FuseDatabase() = default;

    /**
     * @brief Parses one generated JSON resource.
     *
     * @param[in] path Qt resource path.
     * @return True when the document is complete and consistent.
     */
    bool Load(const QString& path);

    /**
     * @brief Finds the product term that contains a fuse.
     *
     * @param[in] fuse JEDEC fuse index.
     * @return Product term pointer, or nullptr outside the product-term
     * region.
     */
    const ProductTerm* FindProductTerm(unsigned int fuse) const;

    std::vector<Region> regions_;
    std::vector<ProductTerm> product_terms_;
    std::vector<QStringList> block_inputs_;
    std::map<unsigned int, QString> labels_;
    std::map<QString, QString> macrocell_pins_;
    QStringList macrocells_;
    QString source_;
};

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_FUSE_DATABASE_H_
