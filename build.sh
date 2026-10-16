#!/usr/bin/env bash
# Builds the payload in the build container.
#   ./build.sh         ps5-signal.elf (127.0.0.1 only)
#   ./build.sh dev     ps5-signal-dev.elf (also on the network, for testing)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$("$root/tools/setup-sdk.sh")
"$root/docker/run.sh" "make PS5_PAYLOAD_SDK=$sdk ${1:-all}"
