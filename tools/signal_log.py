"""Logs every active tuner's signal once a second, so glitches can be matched to reception.

    signal_log.py <tuner address> <seconds> <output file>
"""
import json
import sys
import time
import urllib.request

host = sys.argv[1]
seconds = int(sys.argv[2])
with open(sys.argv[3], "w", buffering=1) as out:
    for _ in range(seconds):
        try:
            with urllib.request.urlopen("http://%s/status.json" % host, timeout=2) as reply:
                tuners = json.load(reply)
            active = ["%s %s str=%s q=%s sym=%s rate=%s" % (
                t["Resource"], t["VctNumber"], t.get("SignalStrengthPercent"), t.get("SignalQualityPercent"),
                t.get("SymbolQualityPercent"), t.get("NetworkRate")) for t in tuners if "VctNumber" in t]
            out.write(time.strftime("%H:%M:%S ") + " | ".join(active) + "\n")
        except OSError as error:
            out.write(time.strftime("%H:%M:%S ") + "status failed: %s\n" % error)
        time.sleep(1)
