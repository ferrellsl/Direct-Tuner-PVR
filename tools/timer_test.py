"""Schedules real recordings of the next shows on the given channels, through Kodi's own API."""
import datetime
import json
import sys
import time

from kodi_rpc import call
from live_test import channels


def show_list():
    timers = call("PVR.GetTimers", {"properties": ["title", "state", "starttime", "endtime", "summary", "istimerrule", "ismanual"]})
    for timer in timers.get("result", {}).get("timers", []):
        print(json.dumps(timer, ensure_ascii=False))
    recordings = call("PVR.GetRecordings", {"properties": ["title", "file", "starttime", "runtime", "channel", "plot", "isdeleted"]})
    for item in recordings.get("result", {}).get("recordings", []):
        print(json.dumps({k: item.get(k) for k in ("recordingid", "title", "starttime", "runtime", "channel", "plot")}, ensure_ascii=False)[:220])


def broadcasts(uid):
    return call("PVR.GetBroadcasts", {"channelid": uid, "properties": [
        "title", "starttime", "endtime", "isseries", "episodename", "hastimer", "plot"]}).get("result", {}).get("broadcasts", [])


def next_show(uid):
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    upcoming = [b for b in broadcasts(uid) if b["starttime"] > now]
    return min(upcoming, key=lambda b: b["starttime"]) if upcoming else None


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    table = channels()
    if sys.argv[1] == "list":
        show_list()
    elif sys.argv[1] == "guide":
        for number in sys.argv[2:]:
            items = broadcasts(table[number]["channelid"])
            print(number, table[number]["label"], len(items), "programs,", items[0]["starttime"] if items else "", "to", items[-1]["endtime"] if items else "")
            for item in items[:3]:
                print("   ", item["starttime"], item["title"], "|", item["episodename"], "| series", item["isseries"], "|", item["plot"][:50])
    elif sys.argv[1] == "once":
        for number in sys.argv[2:]:
            show = next_show(table[number]["channelid"])
            print(number, show["title"], show["starttime"], show["endtime"], "(UTC)")
            print(call("PVR.AddTimer", {"broadcastid": show["broadcastid"]}))
    elif sys.argv[1] == "series":
        for number in sys.argv[2:]:
            show = next_show(table[number]["channelid"])
            print(number, show["title"], "series rule")
            print(call("PVR.AddTimer", {"broadcastid": show["broadcastid"], "timerrule": True}))
    elif sys.argv[1] == "instant":
        print(call("PVR.Record", {"record": True, "channel": table[sys.argv[2]]["channelid"]}))
    elif sys.argv[1] == "delete":
        print(call("PVR.DeleteTimer", {"timerid": int(sys.argv[2])}))
