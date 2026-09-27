// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Loads the generated Project Bureau fuse descriptions.
 */
#include "gui/src/fuse_database.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <memory>

namespace atf {
namespace gui {
namespace {

/**
 * @brief Converts a block letter to its zero-based index.
 *
 * @param[in] name Block name "A" through "D".
 * @return Block index, or -1 for other names.
 */
int BlockIndex(const QString& name) {
    if (name.size() != 1 || name[0] < QLatin1Char('A') ||
        name[0] > QLatin1Char('D')) {
        return -1;
    }
    return name[0].unicode() - 'A';
}

/**
 * @brief Maps an upstream range name to a coloring region.
 *
 * @param[in] name Range key from the generated document.
 * @param[out] region Non-null destination; unchanged for unknown names.
 * @return True for known ranges other than product terms.
 */
bool RangeRegion(const QString& name, Region* region) {
    static const std::map<QString, Region> kRegions = {
        {QStringLiteral("macrocells"), Region::kMacrocells},
        {QStringLiteral("uim_muxes"), Region::kInterconnect},
        {QStringLiteral("goe_muxes"), Region::kGlobalOe},
        {QStringLiteral("config"), Region::kConfiguration},
        {QStringLiteral("user"), Region::kUserSignature},
        {QStringLiteral("reserved"), Region::kReserved},
    };
    auto found = kRegions.find(name);
    if (found == kRegions.end()) {
        return false;
    }
    *region = found->second;
    return true;
}

/**
 * @brief Describes one product-term input for tooltips.
 *
 * @param[in] signal Upstream input name, such as "UIM14_N" or "MC3_FLB".
 * @return Readable input description.
 */
QString DescribeInput(const QString& signal) {
    if (signal.endsWith(QStringLiteral("_P"))) {
        return signal.chopped(2) + QStringLiteral(" true");
    }
    if (signal.endsWith(QStringLiteral("_N"))) {
        return signal.chopped(2) + QStringLiteral(" complement");
    }
    if (signal.endsWith(QStringLiteral("_FLB"))) {
        return signal.chopped(4) + QStringLiteral(" foldback");
    }
    return signal;
}

}  // namespace

/**
 * @brief Captions match the legend in the main window.
 */
QString RegionName(Region region) {
    switch (region) {
        case Region::kProductTermsA:
            return QStringLiteral("Product terms, block A");
        case Region::kProductTermsB:
            return QStringLiteral("Product terms, block B");
        case Region::kProductTermsC:
            return QStringLiteral("Product terms, block C");
        case Region::kProductTermsD:
            return QStringLiteral("Product terms, block D");
        case Region::kMacrocells:
            return QStringLiteral("Macrocell configuration");
        case Region::kInterconnect:
            return QStringLiteral("Interconnect (UIM)");
        case Region::kGlobalOe:
            return QStringLiteral("Global output enables");
        case Region::kConfiguration:
            return QStringLiteral("Device configuration");
        case Region::kUserSignature:
            return QStringLiteral("User signature (UES)");
        case Region::kReserved:
            return QStringLiteral("Reserved");
        case Region::kPadding:
            return QStringLiteral("Unmapped cell");
    }
    return QString();
}

/**
 * @brief Uses one hue family per function, with related product-term blues.
 */
QColor RegionColor(Region region) {
    switch (region) {
        case Region::kProductTermsA:
            return QColor(0x25, 0x63, 0xeb);
        case Region::kProductTermsB:
            return QColor(0x08, 0x91, 0xb2);
        case Region::kProductTermsC:
            return QColor(0x4f, 0x46, 0xe5);
        case Region::kProductTermsD:
            return QColor(0x02, 0x84, 0xc7);
        case Region::kMacrocells:
            return QColor(0xea, 0x58, 0x0c);
        case Region::kInterconnect:
            return QColor(0x16, 0xa3, 0x4a);
        case Region::kGlobalOe:
            return QColor(0x93, 0x33, 0xea);
        case Region::kConfiguration:
            return QColor(0xdc, 0x26, 0x26);
        case Region::kUserSignature:
            return QColor(0xca, 0x8a, 0x04);
        case Region::kReserved:
            return QColor(0x6b, 0x72, 0x80);
        case Region::kPadding:
            return QColor(0xb8, 0xbe, 0xc8);
    }
    return QColor(Qt::black);
}

/**
 * @brief Loads each device once and keeps it for the process lifetime.
 */
const FuseDatabase* FuseDatabase::ForDevice(Device device) {
    static std::map<Device, std::unique_ptr<FuseDatabase>> cache;
    auto found = cache.find(device);
    if (found != cache.end()) {
        return found->second.get();
    }
    std::unique_ptr<FuseDatabase> database(new FuseDatabase());
    QString path = QStringLiteral(":/fusemap/%1.json")
                       .arg(QString::fromLatin1(DeviceName(device)).toLower());
    if (device == Device::kUnknown || !database->Load(path)) {
        database.reset();
    }
    const FuseDatabase* result = database.get();
    cache[device] = std::move(database);
    return result;
}

/**
 * @brief Builds the per-fuse region table and lookup structures.
 */
bool FuseDatabase::Load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    int count = root.value(QStringLiteral("fuse_count")).toInt();
    if (count <= 0) {
        return false;
    }
    source_ = root.value(QStringLiteral("source")).toString();
    regions_.assign(static_cast<size_t>(count), Region::kReserved);

    QJsonObject blocks = root.value(QStringLiteral("blocks")).toObject();
    block_inputs_.assign(4, QStringList());
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        int block = BlockIndex(it.key());
        if (block < 0) {
            return false;
        }
        for (const QJsonValue& input :
             it.value().toObject().value(QStringLiteral("inputs")).toArray()) {
            block_inputs_[block].append(input.toString());
        }
    }

    QJsonObject macrocells =
        root.value(QStringLiteral("macrocells")).toObject();
    for (auto it = macrocells.begin(); it != macrocells.end(); ++it) {
        macrocells_.append(it.key());
        macrocell_pins_[it.key()] =
            it.value().toObject().value(QStringLiteral("pin")).toString();
    }
    // JSON object keys are unordered; restore MC1, MC2, ... numbering.
    std::sort(macrocells_.begin(), macrocells_.end(),
              [](const QString& a, const QString& b) {
                  return a.mid(2).toInt() < b.mid(2).toInt();
              });

    for (const QJsonValue& value :
         root.value(QStringLiteral("regions")).toArray()) {
        QJsonArray range = value.toArray();
        QString name = range.at(0).toString();
        int first = range.at(1).toInt();
        int end = range.at(2).toInt();
        if (first < 0 || end > count || first > end) {
            return false;
        }
        Region region;
        if (RangeRegion(name, &region)) {
            std::fill(regions_.begin() + first, regions_.begin() + end, region);
        }
    }

    for (const QJsonValue& value :
         root.value(QStringLiteral("pterms")).toArray()) {
        QJsonArray entry = value.toArray();
        ProductTerm term;
        term.first = static_cast<unsigned int>(entry.at(0).toInt());
        term.end = static_cast<unsigned int>(entry.at(1).toInt());
        term.block = BlockIndex(entry.at(2).toString());
        term.macrocell = entry.at(3).toString();
        term.name = entry.at(4).toString();
        if (term.block < 0 || term.first >= term.end ||
            term.end > static_cast<unsigned int>(count) ||
            static_cast<int>(term.end - term.first) !=
                block_inputs_[term.block].size()) {
            return false;
        }
        Region region = static_cast<Region>(
            static_cast<int>(Region::kProductTermsA) + term.block);
        std::fill(regions_.begin() + term.first, regions_.begin() + term.end,
                  region);
        product_terms_.push_back(term);
    }
    std::sort(product_terms_.begin(), product_terms_.end(),
              [](const ProductTerm& a, const ProductTerm& b) {
                  return a.first < b.first;
              });

    for (const QJsonValue& value :
         root.value(QStringLiteral("labels")).toArray()) {
        QJsonArray entry = value.toArray();
        labels_[static_cast<unsigned int>(entry.at(0).toInt())] =
            entry.at(1).toString();
    }
    return !product_terms_.empty();
}

/**
 * @brief Binary-searches the sorted product-term table.
 */
const FuseDatabase::ProductTerm* FuseDatabase::FindProductTerm(
    unsigned int fuse) const {
    auto it =
        std::upper_bound(product_terms_.begin(), product_terms_.end(), fuse,
                         [](unsigned int value, const ProductTerm& term) {
                             return value < term.first;
                         });
    if (it == product_terms_.begin()) {
        return nullptr;
    }
    --it;
    return fuse < it->end ? &*it : nullptr;
}

/**
 * @brief Combines product-term geometry or the generated option label.
 */
QString FuseDatabase::Describe(unsigned int fuse) const {
    if (fuse >= fuse_count()) {
        return QString();
    }
    if (const ProductTerm* term = FindProductTerm(fuse)) {
        QString pin = macrocell_pins_.count(term->macrocell)
                          ? macrocell_pins_.at(term->macrocell)
                          : QString();
        QString owner =
            pin.isEmpty()
                ? term->macrocell + QStringLiteral(" (buried)")
                : term->macrocell + QStringLiteral(" (pin %1)").arg(pin);
        const QStringList& inputs = block_inputs_[term->block];
        return QStringLiteral("%1 %2 · input %3")
            .arg(owner, term->name,
                 DescribeInput(
                     inputs.value(static_cast<int>(fuse - term->first))));
    }
    auto label = labels_.find(fuse);
    if (label != labels_.end()) {
        return label->second;
    }
    if (regions_[fuse] == Region::kReserved) {
        return QStringLiteral("Reserved, must be 0");
    }
    return QStringLiteral("Undocumented fuse");
}

}  // namespace gui
}  // namespace atf
