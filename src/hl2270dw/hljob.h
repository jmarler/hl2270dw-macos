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

#ifndef HL2270DW_HLJOB_H
#define HL2270DW_HLJOB_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <vector>
#include "options.h"

// The parts of a CUPS page header the driver uses.
struct page_header {
  int xres = 600;
  int yres = 600;
  int width_pt = 612;       // PageSize[0]
  int height_pt = 792;      // PageSize[1]
  bool duplex = false;
  bool tumble = false;
  unsigned media_type = 1;  // cupsMediaType, Brother numbering
  int copies = 1;
  int bytes_per_line = 0;
  int lines = 0;
};

// Writes one print job in the HL-2270DW's PJL + PCL "1030" format.
class hl_job {
 public:
  // `now` is used for the printer's job log.
  hl_job(FILE *out, const job_options &options, time_t now);
  ~hl_job();

  hl_job(const hl_job &) = delete;
  hl_job &operator=(const hl_job &) = delete;

  // Adds a page. `pixels` holds header.lines rows of
  // header.bytes_per_line bytes, 1 bit per pixel, 1 = black.
  // Returns false if the page was left out because it is blank and
  // BRSkipBlank is on.
  bool add_page(const page_header &header, const std::vector<uint8_t> &pixels);

  // Ends the job. Called by the destructor if not called explicitly.
  void finish();

  int pages_printed() const { return pages_printed_; }

 private:
  // Settings that require a new PJL header when they change.
  struct settings {
    int resolution;
    bool duplex;
    bool tumble;
    unsigned media_type;
    std::string paper;
    bool postcard;

    bool operator==(const settings &o) const {
      return resolution == o.resolution && duplex == o.duplex
          && tumble == o.tumble && media_type == o.media_type
          && paper == o.paper && postcard == o.postcard;
    }
  };

  void write_job_start();
  void write_settings(const settings &s);
  void write_tray(int page_number);
  void write_raster(const page_header &header, int shift_bytes,
                    const std::vector<uint8_t> &pixels);

  FILE *out_;
  job_options options_;
  std::string user_;
  std::string title_;
  time_t now_;
  bool started_;
  bool finished_;
  bool have_settings_;
  settings current_;
  int pages_printed_;
};

#endif  // HL2270DW_HLJOB_H
