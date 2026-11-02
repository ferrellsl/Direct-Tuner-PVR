/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// Exercises the core against a real tuner, with no Kodi:
//   directtuner_tool info [ip]
//   directtuner_tool record <ip> <guide number> <seconds> <out.ts>
//   directtuner_tool share <ip> <guide number> <seconds>      watcher and recording, one tuner
//   directtuner_tool preempt <ip> <seconds>                   recording takes a tuner from live TV

#include "../src/core/Common.h"
#include "../src/core/Device.h"
#include "../src/core/Net.h"
#include "../src/core/Pump.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace dt;

namespace
{

class FileWriter : public IStreamSink
{
public:
  explicit FileWriter(std::FILE* file) : m_file(file) {}
  bool OnData(const uint8_t* data, size_t size) override
  {
    m_bytes += size;
    return !m_file || std::fwrite(data, 1, size, m_file) == size;
  }
  void OnGap() override { std::printf("gap\n"); }
  uint64_t Bytes() const { return m_bytes; }

private:
  std::FILE* m_file;
  std::atomic<uint64_t> m_bytes{0};
};

bool Connect(const char* ip, DeviceInfo& device, std::vector<Channel>& channels)
{
  std::string error;
  if (!FindDevice(ip ? ip : "", device, error))
  {
    std::printf("%s\n", error.c_str());
    return false;
  }
  return FetchLineup(device, true, channels);
}

const Channel* FindChannel(const std::vector<Channel>& channels, const std::string& number)
{
  for (const Channel& channel : channels)
    if (channel.guideNumber == number)
      return &channel;
  return nullptr;
}

void Sleep(int seconds)
{
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

} // namespace

int main(int argc, char** argv)
{
  NetInit();
  SetLogSink([](LogLevel, const std::string& text) { std::printf("log: %s\n", text.c_str()); });

  const std::string command = argc > 1 ? argv[1] : "info";
  DeviceInfo device;
  std::vector<Channel> channels;

  if (command == "info")
  {
    for (const std::string& host : DiscoverDevices(1200))
      std::printf("discovered %s\n", host.c_str());
    if (!Connect(argc > 2 ? argv[2] : nullptr, device, channels))
      return 1;
    std::printf("%s %s %s tuners=%d channels=%d\n", device.friendlyName.c_str(), device.model.c_str(),
                device.baseUrl.c_str(), device.tunerCount, static_cast<int>(channels.size()));
    return 0;
  }

  if (argc < 3 || !Connect(argv[2], device, channels))
    return 1;

  TunerPool pool;
  pool.SetTunerCount(device.tunerCount);
  pool.SetPreemptHandler([](const Channel& channel) {
    std::printf("preempted live TV on %s\n", channel.guideNumber.c_str());
  });

  if (command == "record" && argc >= 6)
  {
    const Channel* channel = FindChannel(channels, argv[3]);
    if (!channel)
      return 1;
    std::FILE* file = std::fopen(argv[5], "wb");
    auto sink = std::make_shared<FileWriter>(file);
    TuneResult result;
    auto lease = pool.Acquire(*channel, TunerUseKind::Recording, result);
    if (!lease)
    {
      std::printf("tune failed: %s\n", TuneResultText(result).c_str());
      return 1;
    }
    lease->Pump().AddSink(sink);
    Sleep(std::atoi(argv[4]));
    lease->Pump().RemoveSink(sink);
    lease.reset();
    std::fclose(file);
    std::printf("wrote %llu bytes\n", static_cast<unsigned long long>(sink->Bytes()));
    return 0;
  }

  if (command == "share" && argc >= 5)
  {
    const Channel* channel = FindChannel(channels, argv[3]);
    if (!channel)
      return 1;
    auto one = std::make_shared<FileWriter>(nullptr);
    auto two = std::make_shared<FileWriter>(nullptr);
    TuneResult result;
    auto live = pool.Acquire(*channel, TunerUseKind::Live, result);
    auto record = pool.Acquire(*channel, TunerUseKind::Recording, result);
    if (!live || !record)
      return 1;
    live->Pump().AddSink(one);
    record->Pump().AddSink(two);
    Sleep(std::atoi(argv[4]));
    std::vector<TunerUse> tuners;
    FetchTunerStatus(device, tuners);
    int used = 0;
    for (const TunerUse& tuner : tuners)
      used += tuner.channel.empty() ? 0 : 1;
    live->Pump().RemoveSink(one);
    record->Pump().RemoveSink(two);
    std::printf("tuners in use %d, pool %d, bytes %llu and %llu\n", used, pool.ActiveCount(),
                static_cast<unsigned long long>(one->Bytes()),
                static_cast<unsigned long long>(two->Bytes()));
    return 0;
  }

  if (command == "preempt" && argc >= 4)
  {
    pool.SetTunerCount(1);
    const std::vector<const char*> numbers = {"3.1", "5.1"};
    TuneResult result;
    auto live = pool.Acquire(*FindChannel(channels, numbers[0]), TunerUseKind::Live, result);
    if (!live)
      return 1;
    Sleep(2);
    auto second = pool.Acquire(*FindChannel(channels, numbers[1]), TunerUseKind::Live, result);
    std::printf("second live: %s\n", second ? "got one (wrong)" : TuneResultText(result).c_str());
    auto record = pool.Acquire(*FindChannel(channels, numbers[1]), TunerUseKind::Recording, result);
    std::printf("recording: %s, live still running: %d\n", record ? "got the tuner" : "refused",
                live->Pump().Running() ? 1 : 0);
    Sleep(std::atoi(argv[3]));
    if (record)
      std::printf("recording pump bytes %llu\n", static_cast<unsigned long long>(record->Pump().Bytes()));
    return 0;
  }

  std::printf("unknown command\n");
  return 1;
}
