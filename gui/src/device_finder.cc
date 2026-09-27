// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Leonardo port discovery through Qt Serial Port.
 */
#include "gui/src/device_finder.h"

#include <QSerialPortInfo>
#include <algorithm>

namespace atf {
namespace gui {

/**
 * @brief Matches the official and Arduino SRL Leonardo USB identities.
 */
bool IsLeonardo(uint16_t vendor_id, uint16_t product_id, bool* bootloader) {
    if (vendor_id != 0x2341 && vendor_id != 0x2a03) {
        return false;
    }
    if (product_id != 0x8036 && product_id != 0x0036) {
        return false;
    }
    if (bootloader != nullptr) {
        *bootloader = product_id == 0x0036;
    }
    return true;
}

/**
 * @brief Filters the system port list by USB identity.
 */
QList<LeonardoPort> FindLeonardoPorts() {
    QList<LeonardoPort> ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        if (!info.hasVendorIdentifier() || !info.hasProductIdentifier()) {
            continue;
        }
        LeonardoPort port;
        port.vendor_id = info.vendorIdentifier();
        port.product_id = info.productIdentifier();
        if (!IsLeonardo(port.vendor_id, port.product_id, &port.bootloader)) {
            continue;
        }
        port.name = info.portName();
        port.description = info.description();
        port.serial_number = info.serialNumber();
        ports.append(port);
    }
    std::sort(ports.begin(), ports.end(),
              [](const LeonardoPort& a, const LeonardoPort& b) {
                  // Natural order keeps COM10 after COM9.
                  if (a.name.size() != b.name.size()) {
                      return a.name.size() < b.name.size();
                  }
                  return a.name < b.name;
              });
    return ports;
}

/**
 * @brief Shows the port name, mode and USB identity.
 */
QString PortLabel(const LeonardoPort& port) {
    QString mode = port.bootloader ? QStringLiteral("Leonardo bootloader")
                                   : QStringLiteral("Arduino Leonardo");
    return QStringLiteral("%1 – %2 (%3:%4)")
        .arg(port.name, mode)
        .arg(port.vendor_id, 4, 16, QLatin1Char('0'))
        .arg(port.product_id, 4, 16, QLatin1Char('0'));
}

}  // namespace gui
}  // namespace atf
