/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dt
{

enum class LogLevel
{
  Debug,
  Info,
  Warning,
  Error
};

using LogSink = std::function<void(LogLevel, const std::string&)>;

// The core has no Kodi types: the add-on or a test program installs the log sink.
void SetLogSink(LogSink sink);
void Log(LogLevel level, const char* format, ...);

std::string Format(const char* format, ...);
std::string ToLower(std::string text);
std::string Trim(const std::string& text);

// Paths are UTF-8 strings throughout, as they come from Kodi.
std::filesystem::path PathFromUtf8(const std::string& path);
std::string PathToUtf8(const std::filesystem::path& path);
std::FILE* OpenFileShared(const std::string& path, const char* mode);
int64_t FileSize(const std::string& path);
bool ReadWholeFile(const std::string& path, std::string& out);
// Writes through a temporary file, so a crash can't leave half a file behind.
bool WriteFileAtomic(const std::string& path, const std::string& text);

// A file name made safe for every file system we might record to.
std::string SafeFileName(const std::string& name);

} // namespace dt
