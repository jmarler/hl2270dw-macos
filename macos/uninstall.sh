#!/bin/bash
# This file is part of hl2270dw-macos.
#
# Copyright 2026 Jon Marler
#
# hl2270dw-macos is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# Removes hl2270dw-macos. Run with sudo:
#   sudo /Library/Printers/hl2270dw/uninstall.sh
# Print queues that use this driver stop working; delete them in System
# Settings > Printers & Scanners first, or switch them to another driver.

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Please run with sudo: sudo $0" >&2
  exit 1
fi

PPD="/Library/Printers/PPDs/Contents/Resources/Brother HL-2270DW series hl2270dw-macos.gz"

# Warn about queues that still use the driver.
for ppd in /etc/cups/ppd/*.ppd; do
  [ -e "$ppd" ] || continue
  if grep -q "rastertohl2270dw" "$ppd"; then
    echo "Note: print queue $(basename "$ppd" .ppd) uses this driver."
  fi
done

rm -f "$PPD"
rm -rf /Library/Printers/hl2270dw
pkgutil --forget io.github.jmarler.hl2270dw-macos >/dev/null 2>&1 || true
echo "hl2270dw-macos has been removed."
