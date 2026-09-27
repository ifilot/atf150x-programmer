// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief A validated JEDEC file together with the statistics the GUI shows.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_JEDEC_DOCUMENT_H_
#define ATF150X_PROGRAMMER_GUI_SRC_JEDEC_DOCUMENT_H_

#include <QString>
#include <array>
#include <cstdint>

#include "cli/src/jedec.h"
#include "gui/src/fuse_database.h"

namespace atf {
namespace gui {

/** @brief Programmed-fuse count of one legend region. */
struct RegionUsage {
    unsigned int total = 0;
    unsigned int programmed = 0;
};

/**
 * @brief Immutable view of one parsed JEDEC file.
 *
 * Construct through Load(); a default-constructed document is empty.
 */
class JedecDocument {
public:
    /**
     * @brief Reads, validates and summarizes a JEDEC file.
     *
     * Uses the same parser and safeguards as the CLI. On failure the output
     * is unchanged.
     *
     * @param[in] path File to read.
     * @param[out] document Non-null destination.
     * @param[out] error Non-null destination for a diagnostic on failure.
     * @return True on success.
     */
    static bool Load(const QString& path, JedecDocument* document,
                     QString* error);

    /**
     * @brief Parses and summarizes JEDEC text that is already in memory.
     *
     * @param[in] text Complete file contents, preserving line endings.
     * @param[in] path Path shown to the user; not opened.
     * @param[out] document Non-null destination; unchanged on failure.
     * @param[out] error Non-null destination for a diagnostic on failure.
     * @return True on success.
     */
    static bool Parse(const std::string& text, const QString& path,
                      JedecDocument* document, QString* error);

    /** @brief Tests whether the document holds a parsed file. */
    bool empty() const {
        return device_ == Device::kUnknown;
    }

    /** @brief Source path as given to Load(). */
    const QString& path() const {
        return path_;
    }

    /** @brief Device inferred from the fuse count. */
    Device device() const {
        return device_;
    }

    /** @brief Complete binary fuse vector in JEDEC order. */
    const Fuses& fuses() const {
        return fuses_;
    }

    /** @brief Physical words ready for programming or verification. */
    const Image& image() const {
        return image_;
    }

    /** @brief Free-text design header between STX and the first star. */
    const QString& header() const {
        return header_;
    }

    /** @brief JEDEC fuse checksum, as stored in the C record. */
    uint16_t checksum() const {
        return checksum_;
    }

    /** @brief Number of fuses with value zero. */
    unsigned int programmed() const {
        return programmed_;
    }

    /** @brief Per-region fuse usage, indexed by Region. */
    const std::array<RegionUsage, kRegionCount>& usage() const {
        return usage_;
    }

    /** @brief Product terms that connect some, but not all, inputs. */
    unsigned int used_product_terms() const {
        return used_product_terms_;
    }

    /** @brief Total product terms of the device. */
    unsigned int total_product_terms() const {
        return total_product_terms_;
    }

    /** @brief Macrocells with at least one used product term. */
    unsigned int used_macrocells() const {
        return used_macrocells_;
    }

    /** @brief Total macrocells of the device. */
    unsigned int total_macrocells() const {
        return total_macrocells_;
    }

    /** @brief True when the image enables outputs after programming. */
    bool armed() const {
        return !fuses_[ArmingFuse(device_)];
    }

    /** @brief The 16 user-signature fuses, first fuse most significant. */
    uint16_t user_signature() const {
        return user_signature_;
    }

private:
    QString path_;
    Device device_ = Device::kUnknown;
    Fuses fuses_;
    Image image_;
    QString header_;
    uint16_t checksum_ = 0;
    unsigned int programmed_ = 0;
    std::array<RegionUsage, kRegionCount> usage_{};
    unsigned int used_product_terms_ = 0;
    unsigned int total_product_terms_ = 0;
    unsigned int used_macrocells_ = 0;
    unsigned int total_macrocells_ = 0;
    uint16_t user_signature_ = 0;
};

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_JEDEC_DOCUMENT_H_
