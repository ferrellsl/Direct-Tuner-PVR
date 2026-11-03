/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RecordingStore.h"

#include "Common.h"
#include "Json.h"

#include <algorithm>

namespace dt
{
namespace
{

Json ToJson(const RecordingInfo& info)
{
  Json json = Json::Object();
  json.Set("title", info.title);
  json.Set("episodeTitle", info.episodeTitle);
  json.Set("plot", info.plot);
  json.Set("icon", info.icon);
  json.Set("channelName", info.channelName);
  json.Set("genre", info.genre);
  json.Set("firstAired", info.firstAired);
  json.Set("episodeKey", info.episodeKey);
  json.Set("status", info.status);
  json.Set("note", info.note);
  json.Set("channelUid", info.channelUid);
  json.Set("start", static_cast<long long>(info.start));
  json.Set("duration", info.duration);
  json.Set("season", info.season);
  json.Set("episode", info.episode);
  json.Set("epgUid", info.epgUid);
  json.Set("ruleId", info.ruleId);
  return json;
}

RecordingInfo FromJson(const Json& json)
{
  RecordingInfo info;
  info.title = json["title"].AsString();
  info.episodeTitle = json["episodeTitle"].AsString();
  info.plot = json["plot"].AsString();
  info.icon = json["icon"].AsString();
  info.channelName = json["channelName"].AsString();
  info.genre = json["genre"].AsString();
  info.firstAired = json["firstAired"].AsString();
  info.episodeKey = json["episodeKey"].AsString();
  info.status = json["status"].AsString("complete");
  info.note = json["note"].AsString();
  info.channelUid = json["channelUid"].AsInt();
  info.start = static_cast<time_t>(json["start"].AsInt64());
  info.duration = json["duration"].AsInt();
  info.season = json["season"].AsInt(-1);
  info.episode = json["episode"].AsInt(-1);
  info.epgUid = static_cast<unsigned int>(json["epgUid"].AsNumber());
  info.ruleId = static_cast<unsigned int>(json["ruleId"].AsNumber());
  return info;
}

} // namespace

void RecordingStore::SetFolder(const std::string& folder)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_folder = folder;
}

std::string RecordingStore::Folder() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_folder;
}

std::string RecordingStore::MediaPath(const std::string& id) const
{
  return JoinPath(Folder(), id + ".ts");
}

std::string RecordingStore::SidecarPath(const std::string& id) const
{
  return JoinPath(Folder(), id + ".json");
}

std::vector<RecordingInfo> RecordingStore::List() const
{
  std::vector<RecordingInfo> recordings;
  const std::string root = Folder();
  if (root.empty() || !IsDirectory(root))
    return recordings;

  for (const std::string& relative : ListFiles(root, ".json"))
  {
    std::string text;
    Json json;
    if (ReadWholeFile(JoinPath(root, relative), text) && Json::Parse(text, json) && json.IsObject())
    {
      RecordingInfo info = FromJson(json);
      info.id = relative.substr(0, relative.size() - 5);
      info.size = FileSize(MediaPath(info.id));
      if (info.size >= 0)
        recordings.push_back(std::move(info));
    }
  }

  std::sort(recordings.begin(), recordings.end(),
            [](const RecordingInfo& a, const RecordingInfo& b) { return a.start > b.start; });
  return recordings;
}

bool RecordingStore::Find(const std::string& id, RecordingInfo& out) const
{
  std::string text;
  Json json;
  if (!ReadWholeFile(SidecarPath(id), text) || !Json::Parse(text, json) || !json.IsObject())
    return false;
  out = FromJson(json);
  out.id = id;
  out.size = FileSize(MediaPath(id));
  return true;
}

bool RecordingStore::Delete(const std::string& id)
{
  const std::string media = MediaPath(id);
  const bool removed = RemovePath(media);
  RemovePath(SidecarPath(id));

  // Drop the show's folder when its last recording goes.
  const std::string parent = ParentPath(media);
  if (id.find('/') != std::string::npos && IsEmptyDirectory(parent))
    RemovePath(parent);
  return removed;
}

bool RecordingStore::Create(RecordingInfo& info)
{
  std::tm parts{};
#ifdef _WIN32
  localtime_s(&parts, &info.start);
#else
  localtime_r(&info.start, &parts);
#endif
  char when[32];
  std::strftime(when, sizeof(when), "%Y-%m-%d %H%M", &parts);

  const std::string folderName = SafeFileName(info.title);
  std::string name = folderName;
  if (info.season >= 0 && info.episode >= 0)
    name += Format(" S%02dE%02d", info.season, info.episode);
  name += std::string(" ") + when;

  if (!CreateDirectories(JoinPath(Folder(), folderName)))
    return false;

  std::string candidate = name;
  for (int i = 2; FileSize(MediaPath(folderName + "/" + candidate)) >= 0; ++i)
    candidate = name + Format(" (%d)", i);
  info.id = folderName + "/" + candidate;
  return Save(info);
}

bool RecordingStore::Save(const RecordingInfo& info)
{
  return WriteFileAtomic(SidecarPath(info.id), ToJson(info).Dump());
}

int RecordingStore::RecoverInterrupted()
{
  int count = 0;
  for (RecordingInfo info : List())
  {
    if (info.status != "recording")
      continue;
    info.status = info.size > 0 ? "partial" : "failed";
    info.note = "Cut off when Kodi stopped unexpectedly";
    Save(info);
    ++count;
  }
  return count;
}

int RecordingStore::Prune(unsigned int ruleId, int keep)
{
  if (keep <= 0 || ruleId == 0)
    return 0;

  int kept = 0;
  int deleted = 0;
  // List() is newest first.
  for (const RecordingInfo& info : List())
  {
    if (info.ruleId != ruleId || info.status == "recording")
      continue;
    if (++kept > keep && Delete(info.id))
      ++deleted;
  }
  return deleted;
}

bool RecordingStore::DriveSpace(uint64_t& total, uint64_t& used) const
{
  uint64_t available = 0;
  if (!DiskSpace(Folder(), total, available))
    return false;
  used = total - available;
  return true;
}

} // namespace dt
