"""Talks to a running Kodi over its JSON-RPC TCP port.

    kodi_rpc.py Method '{"param": 1}'
    kodi_rpc.py --wait 60       waits until Kodi answers
"""
import json
import socket
import sys
import time


def call(method, params=None, timeout=30):
    request = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        request["params"] = params
    with socket.create_connection(("127.0.0.1", 9090), timeout=5) as sock:
        sock.settimeout(timeout)
        sock.sendall(json.dumps(request).encode())
        decoder = json.JSONDecoder()
        text = ""
        while True:
            text += sock.recv(1 << 20).decode("utf-8", "replace")
            position = 0
            while position < len(text):
                try:
                    message, end = decoder.raw_decode(text, position)
                except ValueError:
                    break
                position = end
                if message.get("id") == 1:
                    return message
            text = text[position:]


def wait(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            if call("JSONRPC.Ping", timeout=3).get("result") == "pong":
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if sys.argv[1] == "--wait":
        ok = wait(int(sys.argv[2]))
        print("up" if ok else "timed out")
        sys.exit(0 if ok else 1)
    params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else None
    print(json.dumps(call(sys.argv[1], params), indent=1, ensure_ascii=False))
