"""Pauses, rewinds and returns to live on a channel in a running Kodi."""
import sys
import time

from kodi_rpc import call
from live_test import channels, status


def show(label):
    current = status()
    if not current:
        print(label, "no player")
        return None
    player, p = current
    labels = call("XBMC.GetInfoLabels", {"labels": ["Player.TimeshiftStart", "Player.TimeshiftEnd", "Player.Time", "Player.Progress", "PVR.TimeShiftProgress", "PVR.TimeshiftOffset"]}).get("result", {})
    print(label, "time", p.get("time"), "total", p.get("totaltime"), "speed", p.get("speed"), "seek", p.get("canseek"), list(labels.values()))
    return player


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    number = sys.argv[1]
    table = channels()
    print(call("Player.Open", {"item": {"channelid": table[number]["channelid"]}}).get("result"))
    time.sleep(int(sys.argv[2]) if len(sys.argv) > 2 else 40)
    player = show("playing")
    print("pause:", call("Player.PlayPause", {"playerid": player, "play": False}).get("result"))
    time.sleep(20)
    show("paused 20s")
    print("resume:", call("Player.PlayPause", {"playerid": player, "play": True}).get("result"))
    time.sleep(8)
    show("resumed")
    print("seek -30s:", call("Player.Seek", {"playerid": player, "value": {"seconds": -30}}))
    time.sleep(6)
    show("after back")
    print("seek +15s:", call("Player.Seek", {"playerid": player, "value": {"seconds": 15}}))
    time.sleep(6)
    show("after forward")
    print("seek to 100%:", call("Player.Seek", {"playerid": player, "value": {"percentage": 99.5}}))
    time.sleep(8)
    show("near live")
    call("Player.Stop", {"playerid": player})
