// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors
#include "cli/src/serial.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "cli/src/status.h"
#include "firmware/atf1502_programmer/protocol.h"

namespace atf {
namespace {

using Clock = std::chrono::steady_clock;

// Capture the OS error before further I/O can overwrite it.
Status SystemError(const char* action) {
#ifdef _WIN32
  return Status(std::string(action) + " (Windows error " +
                std::to_string(GetLastError()) + ")");
#else
  return Status(std::string(action) + ": " + std::strerror(errno));
#endif
}

}  // namespace

#ifdef _WIN32
struct Serial::Impl {
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~Impl() {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};
#else
struct Serial::Impl {
  int fd = -1;
  ~Impl() {
    if (fd >= 0) {
      close(fd);
    }
  }
};
#endif

Serial::Serial() = default;
Serial::~Serial() = default;

Status Serial::Open(const std::string& port) {
  impl_.reset();
  // Commit ownership only after setup succeeds. Every early return closes the
  // local handle, including failures after the port was successfully opened.
  auto candidate = std::make_unique<Impl>();
#ifdef _WIN32
  std::string path = port.rfind("\\\\.\\", 0) == 0 ? port : "\\\\.\\" + port;
  candidate->handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  if (candidate->handle == INVALID_HANDLE_VALUE) {
    return SystemError("Cannot open serial port");
  }
  DCB settings{};
  settings.DCBlength = sizeof(settings);
  if (!GetCommState(candidate->handle, &settings)) {
    return SystemError("GetCommState");
  }
  settings.BaudRate = CBR_115200;
  settings.ByteSize = 8;
  settings.Parity = NOPARITY;
  settings.StopBits = ONESTOPBIT;
  settings.fBinary = TRUE;
  settings.fParity = FALSE;
  settings.fOutxCtsFlow = FALSE;
  settings.fOutxDsrFlow = FALSE;
  settings.fDtrControl = DTR_CONTROL_ENABLE;
  settings.fDsrSensitivity = FALSE;
  settings.fOutX = FALSE;
  settings.fInX = FALSE;
  settings.fRtsControl = RTS_CONTROL_DISABLE;
  settings.fAbortOnError = FALSE;
  settings.fErrorChar = FALSE;
  settings.fNull = FALSE;
  if (!SetCommState(candidate->handle, &settings)) {
    return SystemError("SetCommState");
  }
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutConstant = 2000;
  if (!SetCommTimeouts(candidate->handle, &timeouts)) {
    return SystemError("SetCommTimeouts");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  if (!PurgeComm(candidate->handle, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
    return SystemError("PurgeComm");
  }
#else
  candidate->fd =
      open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (candidate->fd < 0) {
    return SystemError("Cannot open serial port");
  }
  if (ioctl(candidate->fd, TIOCEXCL) != 0) {
    return SystemError("Cannot exclusively claim serial port");
  }
  termios settings{};
  if (tcgetattr(candidate->fd, &settings) != 0) {
    return SystemError("tcgetattr");
  }
  cfmakeraw(&settings);
  cfsetispeed(&settings, B115200);
  cfsetospeed(&settings, B115200);
  settings.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
  settings.c_cflag |= CS8 | CLOCAL | CREAD | HUPCL;
  settings.c_cc[VMIN] = 0;
  settings.c_cc[VTIME] = 0;
  if (tcsetattr(candidate->fd, TCSANOW, &settings) != 0) {
    return SystemError("tcsetattr");
  }
  int dtr = TIOCM_DTR;
  // PTYs used by the integration test do not implement modem signals.
  if (ioctl(candidate->fd, TIOCMBIS, &dtr) != 0 && errno != ENOTTY) {
    return SystemError("Cannot assert DTR");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  if (tcflush(candidate->fd, TCIOFLUSH) != 0) {
    return SystemError("tcflush");
  }
#endif
  impl_ = std::move(candidate);
  return Status();
}

Status Serial::Write(const std::string& text) {
  if (impl_ == nullptr) {
    return Status("Serial port is not open");
  }
  size_t position = 0;
  auto deadline = Clock::now() + std::chrono::seconds(2);
  while (position < text.size()) {
    if (Clock::now() > deadline) {
      return Status("Serial write timed out");
    }
#ifdef _WIN32
    DWORD count = 0;
    if (!WriteFile(impl_->handle, text.data() + position,
                   static_cast<DWORD>(text.size() - position), &count,
                   nullptr)) {
      return SystemError("Serial write");
    }
    if (count == 0) {
      return Status("Serial write timed out");
    }
#else
    pollfd descriptor{impl_->fd, POLLOUT, 0};
    int result = poll(&descriptor, 1, 50);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0) {
      return SystemError("Serial poll");
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return Status("Serial port disconnected");
    }
    if (!(descriptor.revents & POLLOUT)) {
      continue;
    }
    ssize_t count =
        write(impl_->fd, text.data() + position, text.size() - position);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    }
    if (count < 0) {
      return SystemError("Serial write");
    }
#endif
    position += static_cast<size_t>(count);
  }
  return Status();
}

Status Serial::ReadLine(std::string* output) {
  if (impl_ == nullptr) {
    return Status("Serial port is not open");
  }
  auto deadline = Clock::now() + std::chrono::seconds(3);
  std::string text;
  while (Clock::now() < deadline) {
    char c;
#ifdef _WIN32
    DWORD count = 0;
    if (!ReadFile(impl_->handle, &c, 1, &count, nullptr)) {
      return SystemError("Serial read");
    }
    if (count == 0) {
      continue;
    }
#else
    pollfd descriptor{impl_->fd, POLLIN, 0};
    int result = poll(&descriptor, 1, 50);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0) {
      return SystemError("Serial poll");
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return Status("Serial port disconnected");
    }
    if (!(descriptor.revents & POLLIN)) {
      continue;
    }
    ssize_t count = read(impl_->fd, &c, 1);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    }
    if (count < 0) {
      return SystemError("Serial read");
    }
    if (count == 0) {
      continue;
    }
#endif
    if (c == '\n') {
      *output = std::move(text);
      return Status();
    }
    if (c != '\r') {
      text += c;
    }
    if (text.size() > 95) {
      return Status("Oversized firmware response");
    }
  }
  return Status(
      "Firmware response timed out; check firmware, port and USB "
      "connection");
}

Status Connection::Open(const std::string& port) {
  Status status = serial_.Open(port);
  if (!status.ok()) {
    return status;
  }
  std::string version;
  status = Command("HELLO", &version);
  if (!status.ok()) {
    return status;
  }
  if (version != "ATF1502 1") {
    return Status("Incompatible programmer firmware");
  }
  return Status();
}

Status Connection::Command(const std::string& text, std::string* output) {
  sequence_ = (sequence_ + 1) & 65535;
  std::string body = std::to_string(sequence_) + " " + text;
  std::ostringstream packet;
  packet << body << '*' << std::hex << std::uppercase << std::setw(4)
         << std::setfill('0') << Crc16(body.data(), body.size()) << '\n';
  Status status = serial_.Write(packet.str());
  if (!status.ok()) {
    return status;
  }
  std::string response;
  status = serial_.ReadLine(&response);
  if (!status.ok()) {
    return status;
  }
  size_t star = response.find('*');
  if (star == std::string::npos || response.size() - star != 5) {
    return Status("Malformed firmware response");
  }
  unsigned int crc = 0;
  for (size_t i = star + 1; i < response.size(); ++i) {
    int digit = HexDigit(response[i]);
    if (digit < 0) {
      return Status("Invalid response checksum");
    }
    crc = (crc << 4) | static_cast<unsigned int>(digit);
  }
  if (crc != Crc16(response.data(), star)) {
    return Status("Response CRC mismatch");
  }
  response.resize(star);
  std::string prefix = std::to_string(sequence_) + " ";
  if (response.rfind(prefix, 0) != 0) {
    return Status("Response sequence mismatch");
  }
  response.erase(0, prefix.size());
  if (response == "OK") {
    if (output != nullptr) {
      output->clear();
    }
    return Status();
  }
  if (response.rfind("OK ", 0) != 0) {
    return Status("Programmer: " + response);
  }
  if (output != nullptr) {
    *output = response.substr(3);
  }
  return Status();
}

}  // namespace atf
