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

#ifndef HL2270DW_OPTIONS_H
#define HL2270DW_OPTIONS_H

#include <string>

enum class improve_output {
  off,
  less_paper_curl,
  fix_intensity,
};

// Settings that apply to the whole job. They come from the PPD defaults
// overridden by the options the job was submitted with. The option names
// and values are the ones Brother's driver uses, so existing lpoptions
// settings keep working.
struct job_options {
  bool toner_save = false;              // TonerSaveMode
  bool density_set = false;             // BRDensityAdjustment, false = printer default
  int density = 0;                      // -6 (lighter) to 6 (darker)
  improve_output improve = improve_output::off;  // BRImproveOutput
  bool reduce_ghost = false;            // BRReduceGhost
  int sleep_minutes = -1;               // BRSleepTime, -1 = printer default
  bool skip_blank = false;              // BRSkipBlank
  std::string input_slot = "AutoSelect";  // InputSlot
  std::string first_page_slot;          // AP_FIRSTPAGE_InputSlot, or empty
  std::string user;
  std::string title;
};

// Parse the job options. `options` is the CUPS filter's argv[5] and
// `ppd_path` the PPD from the PPD environment variable; either may be null.
job_options parse_job_options(const char *user,
                              const char *title,
                              const char *options,
                              const char *ppd_path);

// Make a string safe to put inside a quoted PJL value.
std::string pjl_safe_string(const std::string &s, size_t max_len);

#endif  // HL2270DW_OPTIONS_H
