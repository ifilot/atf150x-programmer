// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief JEDEC loading and fuse statistics for the GUI.
 */
#include "gui/src/jedec_document.h"

#include <QFile>
#include <set>
#include <string>

namespace atf {
namespace gui {

/**
 * @brief Reads the file in binary mode so the transmission checksum holds.
 */
bool JedecDocument::Load(const QString& path, JedecDocument* document,
                         QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error =
            QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    QByteArray bytes = file.readAll();
    return Parse(bytes.toStdString(), path, document, error);
}

/**
 * @brief Validates with the shared parser, then derives display statistics.
 */
bool JedecDocument::Parse(const std::string& text, const QString& path,
                          JedecDocument* document, QString* error) {
    JedecFile jedec;
    Status status = ParseJedec(text, &jedec);
    if (!status.ok()) {
        *error = QString::fromStdString(status.message());
        return false;
    }
    const FuseDatabase* database = FuseDatabase::ForDevice(jedec.device);
    if (database == nullptr || database->fuse_count() != jedec.fuses.size()) {
        *error = QStringLiteral("No fuse map is available for %1")
                     .arg(QString::fromLatin1(DeviceName(jedec.device)));
        return false;
    }

    JedecDocument result;
    result.path_ = path;
    result.device_ = jedec.device;
    result.image_ = PackFuses(jedec.device, jedec.fuses);

    size_t stx = text.find('\x02');
    size_t star = text.find('*', stx == std::string::npos ? 0 : stx);
    if (stx != std::string::npos && star != std::string::npos) {
        result.header_ =
            QString::fromLatin1(text.substr(stx + 1, star - stx - 1).c_str())
                .trimmed();
        result.header_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    }

    unsigned int sum = 0;
    for (size_t i = 0; i < jedec.fuses.size(); ++i) {
        unsigned int value = jedec.fuses[i];
        sum += value << (i % 8);
        RegionUsage& usage =
            result.usage_[static_cast<int>(database->RegionOf(i))];
        ++usage.total;
        if (!value) {
            ++usage.programmed;
            ++result.programmed_;
        }
    }
    result.checksum_ = static_cast<uint16_t>(sum & 0xffff);

    // A product term with every input connected is the conventional disabled
    // (constant false) term; one with no input connected is constant true.
    std::set<QString> used_macrocells;
    for (const auto& term : database->product_terms()) {
        unsigned int connected = 0;
        for (unsigned int i = term.first; i < term.end; ++i) {
            connected += jedec.fuses[i] ? 0 : 1;
        }
        if (connected > 0 && connected < term.end - term.first) {
            ++result.used_product_terms_;
            used_macrocells.insert(term.macrocell);
        }
    }
    result.total_product_terms_ =
        static_cast<unsigned int>(database->product_terms().size());
    result.used_macrocells_ = static_cast<unsigned int>(used_macrocells.size());
    result.total_macrocells_ =
        static_cast<unsigned int>(database->macrocells().size());

    unsigned int ues_first = JtagFuseStart(jedec.device) + 4;
    for (unsigned int i = 0; i < 16; ++i) {
        result.user_signature_ = static_cast<uint16_t>(
            (result.user_signature_ << 1) | jedec.fuses[ues_first + i]);
    }
    result.fuses_ = std::move(jedec.fuses);
    *document = std::move(result);
    return true;
}

}  // namespace gui
}  // namespace atf
