#!/usr/bin/env bash
# One-command developer setup for the OOMWOO CH32 firmware (U13 CH32V307,
# U28 CH32L103) on Linux x64 or WSL2 (Ubuntu/Debian):
#   - build tools (make, git, curl, a host C compiler for the tests)
#   - xPack riscv-none-elf-gcc (pinned, sha256-checked)
#   - the WCH SDK at pinned commits (mcu/third_party/wch)
#   - wlink (WCH-LinkE) and wchisp (bootloaders) in ~/opt/bin
#   - udev rules for the probes and bootloaders (native Linux only)
#   - a PATH block in ~/.bashrc
# then builds both images and runs the host tests.
#
#   mcu/scripts/setup.sh            # everything
#   mcu/scripts/setup.sh --no-build # skip the test build
#
# Safe to run again: each step skips what is already in place.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
prefix="${PREFIX:-$HOME/opt}"
bindir="$prefix/bin"
build=1
[ "${1:-}" = "--no-build" ] && build=0

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "This script supports Linux x64 and WSL2. On Windows, run it inside WSL2 and use"
  echo "mcu/scripts/setup-windows.ps1 for the Windows flashing tools."
  exit 1
fi
is_wsl=0
grep -qi microsoft /proc/version 2>/dev/null && is_wsl=1

say "Build tools"
need=()
for t in make git curl tar sha256sum cc; do command -v "$t" >/dev/null || need+=("$t"); done
if [ ${#need[@]} -gt 0 ]; then
  echo "missing: ${need[*]}"
  if command -v apt-get >/dev/null; then
    sudo apt-get update
    sudo apt-get install -y make git curl tar coreutils build-essential
  else
    echo "Install them with your package manager, then run this script again."
    exit 1
  fi
else
  echo "ok"
fi

say "RISC-V GCC"
pathline="$("$here/scripts/install-toolchain.sh" | tail -1)"
eval "$pathline"
riscv-none-elf-gcc --version | head -1

say "WCH SDK"
"$here/scripts/fetch-wch-sdk.sh"

say "wlink and wchisp"
mkdir -p "$bindir"
fetch_tool() { # name url sha256
  local name="$1" url="$2" sha="$3" tmp
  if [ -x "$bindir/$name" ]; then
    echo "$name: $("$bindir/$name" --version)"
    return
  fi
  tmp="$(mktemp -d)"
  curl -fsSL --retry 5 -o "$tmp/a.tgz" "$url"
  echo "$sha  $tmp/a.tgz" | sha256sum -c --quiet -
  tar -xzf "$tmp/a.tgz" -C "$tmp"
  install -m 755 "$(find "$tmp" -type f -name "$name" | head -1)" "$bindir/$name"
  rm -rf "$tmp"
  echo "$name: $("$bindir/$name" --version)"
}
fetch_tool wlink https://github.com/ch32-rs/wlink/releases/download/v0.1.3/wlink-v0.1.3-linux-x64.tar.gz \
  b0d55a32adf55214e7b6bf29b5eb45c61a13252622e2be3bf5dba7573e4359d8
fetch_tool wchisp https://github.com/ch32-rs/wchisp/releases/download/v0.3.0/wchisp-v0.3.0-linux-x64.tar.gz \
  67e3d4eb0ffd3cc610d8927e3c3f452e2110531a3f14405dcaef87df219f200d
if ! "$bindir/wlink" --version >/dev/null 2>&1; then
  echo "wlink needs libusb and libudev: sudo apt-get install -y libusb-1.0-0 libudev1"
fi

if [ "$is_wsl" = 0 ]; then
  say "udev rules (WCH-Link probes, WCH bootloaders)"
  rules=/etc/udev/rules.d/50-wch.rules
  if [ -f "$rules" ]; then
    echo "already there: $rules"
  else
    sudo tee "$rules" >/dev/null <<'EOF'
# WCH-Link / WCH-LinkE (RISC-V and DAP modes)
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="8010", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="8012", MODE="0666"
# WCH USB bootloaders (wchisp)
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", MODE="0666"
EOF
    sudo udevadm control --reload-rules && sudo udevadm trigger
    echo "installed $rules (replug the probe)"
  fi
else
  echo
  echo "WSL2: USB probes are not visible here by default. Flash from Windows with"
  echo "mcu/scripts/setup-windows.ps1 tools, or attach the probe with usbipd-win."
fi

say "PATH"
marker="# >>> oomwoo-mcu >>>"
if grep -qF "$marker" "$HOME/.bashrc" 2>/dev/null; then
  echo "~/.bashrc already has it"
else
  {
    echo "$marker"
    echo "$pathline"
    echo "export PATH=\"$bindir:\$PATH\""
    echo "# <<< oomwoo-mcu <<<"
  } >>"$HOME/.bashrc"
  echo "added to ~/.bashrc (open a new shell, or: source ~/.bashrc)"
fi
export PATH="$bindir:$PATH"

if [ "$build" = 1 ]; then
  say "Build and test"
  make -C "$here" -j"$(nproc)" all
  make -C "$here" -j"$(nproc)" BOARD=evt all
  make -C "$here" test
  echo
  echo "Images: $here/build/robot/u13.bin, $here/build/robot/u28.bin,"
  echo "        $here/build/evt/u13.bin, $here/build/evt/u28.bin (evaluation boards)"
fi
say "Done"
