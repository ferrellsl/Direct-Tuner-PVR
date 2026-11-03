"""Plays channels in a running Kodi and reports what the player sees."""
import json
import sys
import time

from kodi_rpc import call


def channels():
    groups = call("PVR.GetChannelGroups", {"channeltype": "tv"})["result"]["channelgroups"]
    result = call("PVR.GetChannels", {"channelgroupid": groups[0]["channelgroupid"],
                                      "properties": ["channelnumber", "subchannelnumber"]})
    table = {}
    for item in result["result"]["channels"]:
        table["%d.%d" % (item["channelnumber"], item["subchannelnumber"])] = item
        table.setdefault(item["label"], item)
    return table


def status():
    players = call("Player.GetActivePlayers").get("result", [])
    if not players:
        return None
    player = players[0]["playerid"]
    properties = call("Player.GetProperties", {"playerid": player, "properties": [
        "time", "totaltime", "speed", "percentage", "canseek", "live",
        "currentaudiostream", "currentvideostream"]}).get("result", {})
    return player, properties


def play(number, seconds, table):
    channel = table[number]
    print("==", number, channel["label"], "uid", channel["channelid"])
    print(" open:", json.dumps(call("Player.Open", {"item": {"channelid": channel["channelid"]}}).get("result")))
    for _ in range(seconds // 5):
        time.sleep(5)
        current = status()
        if not current:
            print(" no player")
            continue
        _, p = current
        audio = p.get("currentaudiostream") or {}
        video = p.get("currentvideostream") or {}
        labels = call("XBMC.GetInfoLabels", {"labels": ["Player.Process(audiodecoder)", "Player.Process(audiosamplerate)", "Player.Process(videofps)", "Player.Process(videodecoder)"]}).get("result", {})
        print("  ", list(labels.values()))
        print(" t=%s speed=%s seek=%s live=%s video=%s %sx%s audio=%s ch=%s" % (
            p.get("time"), p.get("speed"), p.get("canseek"), p.get("live"), video.get("codec"),
            video.get("width"), video.get("height"), audio.get("codec"), audio.get("channels")))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    table = channels()
    print(len(table), "channels")
    for number in sys.argv[2:]:
        play(number, int(sys.argv[1]), table)
    call("Player.Stop", {"playerid": 1})
