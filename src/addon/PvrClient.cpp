/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PvrClient.h"

#include "../core/Common.h"
#include "../core/Net.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/addon-instance/inputstream/TimingConstants.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define Seek64 _fseeki64
#define Tell64 _ftelli64
#else
#define Seek64 fseeko
#define Tell64 ftello
#endif

namespace
{

constexpr unsigned int TIMER_ONCE_MANUAL = 1;
constexpr unsigned int TIMER_ONCE_EPG = 2;
constexpr unsigned int TIMER_SERIES_EPG = 3;
constexpr unsigned int TIMER_SERIES_MANUAL = 4;
constexpr unsigned int TIMER_CHILD = 5;

constexpr int GUIDE_REFRESH_SECONDS = 4 * 60 * 60;
constexpr int LINEUP_REFRESH_SECONDS = 60 * 60;
constexpr int FILE_CHECK_SECONDS = 5 * 60;
constexpr int RECONNECT_SECONDS = 30;
constexpr int LIVE_READ_TIMEOUT_MS = 20000;
constexpr int64_t MIN_TIMESHIFT_BYTES = 256LL * 1024 * 1024;

void LogFromCore(dt::LogLevel level, const std::string& text)
{
  ADDON_LOG kodiLevel = ADDON_LOG_DEBUG;
  switch (level)
  {
    case dt::LogLevel::Info:
      kodiLevel = ADDON_LOG_INFO;
      break;
    case dt::LogLevel::Warning:
      kodiLevel = ADDON_LOG_WARNING;
      break;
    case dt::LogLevel::Error:
      kodiLevel = ADDON_LOG_ERROR;
      break;
    default:
      break;
  }
  kodi::Log(kodiLevel, "%s", text.c_str());
}

std::string DefaultRecordingsFolder()
{
#ifdef _WIN32
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (!home)
    return kodi::addon::GetUserPath("recordings");
  return dt::PathToUtf8(std::filesystem::path(home) / "Videos" / "Kodi Recordings");
}

bool FetchWithKodi(const std::string& url, std::string& body)
{
  kodi::vfs::CFile file;
  if (!file.OpenFile(url, ADDON_READ_NO_CACHE))
    return false;
  body.clear();
  char buffer[32 * 1024];
  ssize_t count;
  while ((count = file.Read(buffer, sizeof(buffer))) > 0)
    body.append(buffer, static_cast<size_t>(count));
  return !body.empty();
}

PVR_TIMER_STATE StateToKodi(dt::TimerState state)
{
  switch (state)
  {
    case dt::TimerState::Scheduled:
      return PVR_TIMER_STATE_SCHEDULED;
    case dt::TimerState::Recording:
      return PVR_TIMER_STATE_RECORDING;
    case dt::TimerState::Conflict:
      return PVR_TIMER_STATE_CONFLICT_NOK;
    case dt::TimerState::Disabled:
      return PVR_TIMER_STATE_DISABLED;
    case dt::TimerState::Error:
      return PVR_TIMER_STATE_ERROR;
  }
  return PVR_TIMER_STATE_NEW;
}

kodi::addon::PVRTimerType MakeType(unsigned int id, uint64_t attributes, const std::string& text)
{
  kodi::addon::PVRTimerType type;
  type.SetId(id);
  type.SetAttributes(attributes);
  type.SetDescription(text);
  return type;
}

} // namespace

CPvrClient::CPvrClient(const kodi::addon::IInstanceInfo& instance) : CInstancePVRClient(instance)
{
  dt::SetLogSink(LogFromCore);
  dt::NetInit();
  LoadSettings();

  m_store.SetFolder(m_settings.recordingsFolder);
  std::error_code error;
  std::filesystem::create_directories(dt::PathFromUtf8(m_settings.recordingsFolder), error);
  if (error)
    NotifyUser(true, "Can't create the recordings folder " + m_settings.recordingsFolder);

  m_pool.SetPreemptHandler([this](const dt::Channel& channel) {
    {
      std::lock_guard<std::mutex> lock(m_liveMutex);
      if (m_live && m_liveChannel == channel.uid)
        m_live->MarkEnded();
    }
    NotifyUser(true, "Live TV stopped: a recording needed the tuner");
  });

  ConnectDevice();

  m_scheduler = std::make_unique<dt::Scheduler>(m_pool, m_store, m_guide, *this);
  m_scheduler->Start(dt::PathToUtf8(dt::PathFromUtf8(kodi::addon::GetUserPath()) / "timers.json"));

  m_service = std::thread([this] { ServiceLoop(); });
}

CPvrClient::~CPvrClient()
{
  {
    std::lock_guard<std::mutex> lock(m_serviceMutex);
    m_serviceStop = true;
    m_serviceWake.notify_all();
  }
  if (m_service.joinable())
    m_service.join();

  m_scheduler->Stop();
  CloseLiveStream();
  CloseRecordedStream();
  dt::SetLogSink(nullptr);
}

void CPvrClient::LoadSettings()
{
  m_settings.manualHost = dt::Trim(kodi::addon::GetSettingString("manual_host"));
  m_settings.guideFile = dt::Trim(kodi::addon::GetSettingString("guide_file"));
  m_settings.recordingsFolder = dt::Trim(kodi::addon::GetSettingString("recordings_folder"));
  m_settings.hideDrm = kodi::addon::GetSettingBoolean("hide_drm", true);
  m_settings.marginBefore = kodi::addon::GetSettingInt("margin_before", 1);
  m_settings.marginAfter = kodi::addon::GetSettingInt("margin_after", 3);
  m_settings.timeshiftGb = kodi::addon::GetSettingInt("timeshift_gb", 4);

  if (m_settings.recordingsFolder.empty())
    m_settings.recordingsFolder = DefaultRecordingsFolder();
  m_settings.recordingsFolder = kodi::vfs::TranslateSpecialProtocol(m_settings.recordingsFolder);
#ifdef _WIN32
  // Kodi's folder browser gives smb:// paths, and the core writes through Windows.
  if (m_settings.recordingsFolder.rfind("smb://", 0) == 0)
  {
    std::string& folder = m_settings.recordingsFolder;
    folder = "\\\\" + folder.substr(6);
    std::replace(folder.begin(), folder.end(), '/', '\\');
  }
#endif
  m_timeshiftCapacity = static_cast<int64_t>(m_settings.timeshiftGb) * 1024 * 1024 * 1024;
}

bool CPvrClient::ConnectDevice()
{
  dt::DeviceInfo device;
  std::string error;
  if (!dt::FindDevice(m_settings.manualHost, device, error))
  {
    kodi::Log(ADDON_LOG_WARNING, "%s", error.c_str());
    ConnectionStateChange(error, PVR_CONNECTION_STATE_SERVER_UNREACHABLE, error);
    m_connected = false;
    return false;
  }

  std::vector<dt::Channel> channels;
  if (!dt::FetchLineup(device, m_settings.hideDrm, channels) || channels.empty())
  {
    error = "The tuner at " + device.baseUrl + " has no channels. Run a channel scan on the tuner.";
    kodi::Log(ADDON_LOG_WARNING, "%s", error.c_str());
    ConnectionStateChange(error, PVR_CONNECTION_STATE_SERVER_UNREACHABLE, error);
    m_connected = false;
    return false;
  }

  kodi::Log(ADDON_LOG_INFO, "Connected to %s %s at %s: %d tuners, %d channels",
            device.friendlyName.c_str(), device.id.c_str(), device.baseUrl.c_str(), device.tunerCount,
            static_cast<int>(channels.size()));

  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    m_device = device;
    m_channels = std::move(channels);
  }
  m_pool.SetTunerCount(device.tunerCount);
  if (m_scheduler)
    m_scheduler->TunerCountChanged();
  m_connected = true;
  ConnectionStateChange(device.baseUrl, PVR_CONNECTION_STATE_CONNECTED, "");
  return true;
}

void CPvrClient::GuideUpdated()
{
  std::vector<int> uids;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    for (const dt::Channel& channel : m_channels)
      uids.push_back(channel.uid);
  }
  for (const int uid : uids)
    TriggerEpgUpdate(static_cast<unsigned int>(uid));
  m_scheduler->GuideChanged();
}

void CPvrClient::RefreshDeviceGuide()
{
  dt::DeviceInfo device;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    device = m_device;
  }

  dt::ChannelEvents events;
  std::map<int, std::string> icons;
  if (!dt::FetchDeviceGuide(device, FetchWithKodi, events, icons))
  {
    kodi::Log(ADDON_LOG_WARNING, "Couldn't read the guide from SiliconDust");
    return;
  }

  size_t count = 0;
  for (const auto& item : events)
    count += item.second.size();
  kodi::Log(ADDON_LOG_INFO, "Device guide: %d channels, %d programs", static_cast<int>(events.size()),
            static_cast<int>(count));

  m_guide.SetDeviceGuide(std::move(events));
  bool newIcons;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    newIcons = icons != m_icons;
    m_icons = std::move(icons);
  }
  if (newIcons)
    TriggerChannelUpdate();
}

void CPvrClient::RefreshGuideFile(bool force)
{
  if (m_settings.guideFile.empty())
    return;

  // Kodi's file layer can't open \server\share paths, so plain paths are read directly.
  const bool kodiPath = m_settings.guideFile.find("://") != std::string::npos;
  time_t modified = 0;
  if (kodiPath)
  {
    kodi::vfs::FileStatus status;
    if (kodi::vfs::StatFile(m_settings.guideFile, status))
      modified = status.GetModificationTime();
  }
  else
  {
    std::error_code error;
    const auto written =
        std::filesystem::last_write_time(dt::PathFromUtf8(m_settings.guideFile), error);
    if (!error)
      modified = static_cast<time_t>(written.time_since_epoch().count() / 10000000);
  }
  if (!force && modified == m_guideFileTime)
    return;

  std::string text;
  const bool opened = kodiPath ? FetchWithKodi(m_settings.guideFile, text)
                               : dt::ReadWholeFile(m_settings.guideFile, text);
  if (!opened)
  {
    NotifyUser(true, "Can't open the guide file " + m_settings.guideFile);
    m_guideFileTime = modified;
    return;
  }

  std::vector<dt::Channel> channels;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    channels = m_channels;
  }

  dt::ChannelEvents events;
  dt::XmltvStats stats;
  if (!dt::ParseXmltv(text, channels, events, &stats))
  {
    NotifyUser(true, "The guide file isn't valid XMLTV");
    m_guideFileTime = modified;
    return;
  }

  kodi::Log(ADDON_LOG_INFO, "Guide file: %d of %d channels matched, %d programs", stats.channelsMatched,
            static_cast<int>(channels.size()), stats.programmes);
  m_guide.SetXmltv(std::move(events));
  m_guideFileTime = modified;
}

void CPvrClient::ServiceLoop()
{
  time_t nextGuide = 0;
  time_t nextLineup = std::time(nullptr) + LINEUP_REFRESH_SECONDS;
  time_t nextFile = 0;
  time_t nextConnect = 0;

  std::unique_lock<std::mutex> lock(m_serviceMutex);
  while (!m_serviceStop)
  {
    const time_t now = std::time(nullptr);
    if (m_refreshNow)
    {
      m_refreshNow = false;
      nextGuide = nextFile = nextConnect = nextLineup = 0;
    }
    lock.unlock();

    bool changed = false;
    if (!m_connected && now >= nextConnect)
    {
      nextConnect = now + RECONNECT_SECONDS;
      if (ConnectDevice())
      {
        TriggerChannelUpdate();
        nextGuide = 0;
      }
    }

    if (m_connected)
    {
      if (now >= nextLineup)
      {
        nextLineup = now + LINEUP_REFRESH_SECONDS;
        dt::DeviceInfo device;
        {
          std::lock_guard<std::mutex> deviceLock(m_deviceMutex);
          device = m_device;
        }
        std::vector<dt::Channel> channels;
        if (dt::FetchLineup(device, m_settings.hideDrm, channels) && !channels.empty())
        {
          {
            std::lock_guard<std::mutex> deviceLock(m_deviceMutex);
            m_channels = std::move(channels);
          }
          TriggerChannelUpdate();
        }
        else if (m_pool.ActiveCount() == 0)
        {
          m_connected = false;
          ConnectionStateChange("Lost contact with the tuner", PVR_CONNECTION_STATE_SERVER_UNREACHABLE, "");
        }
      }
      if (now >= nextGuide)
      {
        nextGuide = now + GUIDE_REFRESH_SECONDS;
        RefreshDeviceGuide();
        RefreshGuideFile(true);
        changed = true;
      }
      else if (now >= nextFile)
      {
        nextFile = now + FILE_CHECK_SECONDS;
        const time_t before = m_guideFileTime;
        RefreshGuideFile(false);
        changed = before != m_guideFileTime;
      }
    }

    if (changed)
      GuideUpdated();

    lock.lock();
    m_serviceWake.wait_for(lock, std::chrono::seconds(5));
  }
}

PVR_ERROR CPvrClient::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingSize(true);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsChannelGroups(false);
  capabilities.SetSupportsChannelScan(false);
  capabilities.SetSupportsChannelSettings(false);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);
  capabilities.SetSupportsLastPlayedPosition(false);
  capabilities.SetSupportsRecordingPlayCount(false);
  capabilities.SetSupportsAsyncEPGTransfer(false);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetBackendName(std::string& name)
{
  name = "Direct Tuner PVR";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetBackendVersion(std::string& version)
{
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  version = m_device.firmware;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetBackendHostname(std::string& hostname)
{
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  hostname = m_device.baseUrl;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetConnectionString(std::string& connection)
{
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  connection = m_connected ? m_device.friendlyName + " " + m_device.baseUrl : "Not connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  if (!m_store.DriveSpace(total, used))
    return PVR_ERROR_SERVER_ERROR;
  total /= 1024;
  used /= 1024;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::OnSystemWake()
{
  std::lock_guard<std::mutex> lock(m_serviceMutex);
  m_refreshNow = true;
  m_serviceWake.notify_all();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetChannelsAmount(int& amount)
{
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  amount = static_cast<int>(m_channels.size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  if (radio)
    return PVR_ERROR_NO_ERROR;

  std::lock_guard<std::mutex> lock(m_deviceMutex);
  for (const dt::Channel& channel : m_channels)
  {
    kodi::addon::PVRChannel item;
    item.SetUniqueId(static_cast<unsigned int>(channel.uid));
    item.SetChannelNumber(static_cast<unsigned int>(channel.major));
    item.SetSubChannelNumber(static_cast<unsigned int>(channel.minor));
    item.SetChannelName(channel.name);
    const auto icon = m_icons.find(channel.uid);
    if (icon != m_icons.end())
      item.SetIconPath(icon->second);
    results.Add(item);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& status)
{
  dt::DeviceInfo device;
  dt::Channel channel;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    device = m_device;
  }
  status.SetAdapterName(device.friendlyName);
  if (!LookupChannel(channelUid, channel))
    return PVR_ERROR_NO_ERROR;

  std::vector<dt::TunerUse> tuners;
  if (!dt::FetchTunerStatus(device, tuners))
  {
    status.SetAdapterStatus("Can't read tuner status");
    return PVR_ERROR_NO_ERROR;
  }

  for (const dt::TunerUse& tuner : tuners)
  {
    if (tuner.channel == channel.guideNumber)
    {
      status.SetAdapterStatus(tuner.resource);
      status.SetServiceName(channel.name);
      status.SetSignal(tuner.signalStrength * 65535 / 100);
      status.SetSNR(tuner.signalQuality * 65535 / 100);
      break;
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                       kodi::addon::PVREPGTagsResultSet& results)
{
  for (const dt::GuideEvent& event : m_guide.GetEvents(channelUid, start, end))
  {
    kodi::addon::PVREPGTag tag;
    tag.SetUniqueBroadcastId(event.Uid());
    tag.SetUniqueChannelId(static_cast<unsigned int>(channelUid));
    tag.SetTitle(event.title);
    tag.SetStartTime(event.start);
    tag.SetEndTime(event.end);
    tag.SetPlot(event.plot);
    tag.SetEpisodeName(event.episodeTitle);
    tag.SetIconPath(event.icon);
    tag.SetFirstAired(event.firstAired);
    tag.SetSeriesNumber(event.season);
    tag.SetEpisodeNumber(event.episode);
    tag.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);
    tag.SetSeriesLink(event.seriesId);
    if (!event.genre.empty())
    {
      tag.SetGenreType(EPG_GENRE_USE_STRING);
      tag.SetGenreDescription(event.genre);
    }
    unsigned int flags = EPG_TAG_FLAG_UNDEFINED;
    if (event.season >= 0 || event.episode >= 0 || !event.seriesId.empty())
      flags |= EPG_TAG_FLAG_IS_SERIES;
    if (event.isNew)
      flags |= EPG_TAG_FLAG_IS_NEW;
    tag.SetFlags(flags);
    results.Add(tag);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetRecordingsAmount(bool deleted, int& amount)
{
  amount = deleted ? 0 : static_cast<int>(m_store.List().size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  if (deleted)
    return PVR_ERROR_NO_ERROR;

  for (const dt::RecordingInfo& info : m_store.List())
  {
    kodi::addon::PVRRecording item;
    item.SetRecordingId(info.id);
    item.SetTitle(info.title);
    item.SetEpisodeName(info.episodeTitle);
    item.SetSeriesNumber(info.season);
    item.SetEpisodeNumber(info.episode);
    item.SetChannelName(info.channelName);
    item.SetChannelUid(info.channelUid);
    item.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);
    item.SetRecordingTime(info.start);
    item.SetIconPath(info.icon);
    item.SetThumbnailPath(info.icon);
    item.SetFirstAired(info.firstAired);
    item.SetSizeInBytes(info.size);
    item.SetEPGEventId(info.epgUid);
    if (!info.genre.empty())
    {
      item.SetGenreType(EPG_GENRE_USE_STRING);
      item.SetGenreDescription(info.genre);
    }
    if (info.ruleId != 0)
      item.SetDirectory("/" + info.title);

    // A partial recording says so where the user will see it.
    item.SetPlot(info.note.empty() ? info.plot : "[" + info.note + "] " + info.plot);

    if (info.status == "recording")
      item.SetDuration(static_cast<int>(std::time(nullptr) - info.start));
    else
      item.SetDuration(info.duration);
    results.Add(item);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (m_scheduler->IsRecording(recording.GetRecordingId()))
  {
    NotifyUser(true, "Stop the recording before deleting it");
    return PVR_ERROR_RECORDING_RUNNING;
  }
  if (!m_store.Delete(recording.GetRecordingId()))
    return PVR_ERROR_FAILED;
  TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& size)
{
  size = std::max<int64_t>(dt::FileSize(m_store.MediaPath(recording.GetRecordingId())), 0);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  const uint64_t common = PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
                          PVR_TIMER_TYPE_SUPPORTS_START_TIME | PVR_TIMER_TYPE_SUPPORTS_END_TIME |
                          PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  types.push_back(MakeType(TIMER_ONCE_MANUAL,
                           PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE | common,
                           "One time (manual)"));
  types.push_back(MakeType(TIMER_ONCE_EPG, PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | common,
                           "One time (guide)"));

  const std::vector<kodi::addon::PVRTypeIntValue> newOnly = {{0, "All episodes"},
                                                             {1, "New episodes only"}};
  const std::vector<kodi::addon::PVRTypeIntValue> keep = {
      {0, "Keep all"}, {1, "Keep 1"}, {3, "Keep 3"}, {5, "Keep 5"}, {10, "Keep 10"}, {25, "Keep 25"}};

  kodi::addon::PVRTimerType series = MakeType(
      TIMER_SERIES_EPG,
      PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
          PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE | PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
          PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL | PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH |
          PVR_TIMER_TYPE_SUPPORTS_START_TIME | PVR_TIMER_TYPE_SUPPORTS_END_TIME |
          PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME | PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME |
          PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS | PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY |
          PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
          PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS | PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN,
      "Series (guide)");
  series.SetPreventDuplicateEpisodes(newOnly, 0);
  series.SetMaxRecordings(keep, 0);
  types.push_back(series);

  kodi::addon::PVRTimerType manualSeries =
      MakeType(TIMER_SERIES_MANUAL,
               PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_IS_REPEATING |
                   PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE | PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
                   PVR_TIMER_TYPE_SUPPORTS_FIRST_DAY | PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS | common,
               "Repeating (manual)");
  manualSeries.SetMaxRecordings(keep, 0);
  types.push_back(manualSeries);

  types.push_back(MakeType(TIMER_CHILD,
                           PVR_TIMER_TYPE_IS_READONLY | PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES |
                               PVR_TIMER_TYPE_SUPPORTS_READONLY_DELETE |
                               PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME |
                               PVR_TIMER_TYPE_SUPPORTS_END_TIME |
                               PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN,
                           "Series episode"));
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetTimersAmount(int& amount)
{
  amount = static_cast<int>(m_scheduler->GetTimers().size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  for (const dt::Timer& timer : m_scheduler->GetTimers())
  {
    kodi::addon::PVRTimer item;
    item.SetClientIndex(timer.id);
    item.SetState(StateToKodi(timer.state));
    item.SetTitle(timer.title);
    item.SetSummary(timer.note);
    item.SetClientChannelUid(timer.channelUid == 0 ? PVR_TIMER_ANY_CHANNEL : timer.channelUid);
    item.SetStartTime(timer.start);
    item.SetEndTime(timer.end);
    item.SetMarginStart(static_cast<unsigned int>(timer.marginBefore));
    item.SetMarginEnd(static_cast<unsigned int>(timer.marginAfter));

    switch (timer.kind)
    {
      case dt::TimerKind::Once:
        item.SetTimerType(timer.manual ? TIMER_ONCE_MANUAL : TIMER_ONCE_EPG);
        item.SetEPGUid(timer.epgUid);
        break;
      case dt::TimerKind::Child:
        item.SetTimerType(TIMER_CHILD);
        item.SetParentClientIndex(timer.parentId);
        item.SetEPGUid(timer.epgUid);
        break;
      case dt::TimerKind::Series:
        item.SetTimerType(timer.manual ? TIMER_SERIES_MANUAL : TIMER_SERIES_EPG);
        item.SetEPGSearchString(timer.search);
        item.SetWeekdays(timer.weekdays);
        item.SetFirstDay(timer.firstDay);
        item.SetStartAnyTime(timer.startAnyTime);
        item.SetEndAnyTime(timer.endAnyTime);
        item.SetPreventDuplicateEpisodes(timer.newOnly ? 1 : 0);
        item.SetMaxRecordings(timer.keep);
        break;
    }
    results.Add(item);
  }
  return PVR_ERROR_NO_ERROR;
}

dt::Timer CPvrClient::FromKodi(const kodi::addon::PVRTimer& source)
{
  dt::Timer timer;
  timer.id = source.GetClientIndex();
  timer.channelUid =
      source.GetClientChannelUid() == PVR_TIMER_ANY_CHANNEL ? 0 : source.GetClientChannelUid();
  timer.title = source.GetTitle();
  timer.start = source.GetStartTime();
  timer.end = source.GetEndTime();
  timer.marginBefore = static_cast<int>(source.GetMarginStart());
  timer.marginAfter = static_cast<int>(source.GetMarginEnd());
  timer.enabled = source.GetState() != PVR_TIMER_STATE_DISABLED;
  timer.epgUid = source.GetEPGUid();
  timer.search = source.GetEPGSearchString();
  timer.weekdays = source.GetWeekdays();
  timer.firstDay = source.GetFirstDay();
  timer.startAnyTime = source.GetStartAnyTime();
  timer.endAnyTime = source.GetEndAnyTime();
  timer.newOnly = source.GetPreventDuplicateEpisodes() != 0;
  timer.keep = source.GetMaxRecordings();

  switch (source.GetTimerType())
  {
    case TIMER_ONCE_MANUAL:
      timer.manual = true;
      break;
    case TIMER_SERIES_EPG:
      timer.kind = dt::TimerKind::Series;
      break;
    case TIMER_SERIES_MANUAL:
      timer.kind = dt::TimerKind::Series;
      timer.manual = true;
      break;
    case TIMER_CHILD:
      timer.kind = dt::TimerKind::Child;
      timer.parentId = source.GetParentClientIndex();
      break;
    default:
      break;
  }

  // Kodi sends a start time of 0 for "record from now".
  if (timer.start == 0)
    timer.start = std::time(nullptr);
  if (timer.weekdays == PVR_WEEKDAY_NONE)
    timer.weekdays = PVR_WEEKDAY_ALLDAYS;
  if (timer.kind == dt::TimerKind::Series && timer.search.empty())
    timer.search = timer.title;

  if (timer.epgUid != PVR_TIMER_NO_EPG_UID && timer.channelUid != 0)
    m_guide.FindEvent(timer.channelUid, timer.epgUid, timer.event);
  return timer;
}

PVR_ERROR CPvrClient::AddTimer(const kodi::addon::PVRTimer& source)
{
  dt::Timer timer = FromKodi(source);

  // Kodi's own default margins are 0. Ours apply unless a dialog set something else.
  if (source.GetMarginStart() == 0 && source.GetStartTime() != 0)
    timer.marginBefore = kodi::addon::GetSettingInt("margin_before", 1);
  if (source.GetMarginEnd() == 0)
    timer.marginAfter = kodi::addon::GetSettingInt("margin_after", 3);

  std::string error;
  if (m_scheduler->AddTimer(std::move(timer), &error) == 0)
  {
    NotifyUser(true, error);
    return PVR_ERROR_REJECTED;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::DeleteTimer(const kodi::addon::PVRTimer& timer, bool)
{
  return m_scheduler->DeleteTimer(timer.GetClientIndex()) ? PVR_ERROR_NO_ERROR : PVR_ERROR_INVALID_PARAMETERS;
}

PVR_ERROR CPvrClient::UpdateTimer(const kodi::addon::PVRTimer& source)
{
  return m_scheduler->UpdateTimer(FromKodi(source)) ? PVR_ERROR_NO_ERROR : PVR_ERROR_INVALID_PARAMETERS;
}

bool CPvrClient::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  CloseLiveStream();

  dt::Channel tuned;
  if (!LookupChannel(static_cast<int>(channel.GetUniqueId()), tuned))
    return false;

  // Live TV always plays from the ring, which needs somewhere to live even when the
  // user doesn't want timeshift. It stays small then.
  const int64_t capacity = std::max(m_timeshiftCapacity, MIN_TIMESHIFT_BYTES);
  std::error_code error;
  const std::filesystem::path folder = dt::PathFromUtf8(m_store.Folder()) / ".timeshift";
  std::filesystem::create_directories(folder, error);

  auto buffer = std::make_shared<dt::TimeshiftBuffer>(dt::PathToUtf8(folder / "live.ts"), capacity);
  if (!buffer->Open())
  {
    NotifyUser(true, "Can't write the live TV buffer in the recordings folder");
    return false;
  }

  dt::TuneResult result = dt::TuneResult::Ok;
  auto lease = m_pool.Acquire(tuned, dt::TunerUseKind::Live, result);
  if (!lease)
  {
    NotifyUser(true, tuned.guideNumber + " " + tuned.name + ": " + dt::TuneResultText(result));
    return false;
  }
  lease->Pump().AddSink(buffer);

  std::lock_guard<std::mutex> lock(m_liveMutex);
  m_liveLease = std::move(lease);
  m_live = std::move(buffer);
  m_liveChannel = tuned.uid;
  return true;
}

void CPvrClient::CloseLiveStream()
{
  std::unique_ptr<dt::TunerLease> lease;
  std::shared_ptr<dt::TimeshiftBuffer> buffer;
  {
    std::lock_guard<std::mutex> lock(m_liveMutex);
    lease = std::move(m_liveLease);
    buffer = std::move(m_live);
    m_liveChannel = 0;
  }
  if (buffer)
    buffer->Interrupt();
  if (lease && buffer)
    lease->Pump().RemoveSink(buffer);
}

int CPvrClient::ReadLiveStream(unsigned char* buffer, unsigned int size)
{
  std::shared_ptr<dt::TimeshiftBuffer> live;
  {
    std::lock_guard<std::mutex> lock(m_liveMutex);
    live = m_live;
  }
  if (!live)
    return -1;

  const int count = live->Read(buffer, size, LIVE_READ_TIMEOUT_MS);
  if (count == 0)
  {
    NotifyUser(true, "No data from the tuner");
    return -1;
  }
  return count;
}

int64_t CPvrClient::SeekLiveStream(int64_t position, int whence)
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  if (!m_live)
    return -1;
  switch (whence)
  {
    case SEEK_SET:
      return m_live->Seek(position);
    case SEEK_CUR:
      return m_live->Seek(m_live->Position() + position);
    case SEEK_END:
      return m_live->Seek(m_live->Length() + position);
    default:
      return -1;
  }
}

int64_t CPvrClient::LengthLiveStream()
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  return m_live ? m_live->Length() : -1;
}

bool CPvrClient::CanPauseStream()
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  return !m_live || m_timeshiftCapacity > 0;
}

bool CPvrClient::CanSeekStream()
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  return !m_live || m_timeshiftCapacity > 0;
}

bool CPvrClient::IsRealTimeStream()
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  return m_live && m_live->SecondsBehind() < 10;
}

PVR_ERROR CPvrClient::GetStreamTimes(kodi::addon::PVRStreamTimes& times)
{
  std::lock_guard<std::mutex> lock(m_liveMutex);
  if (!m_live)
    return PVR_ERROR_NOT_IMPLEMENTED;

  const dt::TimeshiftBuffer::Times span = m_live->GetTimes();
  times.SetStartTime(span.start);
  times.SetPTSStart(0);
  times.SetPTSBegin(static_cast<int64_t>(span.oldest - span.start) * STREAM_TIME_BASE);
  times.SetPTSEnd(static_cast<int64_t>(span.newest - span.start) * STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetStreamReadChunkSize(int& chunkSize)
{
  chunkSize = 188 * 1024;
  return PVR_ERROR_NO_ERROR;
}

bool CPvrClient::OpenRecordedStream(const kodi::addon::PVRRecording& recording)
{
  CloseRecordedStream();
  std::lock_guard<std::mutex> lock(m_playMutex);
  m_playFile = dt::OpenFileShared(m_store.MediaPath(recording.GetRecordingId()), "rb");
  m_playId = recording.GetRecordingId();
  if (!m_playFile)
    NotifyUser(true, "Can't open the recording file");
  return m_playFile != nullptr;
}

void CPvrClient::CloseRecordedStream()
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  if (m_playFile)
    std::fclose(m_playFile);
  m_playFile = nullptr;
  m_playId.clear();
}

int CPvrClient::ReadRecordedStream(unsigned char* buffer, unsigned int size)
{
  // A recording in progress keeps growing, so reaching its end means waiting.
  for (int attempt = 0; attempt < 100; ++attempt)
  {
    {
      std::lock_guard<std::mutex> lock(m_playMutex);
      if (!m_playFile)
        return -1;
      const size_t count = std::fread(buffer, 1, size, m_playFile);
      if (count > 0)
        return static_cast<int>(count);
      std::clearerr(m_playFile);
      if (!m_scheduler->IsRecording(m_playId))
        return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return 0;
}

int64_t CPvrClient::SeekRecordedStream(int64_t position, int whence)
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  if (!m_playFile || Seek64(m_playFile, position, whence) != 0)
    return -1;
  return Tell64(m_playFile);
}

int64_t CPvrClient::LengthRecordedStream()
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  return m_playId.empty() ? -1 : dt::FileSize(m_store.MediaPath(m_playId));
}

void CPvrClient::NotifyUser(bool isError, const std::string& text)
{
  kodi::Log(isError ? ADDON_LOG_ERROR : ADDON_LOG_INFO, "%s", text.c_str());
  kodi::QueueNotification(isError ? QUEUE_ERROR : QUEUE_INFO, "Direct Tuner PVR", text);
}

void CPvrClient::TimersChanged()
{
  TriggerTimerUpdate();
}

void CPvrClient::RecordingsChanged()
{
  TriggerRecordingUpdate();
}

bool CPvrClient::LookupChannel(int uid, dt::Channel& out)
{
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  for (const dt::Channel& channel : m_channels)
  {
    if (channel.uid == uid)
    {
      out = channel;
      return true;
    }
  }
  return false;
}
