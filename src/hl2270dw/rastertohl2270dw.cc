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

// CUPS filter: converts 1-bit CUPS raster to the HL-2270DW's print data.

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <cups/raster.h>
#include "hl_config.h"
#include "hljob.h"
#include "options.h"
#include "supplies.h"

namespace {

volatile sig_atomic_t interrupted = 0;

void sigterm_handler(int) {
  interrupted = 1;
}

// Seconds to wait for each SNMP answer from the printer.
const double snmp_timeout = 2.0;

page_header to_page_header(const cups_page_header2_t &h) {
  page_header p;
  p.xres = (int)h.HWResolution[0];
  p.yres = (int)h.HWResolution[1];
  p.width_pt = (int)h.PageSize[0];
  p.height_pt = (int)h.PageSize[1];
  p.duplex = h.Duplex != 0;
  p.tumble = h.Tumble != 0;
  p.media_type = h.cupsMediaType;
  p.copies = (int)h.NumCopies;
  p.bytes_per_line = (int)h.cupsBytesPerLine;
  p.lines = (int)h.cupsHeight;
  return p;
}

}  // namespace

int main(int argc, char *argv[]) {
  fprintf(stderr, "DEBUG: rastertohl2270dw version %s\n", HL2270DW_VERSION);

  if (argc != 6 && argc != 7) {
    fprintf(stderr, "ERROR: rastertohl2270dw job-id user title copies "
                    "options [file]\n");
    fprintf(stderr, "INFO: This program is a CUPS filter. It is not "
                    "intended to be run manually.\n");
    return 1;
  }

  signal(SIGTERM, sigterm_handler);
  signal(SIGPIPE, SIG_IGN);

  int fd = STDIN_FILENO;
  if (argc == 7) {
    fd = open(argv[6], O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "ERROR: rastertohl2270dw: Unable to open raster file\n");
      return 1;
    }
  }

  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) {
    fprintf(stderr, "DEBUG: rastertohl2270dw: Cannot read raster data. Most "
                    "likely an earlier filter in the pipeline failed.\n");
    return 1;
  }

  job_options options = parse_job_options(argv[2], argv[3], argv[5],
                                          getenv("PPD"));
  bool snmp = report_printer_status(stderr, snmp_timeout);

  int status = 0;
  int pages_read = 0;
  {
    hl_job job(stdout, options, time(nullptr));
    cups_page_header2_t header;
    std::vector<uint8_t> pixels;
    while (!interrupted && cupsRasterReadHeader2(ras, &header)) {
      ++pages_read;
      if (header.cupsBitsPerPixel != 1 || header.cupsBitsPerColor != 1
          || header.cupsNumColors != 1 || header.cupsColorSpace != CUPS_CSPACE_K
          || header.cupsBytesPerLine == 0 || header.cupsBytesPerLine > 10000
          || header.cupsHeight > 100000) {
        fprintf(stderr, "ERROR: rastertohl2270dw: Page %d: Unsupported raster "
                        "data (%u bits per pixel, color space %u).\n",
                pages_read, header.cupsBitsPerPixel, header.cupsColorSpace);
        status = 1;
        break;
      }

      page_header page = to_page_header(header);
      pixels.assign((size_t)page.bytes_per_line * page.lines, 0);
      for (int y = 0; y < page.lines && !interrupted; ++y) {
        unsigned char *row = pixels.data() + (size_t)y * page.bytes_per_line;
        if (cupsRasterReadPixels(ras, row, header.cupsBytesPerLine)
            != header.cupsBytesPerLine) {
          fprintf(stderr, "DEBUG: rastertohl2270dw: Page %d is truncated.\n",
                  pages_read);
          break;
        }
      }
      if (interrupted)
        break;

      if (job.add_page(page, pixels)) {
        fprintf(stderr, "PAGE: %d %d\n", job.pages_printed(), page.copies);
      } else {
        fprintf(stderr, "INFO: Skipped blank page %d\n", pages_read);
      }
    }
    job.finish();
  }
  cupsRasterClose(ras);
  if (fd != STDIN_FILENO)
    close(fd);

  if (pages_read == 0) {
    fprintf(stderr, "ERROR: rastertohl2270dw: No pages were found.\n");
    status = 1;
  }

  fflush(stdout);
  if (ferror(stdout)) {
    fprintf(stderr, "DEBUG: rastertohl2270dw: Could not write print data. "
                    "Most likely the CUPS backend failed.\n");
    return 1;
  }

  // Levels change while printing, so report them again at the end.
  if (snmp)
    report_printer_status(stderr, snmp_timeout);
  return status;
}
