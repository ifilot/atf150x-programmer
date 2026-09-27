// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Worker-thread programmer sessions and Leonardo firmware updates.
 */
#include "gui/src/programmer_worker.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSerialPort>
#include <QThread>
#include <string>

#include "cli/src/programmer.h"
#include "cli/src/serial.h"

namespace atf {
namespace gui {
namespace {

/**
 * @brief Converts a core status message for display.
 *
 * @param[in] status Failed status.
 * @return Message as a QString.
 */
QString Message(const Status& status) {
    return QString::fromStdString(status.message());
}

}  // namespace

/**
 * @brief Classifies the port by its HELLO reply.
 */
void ProgrammerWorker::Probe(const QString& port) {
    ProbeResult result;
    result.port = port;
    Connection connection;
    std::string hello;
    Status status = connection.Connect(port.toStdString(), &hello);
    if (!status.ok()) {
        result.error = Message(status);
        // Handshake failures after a successful open mean a foreign sketch.
        result.state = result.error.startsWith(QStringLiteral("Cannot open"))
                           ? FirmwareState::kPortError
                           : FirmwareState::kNoResponse;
        emit ProbeFinished(result);
        return;
    }
    result.hello = QString::fromStdString(hello);
    if (hello != ExpectedHello()) {
        result.state = result.hello.startsWith(QStringLiteral("ATF15XX "))
                           ? FirmwareState::kDifferentVersion
                           : FirmwareState::kNoResponse;
        emit ProbeFinished(result);
        return;
    }
    result.state = FirmwareState::kCompatible;
    std::string id;
    status = connection.Command("ID", &id);
    if (status.ok()) {
        result.idcode = QString::fromStdString(id);
    } else {
        result.error = Message(status);
    }
    emit ProbeFinished(result);
}

/**
 * @brief Identifies the CPLD, then runs the shared session flow.
 */
void ProgrammerWorker::Execute(Task task, const QString& port, Device expected,
                               const Image& image) {
    Connection connection;
    emit Progress(tr("Connecting"), 0, 0);
    Status status = connection.Open(port.toStdString());
    if (!status.ok()) {
        emit OperationFinished(false, Message(status));
        return;
    }
    std::string id;
    status = connection.Command("ID", &id);
    if (!status.ok()) {
        emit OperationFinished(false, Message(status));
        return;
    }
    QString idcode = QString::fromStdString(id);
    emit DeviceIdentified(idcode);
    Device device = DeviceFromIdText(id);
    if (device == Device::kUnknown) {
        emit OperationFinished(
            false, tr("No supported CPLD answered (IDCODE 0x%1). Check that "
                      "an ATF1502AS or ATF1504AS is seated correctly.")
                       .arg(idcode));
        return;
    }
    QString name = QString::fromLatin1(DeviceName(device));
    if (task == Task::kIdentify) {
        emit OperationFinished(true,
                               tr("Found %1 (IDCODE 0x%2).").arg(name, idcode));
        return;
    }
    if (expected != Device::kUnknown && expected != device) {
        emit OperationFinished(
            false, tr("The JEDEC file targets %1, but the socket holds %2.")
                       .arg(QString::fromLatin1(DeviceName(expected)), name));
        return;
    }
    Operation operation = task == Task::kErase   ? Operation::kErase
                          : task == Task::kFlash ? Operation::kFlash
                                                 : Operation::kVerify;
    status =
        RunSession(connection, operation, device, image,
                   [this](Stage stage, unsigned int done, unsigned int total) {
                       QString caption = QString::fromLatin1(StageName(stage));
                       if (done == 0) {
                           emit Log(caption + QStringLiteral("…"));
                       }
                       emit Progress(caption, static_cast<int>(done),
                                     static_cast<int>(total));
                   });
    if (!status.ok()) {
        emit OperationFinished(false, Message(status));
        return;
    }
    QString summary = task == Task::kErase ? tr("%1 erased and blank-checked.")
                      : task == Task::kFlash ? tr("%1 programmed and verified.")
                                             : tr("%1 matches the JEDEC file.");
    emit OperationFinished(true, summary.arg(name));
}

/**
 * @brief Touch, program and wait, reporting each step to the log.
 */
void ProgrammerWorker::InstallFirmware(const LeonardoPort& port,
                                       const QString& hex,
                                       const QString& avrdude,
                                       const QString& config) {
    QSet<QString> sketch_ports;
    QSet<QString> boot_ports;
    for (const LeonardoPort& existing : FindLeonardoPorts()) {
        (existing.bootloader ? boot_ports : sketch_ports).insert(existing.name);
    }
    QString boot_port = port.name;
    if (!port.bootloader) {
        emit Progress(tr("Starting bootloader"), 0, 0);
        emit Log(tr("Resetting %1 into the USB bootloader…").arg(port.name));
        QSerialPort serial;
        serial.setPortName(port.name);
        serial.setBaudRate(1200);
        if (!serial.open(QIODevice::ReadWrite)) {
            emit FirmwareFinished(
                false,
                tr("Cannot open %1: %2").arg(port.name, serial.errorString()),
                QString());
            return;
        }
        // Closing a 1200-baud connection with DTR low starts Caterina.
        serial.setDataTerminalReady(false);
        serial.close();
        boot_port = WaitForPort(true, 10000, boot_ports);
        if (boot_port.isEmpty()) {
            emit FirmwareFinished(
                false,
                tr("The Leonardo bootloader did not appear. Press the reset "
                   "button on the Leonardo and try again within 8 seconds."),
                QString());
            return;
        }
        emit Log(tr("Bootloader found on %1.").arg(boot_port));
        // Give the operating system a moment to finish enumerating the port.
        QThread::msleep(400);
    }

    emit Progress(tr("Writing firmware"), 0, 0);
    QString error;
    if (!RunAvrdude(avrdude, config, boot_port, hex, &error)) {
        emit FirmwareFinished(false, error, QString());
        return;
    }

    emit Progress(tr("Restarting programmer"), 0, 0);
    emit Log(tr("Firmware written and verified; waiting for the programmer…"));
    sketch_ports.remove(port.name);
    QString sketch_port = WaitForPort(false, 15000, sketch_ports);
    if (sketch_port.isEmpty()) {
        emit FirmwareFinished(
            true,
            tr("Firmware written and verified, but the programmer did not "
               "reappear within 15 seconds. Reconnect its USB cable."),
            QString());
        return;
    }
    QThread::msleep(400);
    emit FirmwareFinished(
        true, tr("Firmware installed; programmer is on %1.").arg(sketch_port),
        sketch_port);
}

/**
 * @brief Prefers newly appeared ports, then accepts any matching one.
 */
QString ProgrammerWorker::WaitForPort(bool bootloader, int timeout_ms,
                                      const QSet<QString>& previous) {
    QElapsedTimer timer;
    timer.start();
    QString fallback;
    while (timer.elapsed() < timeout_ms) {
        fallback.clear();
        for (const LeonardoPort& port : FindLeonardoPorts()) {
            if (port.bootloader != bootloader) {
                continue;
            }
            if (!previous.contains(port.name)) {
                return port.name;
            }
            fallback = port.name;
        }
        // A port that kept its name counts once the mode change had time.
        if (!fallback.isEmpty() && timer.elapsed() > 2000) {
            return fallback;
        }
        QThread::msleep(100);
    }
    return fallback;
}

/**
 * @brief Uses the Arduino IDE's Caterina upload recipe.
 */
bool ProgrammerWorker::RunAvrdude(const QString& avrdude, const QString& config,
                                  const QString& port, const QString& hex,
                                  QString* error) {
    QStringList arguments;
    if (!config.isEmpty()) {
        arguments << QStringLiteral("-C") << config;
    }
    arguments << QStringLiteral("-p") << QStringLiteral("atmega32u4")
              << QStringLiteral("-c") << QStringLiteral("avr109")
              << QStringLiteral("-P") << port << QStringLiteral("-b")
              << QStringLiteral("57600") << QStringLiteral("-D")
              << QStringLiteral("-U")
              << QStringLiteral("flash:w:%1:i").arg(hex);
    emit Log(QStringLiteral("> %1 %2").arg(QFileInfo(avrdude).fileName(),
                                           arguments.join(QLatin1Char(' '))));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(avrdude, arguments);
    if (!process.waitForStarted(5000)) {
        *error =
            tr("AVRDUDE could not be started: %1").arg(process.errorString());
        return false;
    }
    QByteArray pending;
    QString tail;
    auto drain = [&](bool flush) {
        pending += process.readAll();
        pending.replace('\r', '\n');
        int newline;
        while ((newline = pending.indexOf('\n')) >= 0 ||
               (flush && !pending.isEmpty())) {
            int length = newline >= 0 ? newline : pending.size();
            QString line =
                QString::fromLocal8Bit(pending.left(length)).trimmed();
            pending.remove(0, newline >= 0 ? newline + 1 : length);
            if (!line.isEmpty()) {
                emit Log(QStringLiteral("  ") + line);
                tail = line;
            }
        }
    };
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        drain(false);
        if (timer.elapsed() > 120000) {
            process.kill();
            process.waitForFinished(3000);
            *error =
                tr("AVRDUDE timed out after two minutes. The bootloader "
                   "is protected; retry the installation.");
            return false;
        }
    }
    drain(true);
    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        *error = tr("AVRDUDE failed with exit code %1: %2")
                     .arg(process.exitCode())
                     .arg(tail);
        return false;
    }
    return true;
}

}  // namespace gui
}  // namespace atf
