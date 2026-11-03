/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PvrClient.h"

#include "../core/Common.h"
#include "../core/Json.h"
#include "../core/Net.h"
#include "../core/RemoteBackend.h"
#include "../core/Wire.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/addon-instance/inputstream/TimingConstants.h>

#include <algorithm>
#include <cstdlib>


namespace
{

constexpr unsigned int TIMER_ONCE_MANUAL = 1;
constexpr unsigned int TIMER_ONCE_EPG = 2;
constexpr unsigned int TIMER_SERIES_EPG = 3;
constexpr unsigned int TIMER_SERIES_MANUAL = 4;
constexpr unsigned int TIMER_CHILD = 5;

constexpr int LIVE_READ_TIMEOUT_MS = 20000;
constexpr int64_t MIN_TIMESHIFT_BYTES = 256LL * 1024 * 1024;
constexpr int RECORDER_PORT = 8866;

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
  return dt::JoinPath(dt::JoinPath(home, "Videos"), "Kodi Recordings");
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

time_t ModifiedWithKodi(const std::string& path)
{
  // Kodi's file layer can't open \\server\share paths, so plain paths are checked directly.
  if (path.find("://") == std::string::npos)
    return dt::ModifiedTime(path);
  kodi::vfs::FileStatus status;
  return kodi::vfs::StatFile(path, status) ? status.GetModificationTime() : 0;
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

  const std::string recorder = FindRecorder();
  m_remote = !recorder.empty();
  if (m_remote)
  {
    kodi::Log(ADDON_LOG_INFO, "Using the recorder at %s", recorder.c_str());
    m_backend = std::make_unique<dt::RemoteBackend>(*this, recorder);
    // Live TV's pause buffer stays on this computer.
    m_timeshiftFolder = kodi::vfs::TranslateSpecialProtocol(kodi::addon::GetUserPath("timeshift"));
  }
  else
  {
    dt::BackendIo io;
    io.fetch = FetchWithKodi;
    io.modified = ModifiedWithKodi;
    m_backend = std::make_unique<dt::Backend>(
        *this, m_settings.backend, io,
        dt::JoinPath(kodi::vfs::TranslateSpecialProtocol(kodi::addon::GetUserPath()), "timers.json"));
    m_timeshiftFolder = dt::JoinPath(m_settings.backend.recordingsFolder, ".timeshift");
  }

  m_backend->Start();
  if (m_remote && m_backend->Connected())
    MoveTimersToRecorder();
}

CPvrClient::~CPvrClient()
{
  CloseLiveStream();
  CloseRecordedStream();
  m_backend->Stop();
  m_backend.reset();
  dt::SetLogSink(nullptr);
}

void CPvrClient::LoadSettings()
{
  m_settings.recorderMode = kodi::addon::GetSettingInt("recorder_mode", 0);
  m_settings.recorderAddress = dt::Trim(kodi::addon::GetSettingString("recorder_address"));
  m_settings.backend.manualHost = dt::Trim(kodi::addon::GetSettingString("manual_host"));
  m_settings.backend.guideFile = dt::Trim(kodi::addon::GetSettingString("guide_file"));
  m_settings.backend.recordingsFolder = dt::Trim(kodi::addon::GetSettingString("recordings_folder"));
  m_settings.backend.hideDrm = kodi::addon::GetSettingBoolean("hide_drm", true);
  m_settings.marginBefore = kodi::addon::GetSettingInt("margin_before", 1);
  m_settings.marginAfter = kodi::addon::GetSettingInt("margin_after", 3);
  m_settings.timeshiftGb = kodi::addon::GetSettingInt("timeshift_gb", 4);

  std::string& folder = m_settings.backend.recordingsFolder;
  if (folder.empty())
    folder = DefaultRecordingsFolder();
  folder = kodi::vfs::TranslateSpecialProtocol(folder);
#ifdef _WIN32
  // Kodi's folder browser gives smb:// paths, and the core writes through Windows.
  if (folder.rfind("smb://", 0) == 0)
  {
    folder = "\\\\" + folder.substr(6);
    std::replace(folder.begin(), folder.end(), '/', '\\');
  }
#endif
  m_timeshiftCapacity = static_cast<int64_t>(m_settings.timeshiftGb) * 1024 * 1024 * 1024;
}

std::string CPvrClient::FindRecorder()
{
  if (m_settings.recorderMode == 1)
    return {};

  if (m_settings.recorderMode == 2)
  {
    std::string url = m_settings.recorderAddress;
    if (url.empty())
      return {};
    if (url.find("://") == std::string::npos)
      url = "http://" + url;
    if (url.find(':', 7) == std::string::npos)
      url += ":" + std::to_string(RECORDER_PORT);
    // Use it even if it's down right now: the add-on keeps trying, and recording here instead
    // would split the timers between two places.
    if (!dt::RemoteBackend::Probe(url))
      kodi::Log(ADDON_LOG_WARNING, "The recorder at %s isn't answering yet", url.c_str());
    return url;
  }

  for (const std::string& url : dt::DiscoverRecorders(1500))
    if (dt::RemoteBackend::Probe(url))
      return url;
  return {};
}

void CPvrClient::MoveTimersToRecorder()
{
  const std::string path =
      dt::JoinPath(kodi::vfs::TranslateSpecialProtocol(kodi::addon::GetUserPath()), "timers.json");
  std::string text;
  dt::Json root;
  if (!dt::ReadWholeFile(path, text) || !dt::Json::Parse(text, root))
    return;

  const time_t now = std::time(nullptr);
  int moved = 0;
  for (const dt::Json& item : root["timers"].Items())
  {
    dt::Timer timer = dt::TimerFromJson(item);
    // Series rules bring their own episodes back; past one-off timers have nothing left to do.
    if (timer.kind == dt::TimerKind::Child ||
        (timer.kind == dt::TimerKind::Once && timer.WindowEnd() <= now))
      continue;
    timer.id = 0;
    timer.state = dt::TimerState::Scheduled;
    std::string error;
    if (m_backend->AddTimer(timer, error) != 0)
      ++moved;
    else
      kodi::Log(ADDON_LOG_WARNING, "Couldn't move the timer %s: %s", timer.title.c_str(), error.c_str());
  }

  dt::RenamePath(path, path + ".moved");
  if (moved > 0)
    Notify(false, dt::Format("Moved %d timer(s) to the recorder", moved));
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
  name = m_backend->Name();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetBackendVersion(std::string& version)
{
  version = m_backend->Version();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetBackendHostname(std::string& hostname)
{
  hostname = m_backend->Hostname();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetConnectionString(std::string& connection)
{
  connection = m_backend->ConnectionText();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  if (!m_backend->DriveSpace(total, used))
    return PVR_ERROR_SERVER_ERROR;
  total /= 1024;
  used /= 1024;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::OnSystemWake()
{
  m_backend->Refresh();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetChannelsAmount(int& amount)
{
  amount = static_cast<int>(m_backend->Channels().size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  if (radio)
    return PVR_ERROR_NO_ERROR;

  const std::map<int, std::string> icons = m_backend->ChannelIcons();
  for (const dt::Channel& channel : m_backend->Channels())
  {
    kodi::addon::PVRChannel item;
    item.SetUniqueId(static_cast<unsigned int>(channel.uid));
    item.SetChannelNumber(static_cast<unsigned int>(channel.major));
    item.SetSubChannelNumber(static_cast<unsigned int>(channel.minor));
    item.SetChannelName(channel.name);
    const auto icon = icons.find(channel.uid);
    if (icon != icons.end())
      item.SetIconPath(icon->second);
    results.Add(item);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& status)
{
  dt::SignalInfo signal;
  if (!m_backend->Signal(channelUid, signal))
    return PVR_ERROR_NO_ERROR;

  dt::Channel channel;
  status.SetAdapterName(signal.adapter);
  status.SetAdapterStatus(signal.status);
  if (m_backend->LookupChannel(channelUid, channel))
    status.SetServiceName(channel.name);
  status.SetSignal(signal.strength * 65535 / 100);
  status.SetSNR(signal.quality * 65535 / 100);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetEPGForChannel(int channelUid, time_t start, time_t end,
                                       kodi::addon::PVREPGTagsResultSet& results)
{
  for (const dt::GuideEvent& event : m_backend->GetEvents(channelUid, start, end))
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
  amount = deleted ? 0 : static_cast<int>(m_backend->GetRecordings().size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  if (deleted)
    return PVR_ERROR_NO_ERROR;

  for (const dt::RecordingInfo& info : m_backend->GetRecordings())
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
  std::string error;
  if (!m_backend->DeleteRecording(recording.GetRecordingId(), error))
  {
    Notify(true, error);
    return PVR_ERROR_FAILED;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& size)
{
  size = m_backend->RecordingSize(recording.GetRecordingId());
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
  amount = static_cast<int>(m_backend->GetTimers().size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  for (const dt::Timer& timer : m_backend->GetTimers())
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
    m_backend->FindEvent(timer.channelUid, timer.epgUid, timer.event);
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
  if (m_backend->AddTimer(std::move(timer), error) == 0)
  {
    Notify(true, error);
    return PVR_ERROR_REJECTED;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPvrClient::DeleteTimer(const kodi::addon::PVRTimer& timer, bool)
{
  return m_backend->DeleteTimer(timer.GetClientIndex()) ? PVR_ERROR_NO_ERROR : PVR_ERROR_INVALID_PARAMETERS;
}

PVR_ERROR CPvrClient::UpdateTimer(const kodi::addon::PVRTimer& source)
{
  return m_backend->UpdateTimer(FromKodi(source)) ? PVR_ERROR_NO_ERROR : PVR_ERROR_INVALID_PARAMETERS;
}

bool CPvrClient::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  CloseLiveStream();
  const int uid = static_cast<int>(channel.GetUniqueId());

  // Live TV always plays from the ring, which needs somewhere to live even when the
  // user doesn't want timeshift. It stays small then.
  const int64_t capacity = std::max(m_timeshiftCapacity, MIN_TIMESHIFT_BYTES);
  dt::CreateDirectories(m_timeshiftFolder);

  auto buffer = std::make_shared<dt::TimeshiftBuffer>(dt::JoinPath(m_timeshiftFolder, "live.ts"), capacity);
  if (!buffer->Open())
  {
    Notify(true, "Can't write the live TV buffer in " + m_timeshiftFolder);
    return false;
  }

  std::string error;
  auto handle = m_backend->OpenLive(uid, buffer, error);
  if (!handle)
  {
    Notify(true, error);
    return false;
  }

  std::lock_guard<std::mutex> lock(m_liveMutex);
  m_liveHandle = std::move(handle);
  m_live = std::move(buffer);
  m_liveChannel = uid;
  return true;
}

void CPvrClient::CloseLiveStream()
{
  std::unique_ptr<dt::LiveHandle> handle;
  std::shared_ptr<dt::TimeshiftBuffer> buffer;
  {
    std::lock_guard<std::mutex> lock(m_liveMutex);
    handle = std::move(m_liveHandle);
    buffer = std::move(m_live);
    m_liveChannel = 0;
  }
  if (buffer)
    buffer->Interrupt();
  handle.reset();
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
    Notify(true, "No data from the tuner");
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
  auto reader = m_backend->OpenRecording(recording.GetRecordingId());
  if (!reader)
  {
    Notify(true, "Can't open the recording file");
    return false;
  }
  std::lock_guard<std::mutex> lock(m_playMutex);
  m_play = std::move(reader);
  return true;
}

void CPvrClient::CloseRecordedStream()
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  m_play.reset();
}

int CPvrClient::ReadRecordedStream(unsigned char* buffer, unsigned int size)
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  return m_play ? m_play->Read(buffer, size) : -1;
}

int64_t CPvrClient::SeekRecordedStream(int64_t position, int whence)
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  return m_play ? m_play->Seek(position, whence) : -1;
}

int64_t CPvrClient::LengthRecordedStream()
{
  std::lock_guard<std::mutex> lock(m_playMutex);
  return m_play ? m_play->Length() : -1;
}

void CPvrClient::Notify(bool isError, const std::string& text)
{
  kodi::Log(isError ? ADDON_LOG_ERROR : ADDON_LOG_INFO, "%s", text.c_str());
  kodi::QueueNotification(isError ? QUEUE_ERROR : QUEUE_INFO, "Direct Tuner PVR", text);
}

void CPvrClient::ConnectionChanged(bool connected, const std::string& text)
{
  if (connected)
    ConnectionStateChange(text, PVR_CONNECTION_STATE_CONNECTED, "");
  else
    ConnectionStateChange(text, PVR_CONNECTION_STATE_SERVER_UNREACHABLE, text);
}

void CPvrClient::ChannelsChanged()
{
  TriggerChannelUpdate();
}

void CPvrClient::GuideChanged()
{
  for (const dt::Channel& channel : m_backend->Channels())
    TriggerEpgUpdate(static_cast<unsigned int>(channel.uid));
}

void CPvrClient::TimersChanged()
{
  TriggerTimerUpdate();
}

void CPvrClient::RecordingsChanged()
{
  TriggerRecordingUpdate();
}

void CPvrClient::LiveStopped(int channelUid, const std::string& reason)
{
  {
    std::lock_guard<std::mutex> lock(m_liveMutex);
    if (!m_live || m_liveChannel != channelUid)
      return;
    m_live->MarkEnded();
  }
  Notify(true, reason);
}
