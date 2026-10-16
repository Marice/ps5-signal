#!/usr/bin/env bash
# Sends a payload to the console's ELF loader (etaHEN: port 9021). PS5_HOST picks the console.
#   tools/deploy.sh                     ps5-signal.elf
#   tools/deploy.sh ps5-signal-dev.elf  the development build
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
host=${PS5_HOST:-192.168.68.125}
port=${PS5_ELFLDR_PORT:-9021}
payload=${1:-$root/ps5-signal.elf}
python3 -I - "$host" "$port" "$payload" <<'PY'
import socket, sys
host, port, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = open(path, "rb").read()
with socket.create_connection((host, port), timeout=10) as s:
    s.sendall(data)
print(f"deploy.sh: sent {path} ({len(data)} bytes) to {host}:{port}")
PY
