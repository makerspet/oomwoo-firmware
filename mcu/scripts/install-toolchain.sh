#!/usr/bin/env bash
# Install the pinned xPack RISC-V GCC (Linux x64, also fine in WSL2 and CI)
# into ~/opt and print the PATH line to use. Plain upstream GCC: the WCH
# "WCH-Interrupt-fast" attribute is not used; ISRs use __attribute__((interrupt)).
set -euo pipefail

ver="14.3.0-1"
sha="be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726"
name="xpack-riscv-none-elf-gcc-$ver"
url="https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$ver/$name-linux-x64.tar.gz"
prefix="${PREFIX:-$HOME/opt}"

if [ -x "$prefix/$name/bin/riscv-none-elf-gcc" ]; then
  echo "already installed: $prefix/$name"
else
  mkdir -p "$prefix"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl -fL --retry 5 -o "$tmp/gcc.tgz" "$url"
  echo "$sha  $tmp/gcc.tgz" | sha256sum -c -
  tar -xzf "$tmp/gcc.tgz" -C "$prefix"
fi
echo "export PATH=\"$prefix/$name/bin:\$PATH\""
