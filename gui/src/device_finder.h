// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief USB identification of Arduino Leonardo programmer ports.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_DEVICE_FINDER_H_
#define ATF150X_PROGRAMMER_GUI_SRC_DEVICE_FINDER_H_

#include <QList>
#include <QString>
#include <cstdint>

namespace atf {
namespace gui {

/** @brief One serial port that belongs to an Arduino Leonardo. */
struct LeonardoPort {
    /** Port name to open, such as "COM7" or "ttyACM0". */
    QString name;
    /** Operating-system description for display. */
    QString description;
    /** USB serial number, possibly empty. */
    QString serial_number;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    /** True for the Caterina USB bootloader rather than the sketch. */
    bool bootloader = false;

    /**
     * @brief Compares the identifying fields.
     *
     * @param[in] other Port to compare with.
     * @return True when name, IDs and mode match.
     */
    bool operator==(const LeonardoPort& other) const {
        return name == other.name && vendor_id == other.vendor_id &&
               product_id == other.product_id && bootloader == other.bootloader;
    }
};

/**
 * @brief Classifies a USB vendor/product pair.
 *
 * Accepts the Arduino LLC (0x2341) and Arduino SRL (0x2A03) vendor IDs with
 * the Leonardo sketch (0x8036) or Caterina bootloader (0x0036) product IDs.
 *
 * @param[in] vendor_id USB vendor ID.
 * @param[in] product_id USB product ID.
 * @param[out] bootloader Optional; set to true for a bootloader product ID.
 * @return True for a Leonardo sketch or bootloader.
 */
bool IsLeonardo(uint16_t vendor_id, uint16_t product_id,
                bool* bootloader = nullptr);

/**
 * @brief Lists Leonardo serial ports currently present, sorted by name.
 *
 * Only reads the operating system's device list; no port is opened.
 *
 * @return Matching sketch and bootloader ports.
 */
QList<LeonardoPort> FindLeonardoPorts();

/**
 * @brief Formats a port for the port selector.
 *
 * @param[in] port Port to describe.
 * @return Text such as "COM7 – Arduino Leonardo (2341:8036)".
 */
QString PortLabel(const LeonardoPort& port);

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_DEVICE_FINDER_H_
