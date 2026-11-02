/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace dt
{

void NetInit();

// Plain http GET to a device on the LAN. Https goes through the host's fetcher instead.
bool HttpGetText(const std::string& url, int timeoutMs, std::string& body, int* status = nullptr);

class HttpStream
{
public:
  HttpStream() = default;
  ~HttpStream();
  HttpStream(const HttpStream&) = delete;
  HttpStream& operator=(const HttpStream&) = delete;

  // Connects and reads the response headers. The tuner can take 10 seconds to answer.
  bool Open(const std::string& url, int timeoutMs);
  // Bytes read, 0 on a timeout, or -1 when the connection has ended.
  int Read(uint8_t* buffer, size_t size, int timeoutMs);
  void Close();

  int Status() const { return m_status; }
  // The X-HDHomeRun-Error code, such as 805 for all tuners in use.
  int DeviceError() const { return m_deviceError; }
  const std::string& LocalAddress() const { return m_localAddress; }

private:
  std::atomic<intptr_t> m_socket{-1};
  int m_status = 0;
  int m_deviceError = 0;
  std::string m_localAddress;
  std::vector<uint8_t> m_pending;
  size_t m_pendingOffset = 0;
};

// Finds tuners with the HDHomeRun UDP discovery broadcast. Returns their IP addresses.
std::vector<std::string> DiscoverDevices(int timeoutMs);

} // namespace dt
