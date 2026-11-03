/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "../core/Backend.h"
#include "../core/Pump.h"

#include <kodi/AddonBase.h>
#include <kodi/addon-instance/PVR.h>

#include <memory>
#include <mutex>
#include <string>

// Translates between Kodi and a backend: this Kodi's own (Backend), or the recorder service on
// another computer (RemoteBackend).
class ATTR_DLL_LOCAL CPvrClient : public kodi::addon::CInstancePVRClient, public dt::IBackendHost
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

  // IBackendHost
  void Notify(bool isError, const std::string& text) override;
  void ConnectionChanged(bool connected, const std::string& text) override;
  void ChannelsChanged() override;
  void GuideChanged() override;
  void TimersChanged() override;
  void RecordingsChanged() override;
  void LiveStopped(int channelUid, const std::string& reason) override;

private:
  struct Settings
  {
    int recorderMode = 0; // 0 find on the network, 1 none, 2 the address below
    std::string recorderAddress;
    dt::BackendSettings backend;
    int marginBefore = 1;
    int marginAfter = 3;
    int timeshiftGb = 4;
  };

  void LoadSettings();
  // The recorder service to use, or empty for this Kodi's own backend.
  std::string FindRecorder();
  // Hands this Kodi's own timers to the recorder the first time it's used.
  void MoveTimersToRecorder();
  dt::Timer FromKodi(const kodi::addon::PVRTimer& timer);

  Settings m_settings;
  bool m_remote = false;
  std::string m_timeshiftFolder;
  int64_t m_timeshiftCapacity = 0;
  std::unique_ptr<dt::IBackend> m_backend;

  std::mutex m_liveMutex;
  std::unique_ptr<dt::LiveHandle> m_liveHandle;
  std::shared_ptr<dt::TimeshiftBuffer> m_live;
  int m_liveChannel = 0;

  std::mutex m_playMutex;
  std::unique_ptr<dt::MediaReader> m_play;
};
