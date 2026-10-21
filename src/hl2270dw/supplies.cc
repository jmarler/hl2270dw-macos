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

#include "supplies.h"
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <cups/sidechannel.h>

namespace {

const char brother_maintenance_oid[] = "1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0";

// Printer-MIB prtMarkerSuppliesLevel / MaxCapacity, entries 1 (toner)
// and 2 (drum).
const char toner_level_oid[] = "1.3.6.1.2.1.43.11.1.1.9.1.1";
const char toner_max_oid[] = "1.3.6.1.2.1.43.11.1.1.8.1.1";
const char drum_level_oid[] = "1.3.6.1.2.1.43.11.1.1.9.1.2";
const char drum_max_oid[] = "1.3.6.1.2.1.43.11.1.1.8.1.2";

// Host-Resources-MIB hrPrinterDetectedErrorState.
const char error_state_oid[] = "1.3.6.1.2.1.25.3.5.1.2.1";

const int tag_drum_remaining = 0x41;
const int tag_toner_remaining = 0x6F;

// hrPrinterDetectedErrorState bits (RFC 3805), most significant bit of
// the first byte first, mapped to CUPS printer-state-reasons.
const char *const error_reasons[] = {
  "media-low-report",                // lowPaper
  "media-empty-warning",             // noPaper
  "toner-low-report",                // lowToner
  "toner-empty-warning",             // noToner
  "door-open-report",                // doorOpen
  "media-jam-warning",               // jammed
  "offline-report",                  // offline
  "other-warning",                   // serviceRequested
  "input-tray-missing-warning",      // inputTrayMissing
  "output-tray-missing-warning",     // outputTrayMissing
  "marker-supply-missing-warning",   // markerSupplyMissing
  "output-area-almost-full-report",  // outputNearFull
  "output-area-full-warning",        // outputFull
  "media-empty-warning",             // inputTrayEmpty
};
const size_t num_error_reasons = sizeof(error_reasons) / sizeof(error_reasons[0]);

int clamp_percent(long hundredths) {
  long p = (hundredths + 50) / 100;
  return (int)(p < 0 ? 0 : p > 100 ? 100 : p);
}

bool snmp_get(const char *oid, std::vector<uint8_t> *value, double timeout) {
  char data[2048];
  int len = sizeof(data) - 1;
  cups_sc_status_t status = cupsSideChannelSNMPGet(oid, data, &len, timeout);
  if (status != CUPS_SC_STATUS_OK || len <= 0)
    return false;
  value->assign(data, data + len);
  return true;
}

bool snmp_get_int(const char *oid, long *value, double timeout) {
  std::vector<uint8_t> v;
  if (!snmp_get(oid, &v, timeout))
    return false;
  std::string s(v.begin(), v.end());
  char *end;
  *value = strtol(s.c_str(), &end, 10);
  return end != s.c_str();
}

// Level from the standard Printer-MIB, as percent or a negative IPP code.
int standard_level(const char *level_oid, const char *max_oid, double timeout) {
  long level, max;
  if (!snmp_get_int(level_oid, &level, timeout))
    return -1;
  if (level < 0)
    return level >= -3 ? (int)level : -2;
  if (!snmp_get_int(max_oid, &max, timeout) || max <= 0)
    return -2;
  return clamp_percent(level * 10000 / max);
}

}  // namespace

bool parse_brother_maintenance(const uint8_t *data, size_t len,
                               std::map<int, long> *records) {
  size_t pos = 0;
  while (pos < len && data[pos] != 0xFF) {
    if (pos + 3 > len)
      return false;
    int tag = data[pos];
    size_t n = data[pos + 2];
    if (pos + 3 + n > len || n > 4)
      return false;
    long value = 0;
    for (size_t i = 0; i < n; ++i)
      value = (value << 8) | data[pos + 3 + i];
    (*records)[tag] = value;
    pos += 3 + n;
  }
  return true;
}

supply_levels levels_from_maintenance(const std::map<int, long> &records) {
  supply_levels levels;
  auto toner = records.find(tag_toner_remaining);
  if (toner != records.end())
    levels.toner = clamp_percent(toner->second);
  auto drum = records.find(tag_drum_remaining);
  if (drum != records.end())
    levels.drum = clamp_percent(drum->second);
  return levels;
}

void write_marker_attributes(FILE *f, const supply_levels &levels) {
  fprintf(f, "ATTR: marker-colors=#000000,none marker-levels=%d,%d "
             "marker-names=\"Black Toner Cartridge\",\"Drum Unit\" "
             "marker-types=toner,opc\n",
          levels.toner, levels.drum);
}

void write_error_state(FILE *f, const uint8_t *data, size_t len) {
  // Several bits can map to the same reason; it is set if any of them is.
  std::vector<std::pair<std::string, bool>> reasons;
  for (size_t bit = 0; bit < num_error_reasons; ++bit) {
    size_t byte = bit / 8;
    bool on = byte < len && (data[byte] & (0x80 >> (bit % 8)));
    auto it = std::find_if(reasons.begin(), reasons.end(),
                           [&](const std::pair<std::string, bool> &r) {
                             return r.first == error_reasons[bit];
                           });
    if (it == reasons.end())
      reasons.emplace_back(error_reasons[bit], on);
    else
      it->second = it->second || on;
  }
  std::string set, clear;
  for (const auto &r : reasons) {
    std::string &list = r.second ? set : clear;
    if (!list.empty())
      list += ',';
    list += r.first;
  }
  if (!clear.empty())
    fprintf(f, "STATE: -%s\n", clear.c_str());
  if (!set.empty())
    fprintf(f, "STATE: +%s\n", set.c_str());
}

bool report_printer_status(FILE *f, double timeout) {
  std::vector<uint8_t> value;

  // The error state is part of the standard MIB every network printer
  // has. If it does not answer, SNMP is unavailable and the remaining
  // queries would only add timeouts.
  if (!snmp_get(error_state_oid, &value, timeout)) {
    fprintf(f, "DEBUG: Printer status is not available over SNMP\n");
    return false;
  }
  write_error_state(f, value.data(), value.size());

  supply_levels levels;
  if (snmp_get(brother_maintenance_oid, &value, timeout)) {
    std::map<int, long> records;
    if (parse_brother_maintenance(value.data(), value.size(), &records))
      levels = levels_from_maintenance(records);
    else
      fprintf(f, "DEBUG: Malformed Brother maintenance data (%zu bytes)\n",
              value.size());
  }
  // Fall back to the standard Printer-MIB for anything Brother's data
  // did not cover.
  if (levels.toner < 0)
    levels.toner = standard_level(toner_level_oid, toner_max_oid, timeout);
  if (levels.drum < 0)
    levels.drum = standard_level(drum_level_oid, drum_max_oid, timeout);
  write_marker_attributes(f, levels);
  fflush(f);
  return true;
}
