#!/usr/bin/env bash
# Fetch the WCH vendor SDK sources (peripheral library, startup, core) at
# pinned commits into mcu/third_party/wch/. They are not committed here: the
# WCH files carry WCH's own terms ("used for microcontrollers manufactured by
# Nanjing Qinheng Microelectronics"), not Apache-2.0.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
dest="${WCH_SDK:-$here/third_party/wch}"

# name|repo|commit|paths
repos=(
  "ch32v307|https://github.com/openwch/ch32v307.git|8b3699d9e4eafc3102e382b74266a1165a2bb584|EVT/EXAM/SRC"
  "ch32l103|https://github.com/openwch/ch32l103.git|8237d6a11d7d81d885b4fd524db3cd818843ad7d|EVT/EXAM/SRC EVT/EXAM/USBPD"
)

mkdir -p "$dest"
for entry in "${repos[@]}"; do
  IFS='|' read -r name url commit paths <<<"$entry"
  dir="$dest/$name"
  if [ -f "$dir/.fetched-$commit" ]; then
    echo "$name: up to date ($commit)"
    continue
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  git -C "$dir" config core.sparseCheckout true
  git -C "$dir" sparse-checkout set --no-cone $(for p in $paths; do printf '%s/* ' "$p"; done)
  git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
  git -C "$dir" checkout -q FETCH_HEAD
  touch "$dir/.fetched-$commit"
  echo "$name: fetched $commit"
done
