/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RecordingStore.h"

#include "Common.h"
#include "Json.h"

#include <algorithm>
#include <filesystem>

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
  return PathToUtf8(PathFromUtf8(Folder()) / PathFromUtf8(id + ".ts"));
}

std::string RecordingStore::SidecarPath(const std::string& id) const
{
  return PathToUtf8(PathFromUtf8(Folder()) / PathFromUtf8(id + ".json"));
}

std::vector<RecordingInfo> RecordingStore::List() const
{
  std::vector<RecordingInfo> recordings;
  const std::filesystem::path root = PathFromUtf8(Folder());
  std::error_code error;
  if (root.empty() || !std::filesystem::is_directory(root, error))
    return recordings;

  auto it = std::filesystem::recursive_directory_iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const auto end = std::filesystem::recursive_directory_iterator();
  while (!error && it != end)
  {
    const std::filesystem::path path = it->path();
    if (it->is_regular_file(error) && path.extension() == ".json")
    {
      std::string text;
      Json json;
      std::filesystem::path relative = path.lexically_relative(root);
      relative.replace_extension();
      std::filesystem::path media = path;
      media.replace_extension(".ts");
      if (ReadWholeFile(PathToUtf8(path), text) && Json::Parse(text, json) && json.IsObject())
      {
        RecordingInfo info = FromJson(json);
        info.id = PathToUtf8(relative.generic_u8string());
        info.size = FileSize(PathToUtf8(media));
        if (info.size >= 0)
          recordings.push_back(std::move(info));
      }
    }
    it.increment(error);
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
  std::error_code error;
  const std::filesystem::path media = PathFromUtf8(MediaPath(id));
  std::filesystem::remove(media, error);
  const bool removed = !error;
  std::filesystem::remove(PathFromUtf8(SidecarPath(id)), error);

  // Drop the show's folder when its last recording goes.
  const std::filesystem::path parent = media.parent_path();
  if (parent != PathFromUtf8(Folder()) && std::filesystem::is_empty(parent, error) && !error)
    std::filesystem::remove(parent, error);
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

  const std::filesystem::path root = PathFromUtf8(Folder());
  std::error_code error;
  std::filesystem::create_directories(root / PathFromUtf8(folderName), error);
  if (error)
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
  std::error_code error;
  const std::filesystem::space_info space = std::filesystem::space(PathFromUtf8(Folder()), error);
  if (error)
    return false;
  total = space.capacity;
  used = space.capacity - space.available;
  return true;
}

} // namespace dt
