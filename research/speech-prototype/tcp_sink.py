"""Optional LAN sink for the Cardputer diagnostic LOAD command.

Run only on a network you trust. The listener is intentionally temporary and
accepts one connection; it discards all received bytes.
"""
import argparse
import socket

parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, default=9876)
args = parser.parse_args()
with socket.socket() as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", args.port))
    listener.listen(1)
    listener.settimeout(60)
    print(f"Waiting for one connection on port {args.port}", flush=True)
    with listener.accept()[0] as client:
        client.settimeout(45)
        total = 0
        while True:
            chunk = client.recv(8192)
            if not chunk:
                break
            total += len(chunk)
        print(f"Received {total} bytes", flush=True)
