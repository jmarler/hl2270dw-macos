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
# Adds a print queue that uses hl2270dw-macos.
#
#   add-queue.sh [QUEUE_NAME] [DEVICE_URI]
#
# Without a device URI, the address of an existing HL-2270DW queue is
# reused, so the new queue can be tested next to the old one.

set -euo pipefail

NAME=${1:-HL2270DW_Native}
URI=${2:-}
PPD="/Library/Printers/PPDs/Contents/Resources/Brother HL-2270DW series hl2270dw-macos.gz"

if [ ! -f "$PPD" ]; then
  echo "hl2270dw-macos is not installed (missing $PPD)." >&2
  exit 1
fi

if [ -z "$URI" ]; then
  URI=$(lpstat -v 2>/dev/null | grep -i "2270" | grep -v "^device for $NAME:" \
        | head -1 | sed 's/^device for [^:]*: //')
fi
if [ -z "$URI" ]; then
  echo "No existing HL-2270DW queue found. Pass the device URI, for example:" >&2
  echo "  $0 $NAME socket://192.168.1.50" >&2
  exit 1
fi

echo "Adding queue $NAME for $URI"
# lpadmin needs administrator rights.
sudo lpadmin -p "$NAME" -E -v "$URI" -P "$PPD" \
  -D "Brother HL-2270DW (native)" -o printer-is-shared=false
echo "Done. Print the CUPS test page with:  lp -d $NAME /usr/share/cups/data/testprint"
