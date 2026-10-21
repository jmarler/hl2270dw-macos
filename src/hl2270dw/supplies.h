// This file is part of hl2270dw-macos, a native macOS driver for the
// Brother HL-2270DW based on the brlaser printer driver.
//
// Copyright 2026 Jon Marler
//
// hl2270dw-macos is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// hl2270dw-macos is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with hl2270dw-macos.  If not, see <http://www.gnu.org/licenses/>.

#ifndef HL2270DW_SUPPLIES_H
#define HL2270DW_SUPPLIES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <map>
#include <string>

// Supply levels in percent. Negative values follow the IPP marker-levels
// convention: -1 unavailable, -2 unknown, -3 "some remaining".
struct supply_levels {
  int toner = -1;
  int drum = -1;
};

// Brother's maintenance information (SNMP OID
// 1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0) is a list of records:
//   tag, 0x01, length, value (length bytes, big endian) ... 0xFF
// Returns false if the data is malformed.
bool parse_brother_maintenance(const uint8_t *data, size_t len,
                               std::map<int, long> *records);

// Converts maintenance records to levels: tag 0x6F is toner remaining and
// tag 0x41 drum remaining, both in hundredths of a percent.
supply_levels levels_from_maintenance(const std::map<int, long> &records);

// Writes the CUPS "ATTR:" line that updates the printer's marker-*
// attributes (shown by macOS as supply levels).
void write_marker_attributes(FILE *f, const supply_levels &levels);

// Converts the standard hrPrinterDetectedErrorState bit string to CUPS
// printer-state-reasons and writes the "STATE:" lines that set and clear
// them.
void write_error_state(FILE *f, const uint8_t *data, size_t len);

// Queries the printer through the CUPS backend's SNMP side channel and
// reports supply levels and error state on `f` (normally stderr).
// Returns false if the backend does not offer SNMP (for example USB).
bool report_printer_status(FILE *f, double timeout);

#endif  // HL2270DW_SUPPLIES_H
