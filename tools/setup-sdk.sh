#!/usr/bin/env bash
# Downloads the PS5 payload SDK at a pinned release into .deps/ and prints its path (PS5_PAYLOAD_SDK).
# Does nothing if that release is already extracted.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=v0.43
hash=a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb
url="https://github.com/ps5-payload-dev/sdk/releases/download/$version/ps5-payload-sdk.zip"
cache="$root/.deps/payload-sdk"
release="$cache/$version"
archive="$cache/ps5-payload-sdk-$version.zip"

if [[ ! -f $release/.complete ]]; then
    mkdir -p "$cache"
    if [[ ! -f $archive ]] || ! printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict >/dev/null 2>&1; then
        echo "setup-sdk.sh: downloading the PS5 payload SDK $version" >&2
        curl -sfL "$url" -o "$archive.download"
        printf '%s  %s\n' "$hash" "$archive.download" | sha256sum --check --strict >&2
        mv "$archive.download" "$archive"
    fi
    rm -rf "$release"
    mkdir -p "$release"
    unzip -q "$archive" -d "$release"
    touch "$release/.complete"
fi
sdk=$(dirname "$(find "$release" -path '*/toolchain/prospero.mk' | head -1)")
dirname "$sdk"
