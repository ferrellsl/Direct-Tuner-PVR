/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// Tests of the core with no Kodi and no tuner. A real guide file is optional: pass its path.

#include "../src/core/Common.h"
#include "../src/core/Device.h"
#include "../src/core/Guide.h"
#include "../src/core/Json.h"
#include "../src/core/Pump.h"
#include "../src/core/RecordingStore.h"
#include "../src/core/Scheduler.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace dt;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(condition) \
  do \
  { \
    ++g_checks; \
    if (!(condition)) \
    { \
      ++g_failed; \
      std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    } \
  } while (0)

static Channel MakeChannel(const std::string& number)
{
  Channel channel;
  channel.guideNumber = number;
  channel.uid = ChannelUid(number, &channel.major, &channel.minor);
  channel.url = "http://x/" + number;
  return channel;
}

static void TestJson()
{
  Json json;
  CHECK(Json::Parse(R"({"a":[1,2,{"b":"x\nyé"}],"c":true,"d":null,"e":1793651400})", json));
  CHECK(json["a"].Items().size() == 3);
  CHECK(json["a"].Items()[2]["b"].AsString() == "x\ny\xC3\xA9");
  CHECK(json["c"].AsBool());
  CHECK(json["d"].IsNull());
  CHECK(json["e"].AsInt64() == 1793651400LL);
  Json again;
  CHECK(Json::Parse(json.Dump(), again));
  CHECK(again["a"].Items()[2]["b"].AsString() == json["a"].Items()[2]["b"].AsString());
  CHECK(!Json::Parse("{", json));
}

static void TestLineup()
{
  CHECK(ChannelUid("103.1") == 1030001);
  CHECK(ChannelUid("702") == 7020000);
  CHECK(ChannelUid("garbage") == 0);

  std::vector<Channel> channels;
  CHECK(ParseLineup(R"([
    {"GuideNumber":"3.1","GuideName":"KTVK","VideoCodec":"MPEG2","AudioCodec":"AC3","URL":"http://a"},
    {"GuideNumber":"105.1","GuideName":"KPHO","DRM":1,"URL":"http://b"},
    {"GuideNumber":"7.1","GuideName":"NoUrl"}])", true, channels));
  CHECK(channels.size() == 1);
  CHECK(ParseLineup(R"([{"GuideNumber":"105.1","GuideName":"KPHO","DRM":1,"URL":"http://b"}])", false, channels));
  CHECK(channels.size() == 1 && channels[0].drm);
}

static void TestXmltv()
{
  const std::string xml = R"(<?xml version="1.0" encoding="ISO-8859-1"?>
<!DOCTYPE tv SYSTEM "xmltv.dtd">
<tv>
  <channel id="I1.labs"><display-name>3.1 KTVKDT</display-name><display-name>3.1</display-name></channel>
  <channel id="I2.labs"><display-name>KTVK-HD</display-name></channel>
  <channel id="10.1"><display-name>Fox</display-name></channel>
  <programme start="20261102120000 -0700" stop="20261102130000 -0700" channel="I1.labs">
    <title lang="en">News &amp; Views *</title>
    <desc lang="en">Caf)" "\xE9" R"( talk</desc>
    <category lang="en">News</category>
    <episode-num system="dd_progid">SH03338709.0000</episode-num>
    <new />
  </programme>
  <programme start="20261102130000 -0700" stop="20261102133000 -0700" channel="10.1">
    <title>Show</title>
    <sub-title>Pilot</sub-title>
    <episode-num system="xmltv_ns">3.35.0/0</episode-num>
    <episode-num system="onscreen">S04E36</episode-num>
  </programme>
</tv>)";

  const std::vector<Channel> lineup = {MakeChannel("3.1"), MakeChannel("103.1"), MakeChannel("10.1"),
                                       MakeChannel("9.9")};
  ChannelEvents events;
  XmltvStats stats;
  CHECK(ParseXmltv(xml, lineup, events, &stats));
  CHECK(stats.channelsMatched == 3); // 3.1, 103.1 through 3.1, and 10.1
  const auto& first = events[ChannelUid("3.1")];
  CHECK(first.size() == 1);
  CHECK(first[0].title == "News & Views");
  CHECK(first[0].plot == "Caf\xC3\xA9 talk");
  CHECK(first[0].isNew);
  CHECK(first[0].genre == "News");
  CHECK(first[0].start == 1793646000); // 2026-11-02 19:00:00 UTC
  CHECK(events[ChannelUid("103.1")].size() == 1);
  const auto& show = events[ChannelUid("10.1")];
  CHECK(show[0].season == 4 && show[0].episode == 36);
  CHECK(show[0].episodeTitle == "Pilot");
  CHECK(events.count(ChannelUid("9.9")) == 0);
}

static void TestGuideMerge()
{
  GuideEvent a;
  a.start = 1000;
  a.end = 2000;
  a.title = "File";
  GuideEvent b = a;
  b.title = "Device early";
  GuideEvent c = a;
  c.start = 2000;
  c.end = 3000;
  c.title = "Device late";

  GuideStore store;
  store.SetDeviceGuide({{1, {b, c}}, {2, {b}}});
  store.SetXmltv({{1, {a}}});
  const auto events = store.GetEvents(1, 0, 5000);
  CHECK(events.size() == 2);
  CHECK(events[0].title == "File");
  CHECK(events[1].title == "Device late");
  CHECK(store.GetEvents(2, 0, 5000)[0].title == "Device early");
}

static GuideEvent MakeEvent(time_t start, const char* title, int season, int episode, bool isNew)
{
  GuideEvent event;
  event.start = start;
  event.end = start + 1800;
  event.title = title;
  event.season = season;
  event.episode = episode;
  event.isNew = isNew;
  return event;
}

static void TestSeries()
{
  const time_t now = 1793600000;
  ChannelEvents guide;
  guide[30001] = {MakeEvent(now + 3600, "Jeopardy!", 42, 36, true),
                  MakeEvent(now + 7200, "Jeopardy!", 42, 35, false),
                  MakeEvent(now + 90000, "Jeopardy!", 42, 36, true), // repeat of the first
                  MakeEvent(now + 3600 * 3, "Other", 1, 1, true),
                  MakeEvent(now - 90000, "Jeopardy!", 42, 34, true)}; // past
  guide[50001] = {MakeEvent(now + 3600 * 5, "jeopardy!", 42, 37, true)};

  Timer rule;
  rule.id = 7;
  rule.kind = TimerKind::Series;
  rule.title = "Jeopardy!";
  rule.channelUid = 30001;

  auto children = BuildChildren(rule, guide, {}, now);
  CHECK(children.size() == 3);

  rule.newOnly = true;
  children = BuildChildren(rule, guide, {}, now);
  CHECK(children.size() == 1);
  CHECK(children[0].event.episode == 36);
  CHECK(children[0].parentId == 7);

  children = BuildChildren(rule, guide, {"jeopardy!|s42e36"}, now);
  CHECK(children.empty());

  rule.channelUid = 0;
  children = BuildChildren(rule, guide, {}, now);
  CHECK(children.size() == 2);

  rule.newOnly = false;
  rule.channelUid = 30001;
  rule.skipped.insert(Format("%d:%lld", 30001, static_cast<long long>(now + 7200)));
  children = BuildChildren(rule, guide, {}, now);
  CHECK(children.size() == 2);

  Timer manual;
  manual.id = 8;
  manual.kind = TimerKind::Series;
  manual.manual = true;
  manual.channelUid = 30001;
  manual.start = now + 600;
  manual.end = now + 2400;
  manual.title = "Clock";
  CHECK(BuildChildren(manual, guide, {}, now).size() == 7);
  manual.weekdays = 0;
  CHECK(BuildChildren(manual, guide, {}, now).empty());
}

static Timer MakeTimer(unsigned int id, int channel, time_t start, time_t end)
{
  Timer timer;
  timer.id = id;
  timer.channelUid = channel;
  timer.start = start;
  timer.end = end;
  return timer;
}

static void TestConflicts()
{
  std::vector<Timer> timers = {MakeTimer(1, 10, 100, 200), MakeTimer(2, 20, 100, 200),
                               MakeTimer(3, 30, 150, 250), MakeTimer(4, 10, 150, 250),
                               MakeTimer(5, 40, 200, 300)};
  MarkConflicts(timers, 2);
  CHECK(timers[0].state == TimerState::Scheduled);
  CHECK(timers[1].state == TimerState::Scheduled);
  CHECK(timers[2].state == TimerState::Conflict); // third channel at once
  CHECK(timers[3].state == TimerState::Scheduled); // shares channel 10
  CHECK(timers[4].state == TimerState::Scheduled); // the others ended, and 4 leaves one free

  MarkConflicts(timers, 4);
  CHECK(timers[2].state == TimerState::Scheduled);

  // A disabled timer takes no tuner.
  timers[0].enabled = false;
  timers[1].enabled = false;
  MarkConflicts(timers, 1);
  CHECK(timers[2].state == TimerState::Scheduled);
  CHECK(timers[3].state == TimerState::Conflict);
}

static void TestTimeshift(const std::string& folder)
{
  const std::string path = folder + "/ring.ts";
  const int64_t capacity = 64 * 1024 * 1024 / 188 * 188;
  TimeshiftBuffer buffer(path, capacity);
  CHECK(buffer.Open());

  std::vector<uint8_t> chunk(188 * 1000);
  uint32_t counter = 0;
  const auto fill = [&](int chunks) {
    for (int i = 0; i < chunks; ++i)
    {
      for (size_t p = 0; p < chunk.size(); p += 188)
      {
        chunk[p] = 0x47;
        std::memcpy(&chunk[p + 4], &counter, 4);
        ++counter;
      }
      CHECK(buffer.OnData(chunk.data(), chunk.size()));
    }
  };

  fill(10);
  std::vector<uint8_t> out(188 * 500);
  CHECK(buffer.Read(out.data(), out.size(), 100) == static_cast<int>(out.size()));
  uint32_t value;
  std::memcpy(&value, &out[188 * 3 + 4], 4);
  CHECK(value == 3);

  CHECK(buffer.Seek(188 * 2000) == 188 * 2000);
  CHECK(buffer.Read(out.data(), 188, 100) == 188);
  std::memcpy(&value, &out[4], 4);
  CHECK(value == 2000);

  // Fill far past the ring, so the old data is gone and the reader has to jump.
  fill(800);
  CHECK(buffer.OldestPosition() > 0);
  CHECK(buffer.Seek(0) == buffer.OldestPosition());
  const int count = buffer.Read(out.data(), 188, 100);
  CHECK(count == 188);
  std::memcpy(&value, &out[4], 4);
  CHECK(static_cast<int64_t>(value) * 188 == buffer.OldestPosition());
  CHECK(out[0] == 0x47);

  // Up to the live edge, then a timeout, then the end.
  buffer.Seek(buffer.Length());
  CHECK(buffer.Read(out.data(), 188, 20) == 0);
  buffer.MarkEnded();
  CHECK(buffer.Read(out.data(), 188, 20) == -1);
}

static void TestStore(const std::string& folder)
{
  const std::string nested = folder + "/not/created/yet/state.json";
  CHECK(WriteFileAtomic(nested, "{}"));
  CHECK(FileSize(nested) == 2);

  RecordingStore store;
  store.SetFolder(folder + "/rec");
  std::filesystem::create_directories(PathFromUtf8(store.Folder()));

  const auto add = [&](const char* title, time_t start, unsigned int rule, const char* status) {
    RecordingInfo info;
    info.title = title;
    info.start = start;
    info.ruleId = rule;
    info.status = status;
    CHECK(store.Create(info));
    WriteFileAtomic(store.MediaPath(info.id), "data");
    return info.id;
  };

  add("Show: One?", 1000, 5, "complete");
  add("Show: One?", 2000, 5, "complete");
  const std::string newest = add("Show: One?", 3000, 5, "complete");
  add("Cut off", 4000, 0, "recording");

  CHECK(store.List().size() == 4);
  CHECK(store.RecoverInterrupted() == 1);
  CHECK(store.Prune(5, 1) == 2);
  const auto list = store.List();
  CHECK(list.size() == 2);
  CHECK(list[0].status == "partial");
  CHECK(list[1].id == newest);
  CHECK(list[1].size == 4);
  CHECK(store.Delete(newest));
  CHECK(!std::filesystem::exists(PathFromUtf8(store.Folder() + "/Show_ One_")));
  uint64_t total = 0;
  uint64_t used = 0;
  CHECK(store.DriveSpace(total, used) && total > 0);
}

static void TestRealGuide(const char* path)
{
  std::string text;
  if (!ReadWholeFile(path, text))
  {
    std::printf("Can't read %s\n", path);
    ++g_failed;
    return;
  }
  std::vector<Channel> lineup;
  for (const char* number : {"3.1", "5.1", "10.1", "12.1", "103.1", "128.1"})
    lineup.push_back(MakeChannel(number));
  ChannelEvents events;
  XmltvStats stats;
  CHECK(ParseXmltv(text, lineup, events, &stats));
  std::printf("%s: %d of %d channels, %d programs\n", path, stats.channelsMatched,
              static_cast<int>(lineup.size()), stats.programmes);
  CHECK(stats.channelsMatched >= 4);
}

int main(int argc, char** argv)
{
  const std::filesystem::path folder =
      std::filesystem::temp_directory_path() / "directtuner_tests";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);

  TestJson();
  TestLineup();
  TestXmltv();
  TestGuideMerge();
  TestSeries();
  TestConflicts();
  TestTimeshift(PathToUtf8(folder));
  TestStore(PathToUtf8(folder));
  for (int i = 1; i < argc; ++i)
    TestRealGuide(argv[i]);

  std::filesystem::remove_all(folder);
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
