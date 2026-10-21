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

// CUPS command filter: handles the maintenance commands macOS sends from
// Printers & Scanners (supply levels and the printer self-test page).

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <cups/cups.h>
#include "hl_config.h"
#include "supplies.h"

namespace {

const double snmp_timeout = 2.0;

void print_self_test_page() {
  // Brother's driver sends a block of NUL bytes before the UEL, which
  // makes sure a sleeping printer has woken up and synchronized.
  static const char nuls[4096] = {};
  fwrite(nuls, 1, sizeof(nuls), stdout);
  fputs("\033%-12345X@PJL\n", stdout);
  fputs("@PJL EXECUTE TESTPRINT\r\n", stdout);
  fputs("\033%-12345X", stdout);
  fflush(stdout);
  fprintf(stderr, "PAGE: 1 1\n");
}

}  // namespace

int main(int argc, char *argv[]) {
  fprintf(stderr, "DEBUG: commandtohl2270dw version %s\n", HL2270DW_VERSION);

  if (argc != 6 && argc != 7) {
    fprintf(stderr, "ERROR: commandtohl2270dw job-id user title copies "
                    "options [file]\n");
    return 1;
  }

  cups_file_t *in = argc == 7 ? cupsFileOpen(argv[6], "r")
                              : cupsFileStdin();
  if (!in) {
    fprintf(stderr, "ERROR: commandtohl2270dw: Unable to open command file\n");
    return 1;
  }

  int status = 0;
  char line[1024];
  while (cupsFileGets(in, line, sizeof(line))) {
    char *cmd = line + strspn(line, " \t");
    if (!*cmd || *cmd == '#')
      continue;
    cmd[strcspn(cmd, " \t\r\n")] = '\0';

    if (!strcasecmp(cmd, "ReportLevels")) {
      fprintf(stderr, "INFO: Checking supply levels\n");
      if (!report_printer_status(stderr, snmp_timeout))
        fprintf(stderr, "INFO: Supply levels are only available when the "
                        "printer is connected over the network.\n");
    } else if (!strcasecmp(cmd, "PrintSelfTestPage")) {
      fprintf(stderr, "INFO: Printing self-test page\n");
      print_self_test_page();
    } else {
      fprintf(stderr, "ERROR: commandtohl2270dw: Unsupported command "
                      "\"%s\"\n", cmd);
      status = 1;
    }
  }
  cupsFileClose(in);
  fprintf(stderr, "INFO: Ready\n");
  return status;
}
