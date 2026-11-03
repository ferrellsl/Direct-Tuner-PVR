"""Records channels in parallel through Kodi for a set time, then stops and counts lost packets.

    record_check.py <seconds> <guide number>...

Run signal_log.py alongside it to tell reception problems from code problems.
"""
import glob
import os
import sys
import time

from kodi_rpc import call
from live_test import channels


def lost_packets(path):
    """Counts continuity errors, and how far through the file each burst fell."""
    counters, positions, total = {}, [], 0
    with open(path, "rb") as file:
        while True:
            block = file.read(188 * 20000)
            if not block:
                break
            for i in range(0, len(block), 188):
                total += 1
                pid = ((block[i + 1] & 31) << 8) | block[i + 2]
                if pid == 0x1FFF or not block[i + 3] & 0x10:
                    continue
                count = block[i + 3] & 15
                if pid in counters and count != counters[pid] and count != (counters[pid] + 1) & 15:
                    positions.append(total)
                counters[pid] = count
    bursts, last = [], -10 ** 9
    for position in positions:
        if position - last > 5000:
            bursts.append(round(position / total, 3))
        last = position
    return total, len(positions), bursts


if __name__ == "__main__":
    seconds = int(sys.argv[1])
    table = channels()
    started = time.strftime("%H:%M:%S")
    since = time.time()
    for number in sys.argv[2:]:
        print(number, call("PVR.Record", {"record": True, "channel": table[number]["channelid"]}).get("result"))
    time.sleep(seconds)
    timers = call("PVR.GetTimers", {"properties": ["title", "state"]})["result"]["timers"]
    for timer in timers:
        if timer["state"] == "recording":
            print("stop", timer["title"], call("PVR.DeleteTimer", {"timerid": timer["timerid"]}).get("result"))
    print("ran", started, "to", time.strftime("%H:%M:%S"))
    time.sleep(5)
    folder = os.path.join(os.path.expanduser("~"), "Videos", "Kodi Recordings")
    for path in glob.glob(os.path.join(folder, "*", "*.ts")):
        if os.path.getmtime(path) >= since:
            print(os.path.basename(path), os.path.getsize(path), "packets, lost, bursts at:", lost_packets(path))
