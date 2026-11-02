"""Sends an action to Kodi through its EventServer, which works when JSON-RPC is waiting on a dialog.

    kodi_action.py Select
"""
import socket
import struct
import sys


def packet(kind, payload, uid=4242):
    header = b"XBMC" + bytes([2, 0]) + struct.pack("!HIIHI", kind, 1, 1, len(payload), uid) + b"\0" * 10
    return header + payload


def send(action):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        target = ("127.0.0.1", 9777)
        hello = b"kodi_action\0" + bytes([0]) + struct.pack("!HII", 0, 0, 0)
        sock.sendto(packet(0x01, hello), target)
        sock.sendto(packet(0x0A, bytes([2]) + action.encode() + b"\0"), target)


if __name__ == "__main__":
    send(sys.argv[1])
