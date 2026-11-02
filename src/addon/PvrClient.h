/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../core/Device.h"
#include "../core/Guide.h"
#include "../core/Pump.h"
#include "../core/RecordingStore.h"
#include "../core/Scheduler.h"

#include <kodi/AddonBase.h>
#include <kodi/addon-instance/PVR.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ATTR_DLL_LOCAL CPvrClient : public kodi::addon::CInstancePVRClient, public dt::ISchedulerHost
{
public:
  explicit CPvrClient(const kodi::addon::IInstanceInfo& instance);
  ~CPvrClient() override;

  // General
  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override;
  PVR_ERROR OnSystemWake() override;

  // Channels
  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& status) override;

  // Guide
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR SetEPGMaxPastDays(int) override { return PVR_ERROR_NO_ERROR; }
  PVR_ERROR SetEPGMaxFutureDays(int) override { return PVR_ERROR_NO_ERROR; }

  // Recordings
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& size) override;

  // Timers
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;

  // Live stream
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int ReadLiveStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekLiveStream(int64_t position, int whence) override;
  int64_t LengthLiveStream() override;
  bool CanPauseStream() override;
  bool CanSeekStream() override;
  bool IsRealTimeStream() override;
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;
  PVR_ERROR GetStreamReadChunkSize(int& chunkSize) override;

  // Recorded stream
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recording) override;
  void CloseRecordedStream() override;
  int ReadRecordedStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekRecordedStream(int64_t position, int whence) override;
  int64_t LengthRecordedStream() override;

  // ISchedulerHost
  void NotifyUser(bool isError, const std::string& text) override;
  void TimersChanged() override;
  void RecordingsChanged() override;
  bool LookupChannel(int uid, dt::Channel& out) override;

private:
  struct Settings
  {
    std::string manualHost;
    std::string guideFile;
    std::string recordingsFolder;
    bool hideDrm = true;
    int marginBefore = 1;
    int marginAfter = 3;
    int timeshiftGb = 4;
  };

  void LoadSettings();
  bool ConnectDevice();
  void ServiceLoop();
  void RefreshDeviceGuide();
  void RefreshGuideFile(bool force);
  void GuideUpdated();
  dt::Timer FromKodi(const kodi::addon::PVRTimer& timer);
  std::string RecordingPath(const std::string& id);

  Settings m_settings;
  dt::DeviceInfo m_device;
  std::atomic<bool> m_connected{false};
  mutable std::mutex m_deviceMutex;
  std::vector<dt::Channel> m_channels;
  std::map<int, std::string> m_icons;

  dt::TunerPool m_pool;
  dt::GuideStore m_guide;
  dt::RecordingStore m_store;
  std::unique_ptr<dt::Scheduler> m_scheduler;

  std::thread m_service;
  std::mutex m_serviceMutex;
  std::condition_variable m_serviceWake;
  bool m_serviceStop = false;
  bool m_refreshNow = false;
  time_t m_guideFileTime = 0;

  std::mutex m_liveMutex;
  std::unique_ptr<dt::TunerLease> m_liveLease;
  std::shared_ptr<dt::TimeshiftBuffer> m_live;
  int m_liveChannel = 0;
  int64_t m_timeshiftCapacity = 0;

  std::mutex m_playMutex;
  std::FILE* m_playFile = nullptr;
  std::string m_playId;
};
