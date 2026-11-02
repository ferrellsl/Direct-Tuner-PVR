/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Common.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <mutex>

#ifdef _WIN32
#include <share.h>
#endif

namespace dt
{
namespace
{

std::mutex g_logMutex;
LogSink g_logSink;

std::string FormatV(const char* format, va_list args)
{
  va_list copy;
  va_copy(copy, args);
  const int size = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (size <= 0)
    return {};
  std::string text(static_cast<size_t>(size) + 1, '\0');
  std::vsnprintf(text.data(), text.size(), format, args);
  text.resize(static_cast<size_t>(size));
  return text;
}

} // namespace

void SetLogSink(LogSink sink)
{
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_logSink = std::move(sink);
}

void Log(LogLevel level, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  const std::string text = FormatV(format, args);
  va_end(args);

  std::lock_guard<std::mutex> lock(g_logMutex);
  if (g_logSink)
    g_logSink(level, text);
}

std::string Format(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  std::string text = FormatV(format, args);
  va_end(args);
  return text;
}

std::string ToLower(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::string Trim(const std::string& text)
{
  size_t first = 0;
  size_t last = text.size();
  while (first < last && std::isspace(static_cast<unsigned char>(text[first])))
    ++first;
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
    --last;
  return text.substr(first, last - first);
}

std::filesystem::path PathFromUtf8(const std::string& path)
{
  return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::FILE* OpenFileShared(const std::string& path, const char* mode)
{
#ifdef _WIN32
  const std::wstring widePath = PathFromUtf8(path).wstring();
  const std::wstring wideMode(mode, mode + std::char_traits<char>::length(mode));
  return _wfsopen(widePath.c_str(), wideMode.c_str(), _SH_DENYNO);
#else
  return std::fopen(path.c_str(), mode);
#endif
}

int64_t FileSize(const std::string& path)
{
  std::error_code error;
  const auto size = std::filesystem::file_size(PathFromUtf8(path), error);
  return error ? -1 : static_cast<int64_t>(size);
}

bool ReadWholeFile(const std::string& path, std::string& out)
{
  std::FILE* file = OpenFileShared(path, "rb");
  if (!file)
    return false;

  out.clear();
  char buffer[65536];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    out.append(buffer, count);
  const bool ok = std::ferror(file) == 0;
  std::fclose(file);
  return ok;
}

bool WriteFileAtomic(const std::string& path, const std::string& text)
{
  // On a fresh install Kodi hasn't created the add-on's data folder yet.
  std::error_code folderError;
  std::filesystem::create_directories(PathFromUtf8(path).parent_path(), folderError);

  const std::string temporary = path + ".tmp";
  std::FILE* file = OpenFileShared(temporary, "wb");
  if (!file)
    return false;

  const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  const bool closed = std::fclose(file) == 0;
  std::error_code error;
  if (written && closed)
    std::filesystem::rename(PathFromUtf8(temporary), PathFromUtf8(path), error);
  else
    error = std::make_error_code(std::errc::io_error);
  if (error)
  {
    std::filesystem::remove(PathFromUtf8(temporary), error);
    return false;
  }
  return true;
}

std::string SafeFileName(const std::string& name)
{
  std::string safe;
  safe.reserve(name.size());
  for (const char c : name)
  {
    const auto u = static_cast<unsigned char>(c);
    if (u < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos)
      safe.push_back('_');
    else
      safe.push_back(c);
  }
  safe = Trim(safe);
  while (!safe.empty() && safe.back() == '.')
    safe.pop_back();
  if (safe.size() > 120)
    safe.resize(120);
  return safe.empty() ? "Recording" : safe;
}

} // namespace dt
