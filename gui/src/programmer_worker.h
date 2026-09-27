// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Background execution of programmer handshakes, operations and
 * firmware installation.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_PROGRAMMER_WORKER_H_
#define ATF150X_PROGRAMMER_GUI_SRC_PROGRAMMER_WORKER_H_

#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <cstdint>

#include "cli/src/jedec.h"
#include "firmware/atf1502_programmer/protocol.h"
#include "gui/src/device_finder.h"

namespace atf {
namespace gui {

/** @brief What answered on a Leonardo sketch port. */
enum class FirmwareState : uint8_t {
    /** Protocol and release version match this application. */
    kCompatible,
    /** ATF15XX firmware of another protocol or release version. */
    kDifferentVersion,
    /** No valid protocol reply: another sketch or no sketch. */
    kNoResponse,
    /** The port could not be opened, for example because it is in use. */
    kPortError,
};

/** @brief Outcome of probing one port. */
struct ProbeResult {
    QString port;
    FirmwareState state = FirmwareState::kNoResponse;
    /** HELLO payload, such as "ATF15XX 2 v0.3.0"; empty without a reply. */
    QString hello;
    /** IDCODE text of the attached CPLD; empty when not read. */
    QString idcode;
    /** Diagnostic for failed handshakes or ID reads. */
    QString error;
};

/** @brief Programmer tasks started from the GUI. */
enum class Task : uint8_t {
    kIdentify,
    kErase,
    kFlash,
    kVerify,
};

/**
 * @brief Runs blocking serial and process work on a dedicated thread.
 *
 * Lives in a worker QThread; call its methods only through queued
 * invocations. One task runs at a time, and every task ends with exactly one
 * finished signal. Each task opens and closes its own connection.
 */
class ProgrammerWorker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /**
     * @brief Performs HELLO and, for compatible firmware, reads the IDCODE.
     *
     * Never erases or programs. Emits ProbeFinished().
     *
     * @param[in] port Sketch port name.
     */
    void Probe(const QString& port);

    /**
     * @brief Runs one task inside a validated connection.
     *
     * Reads the IDCODE first and refuses a device that differs from
     * expected. Emits DeviceIdentified() when an IDCODE was read, Progress()
     * while working, then OperationFinished().
     *
     * @param[in] task Task to run.
     * @param[in] port Sketch port name.
     * @param[in] expected Device the image targets, or Device::kUnknown for
     * identify and erase.
     * @param[in] image Validated physical words for flash and verify.
     */
    void Execute(Task task, const QString& port, Device expected,
                 const Image& image);

    /**
     * @brief Installs Leonardo firmware through the Caterina bootloader.
     *
     * Resets a sketch port into its bootloader with a 1200-baud touch,
     * programs and verifies the image with AVRDUDE, then waits for the
     * sketch port to return. Emits FirmwareFinished().
     *
     * @param[in] port Sketch or bootloader port of the programmer.
     * @param[in] hex Intel HEX application image.
     * @param[in] avrdude AVRDUDE executable.
     * @param[in] config avrdude.conf, or empty for AVRDUDE's default.
     */
    void InstallFirmware(const LeonardoPort& port, const QString& hex,
                         const QString& avrdude, const QString& config);

signals:
    /** @brief Delivers the result of Probe(). */
    void ProbeFinished(const atf::gui::ProbeResult& result);

    /**
     * @brief Reports progress of the current stage.
     *
     * @param[in] stage Stage caption.
     * @param[in] done Completed units; zero at stage start.
     * @param[in] total Units in the stage; zero for an indeterminate stage.
     */
    void Progress(const QString& stage, int done, int total);

    /** @brief Adds one line to the session log. */
    void Log(const QString& text);

    /** @brief Reports the IDCODE read during Execute(). */
    void DeviceIdentified(const QString& idcode);

    /**
     * @brief Ends Execute().
     *
     * @param[in] ok True on success.
     * @param[in] message Summary or diagnostic.
     */
    void OperationFinished(bool ok, const QString& message);

    /**
     * @brief Ends InstallFirmware().
     *
     * @param[in] ok True when AVRDUDE programmed and verified the image.
     * @param[in] message Summary or diagnostic.
     * @param[in] port Sketch port after installation; empty if none returned.
     */
    void FirmwareFinished(bool ok, const QString& message, const QString& port);

private:
    /**
     * @brief Polls for a Leonardo port in the requested mode.
     *
     * @param[in] bootloader True to wait for a bootloader port.
     * @param[in] timeout_ms Maximum wait in milliseconds.
     * @param[in] previous Names present before the mode change; ports not in
     * this set are preferred.
     * @return Port name, or empty on timeout.
     */
    QString WaitForPort(bool bootloader, int timeout_ms,
                        const QSet<QString>& previous);

    /**
     * @brief Runs AVRDUDE and forwards its output to the log.
     *
     * @param[in] avrdude Executable path.
     * @param[in] config Configuration path, or empty.
     * @param[in] port Bootloader port.
     * @param[in] hex Image to write.
     * @param[out] error Non-null diagnostic destination on failure.
     * @return True when AVRDUDE exits successfully.
     */
    bool RunAvrdude(const QString& avrdude, const QString& config,
                    const QString& port, const QString& hex, QString* error);
};

}  // namespace gui
}  // namespace atf

Q_DECLARE_METATYPE(atf::gui::ProbeResult)

#endif  // ATF150X_PROGRAMMER_GUI_SRC_PROGRAMMER_WORKER_H_
