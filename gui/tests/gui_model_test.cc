// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Consistency checks of the GUI fuse database and JEDEC statistics.
 */
#include <QString>
#include <string>

#include "gui/src/fuse_database.h"
#include "gui/src/jedec_document.h"
#include "tests/test_support.h"

namespace {

/**
 * @brief Checks region coverage and descriptions of one device.
 *
 * @param[in] device Supported device selector.
 */
void CheckDatabase(atf::Device device) {
    using atf::gui::Region;
    const atf::gui::FuseDatabase* database =
        atf::gui::FuseDatabase::ForDevice(device);
    ATF_CHECK(database != nullptr);
    ATF_CHECK(database->fuse_count() == atf::FuseCount(device));
    ATF_CHECK(database->product_terms().size() ==
              5u * static_cast<unsigned int>(database->macrocells().size()));
    unsigned int blocks = device == atf::Device::kAtf1502as ? 2 : 4;
    ATF_CHECK(database->macrocells().size() == static_cast<int>(16 * blocks));

    unsigned int next = 0;
    for (const auto& term : database->product_terms()) {
        // Product terms tile the start of the fuse map without gaps.
        ATF_CHECK(term.first == next);
        ATF_CHECK(term.end - term.first == 96);
        next = term.end;
    }
    for (unsigned int fuse = 0; fuse < database->fuse_count(); ++fuse) {
        Region region = database->RegionOf(fuse);
        bool pterm = static_cast<unsigned int>(region) < blocks;
        ATF_CHECK(pterm == (fuse < next));
        ATF_CHECK(region != Region::kPadding);
        ATF_CHECK(!database->Describe(fuse).isEmpty());
    }
    ATF_CHECK(database->RegionOf(atf::ArmingFuse(device)) ==
              Region::kConfiguration);
    ATF_CHECK(database->Describe(atf::ArmingFuse(device)) ==
              QStringLiteral("arming_switch"));
    ATF_CHECK(database->RegionOf(atf::JtagFuseStart(device) + 4) ==
              Region::kUserSignature);
    ATF_CHECK(database->RegionOf(atf::ReservedFuseStart(device)) ==
              Region::kReserved);
    ATF_CHECK(database->Describe(0).contains(QStringLiteral("PT")));
}

/**
 * @brief Loads a checked-in example and validates its statistics.
 *
 * @param[in] path Example path relative to the repository root.
 * @param[in] device Expected device.
 * @param[in] checksum Fuse checksum from the file's C record.
 */
void CheckExample(const char* path, atf::Device device, uint16_t checksum) {
    atf::gui::JedecDocument document;
    QString error;
    ATF_CHECK(atf::gui::JedecDocument::Load(QString::fromLatin1(path),
                                            &document, &error));
    ATF_CHECK(error.isEmpty());
    ATF_CHECK(document.device() == device);
    ATF_CHECK(document.checksum() == checksum);
    ATF_CHECK(document.header().contains(
        QString::fromLatin1(atf::DeviceName(device))));
    unsigned int programmed = 0;
    unsigned int total = 0;
    for (const auto& usage : document.usage()) {
        programmed += usage.programmed;
        total += usage.total;
    }
    ATF_CHECK(programmed == document.programmed());
    ATF_CHECK(total == atf::FuseCount(device));
    ATF_CHECK(document.used_product_terms() > 0);
    ATF_CHECK(document.used_macrocells() > 0);
    ATF_CHECK(document.used_macrocells() <= document.total_macrocells());
    ATF_CHECK(document.image() == atf::PackFuses(device, document.fuses()));
}

}  // namespace

/**
 * @brief Runs the GUI model checks.
 *
 * @return Zero when all checks pass; any failed check aborts the process.
 */
int main() {
    CheckDatabase(atf::Device::kAtf1502as);
    CheckDatabase(atf::Device::kAtf1504as);
    ATF_CHECK(atf::gui::FuseDatabase::ForDevice(atf::Device::kUnknown) ==
              nullptr);
    CheckExample("examples/logic_test/atf1502as-plcc44.jed",
                 atf::Device::kAtf1502as, 0xa5ff);

    atf::gui::JedecDocument document;
    QString error;
    ATF_CHECK(!atf::gui::JedecDocument::Parse(
        "not a JEDEC file", QStringLiteral("x.jed"), &document, &error));
    ATF_CHECK(!error.isEmpty());
    ATF_CHECK(document.empty());
    return 0;
}
