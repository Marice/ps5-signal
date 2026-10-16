#!/usr/bin/env bash
# Runs a command in the build container with the project mounted at the same path.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec docker run --rm -u "$(id -u):$(id -g)" -v "$root:$root" -w "$root" -e HOME="$root/.home" \
    ps5-signal-build bash -lc "$*"
