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

#include "lest.hpp"
#include <stdint.h>
#include <string>
#include <vector>
#include "tempfile.h"
#include "hljob.h"
#include "options.h"
#include "paper.h"
#include "supplies.h"

namespace {

std::string as_string(const std::vector<uint8_t> &v) {
  return std::string(v.begin(), v.end());
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// 2026-10-21 04:20:35 UTC, a Wednesday.
const time_t test_time = 1792556435;

page_header small_page(int lines) {
  page_header h;
  h.width_pt = 612;
  h.height_pt = 792;
  h.bytes_per_line = 4;
  h.lines = lines;
  return h;
}

// Maintenance data read from a real HL-2270DW: toner 44.00 %, drum 38.51 %.
const uint8_t maintenance_sample[] = {
  0x00, 0x01, 0x04, 0x00, 0x00, 0x1C, 0xD2,
  0x11, 0x01, 0x04, 0x00, 0x00, 0x1C, 0xD2,
  0x6F, 0x01, 0x04, 0x00, 0x00, 0x11, 0x30,
  0x31, 0x01, 0x01, 0x01,
  0x41, 0x01, 0x04, 0x00, 0x00, 0x0F, 0x0B,
  0x63, 0x01, 0x01, 0x01,
  0xFF,
};

}  // namespace

const lest::test specification[] = {
  "Standard paper sizes get their PJL name and a 1/8 inch shift",
  [] {
    paper_selection p = select_paper(612, 792, 600);
    EXPECT(p.pjl_name == "LETTER");
    EXPECT(p.shift_bytes == 9);
    EXPECT(select_paper(612, 792, 300).shift_bytes == 4);
    EXPECT(select_paper(612, 792, 1200).shift_bytes == 18);
    EXPECT(select_paper(595, 842, 600).pjl_name == "A4");
    EXPECT(select_paper(595, 420, 600).pjl_name == "A5L");
    EXPECT(select_paper(297, 684, 600).pjl_name == "COM10");
    EXPECT(select_paper(612, 936, 600).pjl_name == "FOLIO");
    EXPECT(select_paper(624, 312, 600).pjl_name == "DLL");
  },

  "Custom sizes use the smallest paper they fit in and are centered",
  [] {
    // Values measured with Brother's own driver.
    paper_selection p = select_paper(400, 700, 600);
    EXPECT(p.pjl_name == "B5");
    EXPECT(p.shift_bytes == 60);
    EXPECT(select_paper(400, 700, 300).shift_bytes == 30);
    EXPECT(select_paper(300, 500, 600).pjl_name == "DL");
    EXPECT(select_paper(300, 500, 600).shift_bytes == 15);
    EXPECT(select_paper(500, 900, 600).pjl_name == "FOLIO");
    EXPECT(select_paper(500, 900, 600).shift_bytes == 67);
    EXPECT(select_paper(216, 329, 600).pjl_name == "P3X5");
    EXPECT(select_paper(612, 1152, 600).pjl_name == "USERDEF");
    EXPECT(select_paper(612, 1152, 600).shift_bytes == 9);
  },

  "Job options are read from the option string",
  [] {
    job_options o = parse_job_options(
        "user", "title",
        "TonerSaveMode=ON BRImproveOutput=BRFixIntensity BRReduceGhost=ON "
        "BRSleepTime=Custom.15 BRSkipBlank=ON InputSlot=Tray1 "
        "AP_FIRSTPAGE_InputSlot=Manual",
        nullptr);
    EXPECT(o.toner_save);
    EXPECT(o.improve == improve_output::fix_intensity);
    EXPECT(o.reduce_ghost);
    EXPECT(o.sleep_minutes == 15);
    EXPECT(o.skip_blank);
    EXPECT(o.input_slot == "Tray1");
    EXPECT(o.first_page_slot == "Manual");
  },

  "Job options have Brother's defaults",
  [] {
    job_options o = parse_job_options("user", "title", "", nullptr);
    EXPECT(!o.toner_save);
    EXPECT(o.improve == improve_output::off);
    EXPECT(!o.reduce_ghost);
    EXPECT(o.sleep_minutes == -1);
    EXPECT(!o.skip_blank);
    EXPECT(o.input_slot == "AutoSelect");
    EXPECT(o.first_page_slot.empty());
  },

  "ManualFeed selects the manual feed slot, but not when false",
  [] {
    EXPECT(parse_job_options("u", "t", "ManualFeed", nullptr).input_slot
           == "Manual");
    EXPECT(parse_job_options("u", "t", "ManualFeed=False", nullptr).input_slot
           == "AutoSelect");
  },

  "Sleep times outside the printer's range fall back to 5 minutes",
  [] {
    EXPECT(parse_job_options("u", "t", "BRSleepTime=500", nullptr)
           .sleep_minutes == 5);
    EXPECT(parse_job_options("u", "t", "BRSleepTime=Default", nullptr)
           .sleep_minutes == -1);
  },

  "Quotes and control characters are removed from PJL strings",
  [] {
    EXPECT(pjl_safe_string("a \"b\"\n\\c", 80) == "a _b___c");
    EXPECT(pjl_safe_string(std::string(100, 'x'), 80).size() == 80);
  },

  "An empty job produces no output",
  [] {
    tempfile f;
    {
      hl_job job(f.file(), job_options(), test_time);
    }
    EXPECT(f.data().empty());
  },

  "The job header matches Brother's driver",
  [] {
    job_options o;
    o.user = "jmarler";
    o.title = "My Doc";
    o.toner_save = true;
    o.improve = improve_output::fix_intensity;
    o.reduce_ghost = true;
    o.sleep_minutes = 15;
    o.input_slot = "Manual";
    o.first_page_slot = "Tray1";
    page_header h = small_page(2);
    h.xres = h.yres = 300;
    h.width_pt = 284;
    h.height_pt = 419;
    h.media_type = 12;
    h.duplex = true;
    tempfile f;
    {
      hl_job job(f.file(), o, test_time);
      job.add_page(h, std::vector<uint8_t>(8, 0x0F));
    }
    std::string out = as_string(f.data());
    // Captured from Brother's rastertobrother2130 with the same settings.
    const std::string expected =
        "\033%-12345X@PJL\n"
        "@PJL SET HOLD=OFF\n"
        "@PJL SET JOBNAME=\"My Doc\"\n"
        "@PJL SET USERNAME=\"jmarler\"\n"
        "@PJL JOB NAME=\"My Doc\"\n"
        "@PJL PRINTLOG ITEM = 1,PRINTER\n"
        "@PJL PRINTLOG ITEM = 2,Wed,21 Oct 2026 4:20:35\n"
        "@PJL PRINTLOG ITEM = 3,jmarler\n"
        "@PJL PRINTLOG ITEM = 4,Macintosh\n"
        "@PJL SET STRINGCODESET=HPROMAN8\n"
        "@PJL SET LOGINUSER=\"jmarler\"\n"
        "@PJL SET RESOLUTION = 300\n"
        "@PJL SET PAPERFEEDSPEED = FULL\n"
        "@PJL SET ECONOMODE = ON\n"
        "@PJL SET LESSPAPERCURL=OFF\n"
        "@PJL SET FIXINTENSITYUP=ON\n"
        "@PJL SET TRANSFERLEVELUP=ON\n"
        "@PJL SET MEDIATYPE=POSTCARD\n"
        "@PJL DEFAULT AUTOSLEEP = ON\n"
        "@PJL DEFAULT TIMEOUTSLEEP = 15\n"
        "@PJL SET AUTOSLEEP = ON\n"
        "@PJL SET TIMEOUTSLEEP = 15\n"
        "@PJL SET ORIENTATION = PORTRAIT\n"
        "@PJL SET PAPER = POSTCARD\n"
        "@PJL SET MEDIATYPE=THICK\n"
        "@PJL SET PAGEPROTECT = AUTO\n"
        "@PJL ENTER LANGUAGE = PCL\n"
        "\033E\033&l1S\033&l1h1001H\033&l1X\033*b1030m";
    EXPECT(out.compare(0, expected.size(), expected) == 0);
    const std::string trailer =
        "1030M\f\033%-12345X@PJL\n@PJL EOJ NAME=\"My Doc\"\n\033%-12345X";
    EXPECT(out.size() > trailer.size());
    EXPECT(out.compare(out.size() - trailer.size(), trailer.size(), trailer)
           == 0);
  },

  "Duplex selects long-edge or short-edge binding",
  [] {
    page_header h = small_page(1);
    h.duplex = true;
    h.tumble = true;
    tempfile f;
    {
      hl_job job(f.file(), job_options(), test_time);
      job.add_page(h, std::vector<uint8_t>(4, 1));
    }
    EXPECT(contains(as_string(f.data()), "\033E\033&l2S"));
  },

  "The first page source applies to page 1 only",
  [] {
    job_options o;
    o.input_slot = "Manual";
    o.first_page_slot = "Tray1";
    tempfile f;
    {
      hl_job job(f.file(), o, test_time);
      for (int i = 0; i < 3; ++i)
        job.add_page(small_page(1), std::vector<uint8_t>(4, 1));
    }
    std::string out = as_string(f.data());
    size_t first = out.find("\033&l1h1001H\033&l1X");
    size_t second = out.find("\033&l2H\033&l1X");
    EXPECT(first != std::string::npos);
    EXPECT(second != std::string::npos);
    EXPECT(first < second);
    EXPECT(out.find("\033&l2H", second + 1) == std::string::npos);
  },

  "Blank pages are skipped only when BRSkipBlank is on",
  [] {
    job_options o;
    o.skip_blank = true;
    tempfile f;
    {
      hl_job job(f.file(), o, test_time);
      EXPECT(!job.add_page(small_page(2), std::vector<uint8_t>(8, 0)));
      EXPECT(job.add_page(small_page(2), std::vector<uint8_t>(8, 1)));
      EXPECT(job.pages_printed() == 1);
    }
    std::string out = as_string(f.data());
    EXPECT(out.find("1030M\f") == out.rfind("1030M\f"));

    tempfile g;
    {
      hl_job job(g.file(), job_options(), test_time);
      EXPECT(job.add_page(small_page(2), std::vector<uint8_t>(8, 0)));
    }
  },

  "A change of paper size between pages starts a new PJL section",
  [] {
    tempfile f;
    {
      hl_job job(f.file(), job_options(), test_time);
      job.add_page(small_page(1), std::vector<uint8_t>(4, 1));
      page_header a4 = small_page(1);
      a4.width_pt = 595;
      a4.height_pt = 842;
      job.add_page(a4, std::vector<uint8_t>(4, 1));
    }
    std::string out = as_string(f.data());
    EXPECT(contains(out, "@PJL SET PAPER = LETTER"));
    EXPECT(contains(out, "1030M\f\033%-12345X@PJL\n@PJL SET RESOLUTION"));
    EXPECT(contains(out, "@PJL SET PAPER = A4"));
  },

  "Brother maintenance data is parsed",
  [] {
    std::map<int, long> records;
    EXPECT(parse_brother_maintenance(maintenance_sample,
                                     sizeof(maintenance_sample), &records));
    EXPECT(records[0x11] == 7378);
    EXPECT(records[0x6F] == 4400);
    EXPECT(records[0x41] == 3851);
    supply_levels levels = levels_from_maintenance(records);
    EXPECT(levels.toner == 44);
    EXPECT(levels.drum == 39);
  },

  "Truncated maintenance data is rejected",
  [] {
    std::map<int, long> records;
    EXPECT(!parse_brother_maintenance(maintenance_sample, 5, &records));
  },

  "Supply levels are reported as marker attributes",
  [] {
    tempfile f;
    supply_levels levels;
    levels.toner = 44;
    levels.drum = 39;
    write_marker_attributes(f.file(), levels);
    EXPECT(as_string(f.data()) ==
           "ATTR: marker-colors=#000000,none marker-levels=44,39 "
           "marker-names=\"Black Toner Cartridge\",\"Drum Unit\" "
           "marker-types=toner,opc\n");
  },

  "Printer errors are mapped to printer-state-reasons",
  [] {
    tempfile f;
    const uint8_t low_toner_and_jam[] = { 0x24, 0x00 };
    write_error_state(f.file(), low_toner_and_jam, 2);
    std::string out = as_string(f.data());
    EXPECT(contains(out, "STATE: +toner-low-report,media-jam-warning\n"));
    EXPECT(contains(out, "STATE: -media-low-report,media-empty-warning,"));
  },

  "No paper and an empty input tray both set media-empty",
  [] {
    tempfile f;
    const uint8_t tray_empty[] = { 0x00, 0x04 };
    write_error_state(f.file(), tray_empty, 2);
    std::string out = as_string(f.data());
    EXPECT(contains(out, "STATE: +media-empty-warning\n"));
    EXPECT(!contains(out, "-media-low-report,media-empty-warning"));
  },
};

int main() {
  return lest::run(specification);
}
