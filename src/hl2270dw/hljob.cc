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

#include "hljob.h"
#include <string.h>
#include <algorithm>
#include "paper.h"
#include "../line.h"
#include "../block.h"

namespace {

const char uel[] = "\033%-12345X";

// Job names and user names are truncated to this length, like the
// Brother driver does.
const size_t max_name_length = 80;

// brlaser restarts the line compression every 64 lines.
const int lines_per_band = 64;

const char *media_type_name(unsigned media_type) {
  switch (media_type) {
    case 2: return "THIN";
    case 3: return "THICK";
    case 4: return "THICK2";
    case 5: return "BOND";
    case 6: return "TRANSPARENCY";
    case 7: return "ENVELOPES";
    case 8: return "ENVTHICK";
    case 9: return "ENVTHIN";
    case 10: return "RECYCLED";
    case 11: return "LABEL";
    case 12: return "POSTCARD";
    default: return "REGULAR";
  }
}

// PCL paper source command for an InputSlot choice.
const char *tray_command(const std::string &slot) {
  if (slot == "Tray1")
    return "\033&l1h1001H";
  if (slot == "Manual")
    return "\033&l2H";
  return "\033&l7H";  // Auto select
}

bool all_zero(const std::vector<uint8_t> &v) {
  return std::none_of(v.begin(), v.end(), [](uint8_t b) { return b != 0; });
}

}  // namespace

hl_job::hl_job(FILE *out, const job_options &options, time_t now)
    : out_(out),
      options_(options),
      user_(pjl_safe_string(options.user, max_name_length)),
      title_(pjl_safe_string(options.title, max_name_length)),
      now_(now),
      started_(false),
      finished_(false),
      have_settings_(false),
      current_(),
      pages_printed_(0) {
}

hl_job::~hl_job() {
  finish();
}

void hl_job::write_job_start() {
  static const char *const days[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };
  static const char *const months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  struct tm t;
  gmtime_r(&now_, &t);

  fprintf(out_, "%s@PJL\n", uel);
  fprintf(out_, "@PJL SET HOLD=OFF\n");
  fprintf(out_, "@PJL SET JOBNAME=\"%s\"\n", title_.c_str());
  fprintf(out_, "@PJL SET USERNAME=\"%s\"\n", user_.c_str());
  fprintf(out_, "@PJL JOB NAME=\"%s\"\n", title_.c_str());
  // The printer keeps a log of recent jobs.
  fprintf(out_, "@PJL PRINTLOG ITEM = 1,PRINTER\n");
  fprintf(out_, "@PJL PRINTLOG ITEM = 2,%s,%d %s %d %d:%d:%d\n",
          days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year + 1900,
          t.tm_hour, t.tm_min, t.tm_sec);
  fprintf(out_, "@PJL PRINTLOG ITEM = 3,%s\n", user_.c_str());
  fprintf(out_, "@PJL PRINTLOG ITEM = 4,Macintosh\n");
  fprintf(out_, "@PJL SET STRINGCODESET=HPROMAN8\n");
  // User name for the printer's Secure Function Lock.
  fprintf(out_, "@PJL SET LOGINUSER=\"%s\"\n", user_.c_str());
}

void hl_job::write_settings(const settings &s) {
  if (have_settings_) {
    // A later page changed a setting: start a new PJL section.
    fprintf(out_, "%s@PJL\n", uel);
  }
  fprintf(out_, "@PJL SET RESOLUTION = %d\n", s.resolution);
  fprintf(out_, "@PJL SET PAPERFEEDSPEED = FULL\n");
  fprintf(out_, "@PJL SET ECONOMODE = %s\n", options_.toner_save ? "ON" : "OFF");
  fprintf(out_, "@PJL SET LESSPAPERCURL=%s\n",
          options_.improve == improve_output::less_paper_curl ? "ON" : "OFF");
  fprintf(out_, "@PJL SET FIXINTENSITYUP=%s\n",
          options_.improve == improve_output::fix_intensity ? "ON" : "OFF");
  fprintf(out_, "@PJL SET TRANSFERLEVELUP=%s\n",
          options_.reduce_ghost ? "ON" : "OFF");
  fprintf(out_, "@PJL SET MEDIATYPE=%s\n", media_type_name(s.media_type));
  if (options_.density_set) {
    fprintf(out_, "@PJL SET DENSITY = %d\n", options_.density);
    fprintf(out_, "@PJL SET DEVBIASADJUST = %d\n", options_.density);
  }
  if (options_.sleep_minutes >= 0) {
    // DEFAULT changes the printer's stored setting, SET this job's.
    fprintf(out_, "@PJL DEFAULT AUTOSLEEP = ON\n");
    fprintf(out_, "@PJL DEFAULT TIMEOUTSLEEP = %d\n", options_.sleep_minutes);
    fprintf(out_, "@PJL SET AUTOSLEEP = ON\n");
    fprintf(out_, "@PJL SET TIMEOUTSLEEP = %d\n", options_.sleep_minutes);
  }
  fprintf(out_, "@PJL SET ORIENTATION = PORTRAIT\n");
  fprintf(out_, "@PJL SET PAPER = %s\n", s.paper.c_str());
  if (s.postcard) {
    // Postcards always go through the printer's thick paper path.
    fprintf(out_, "@PJL SET MEDIATYPE=THICK\n");
  }
  fprintf(out_, "@PJL SET PAGEPROTECT = AUTO\n");
  fprintf(out_, "@PJL ENTER LANGUAGE = PCL\n");
  fputs("\033E", out_);
  // 0 = simplex, 1 = long-edge binding, 2 = short-edge binding.
  fprintf(out_, "\033&l%dS", s.duplex ? (s.tumble ? 2 : 1) : 0);
  current_ = s;
  have_settings_ = true;
}

void hl_job::write_tray(int page_number) {
  // The first page may come from a different source (macOS "First page
  // from"). The second page then switches to the source for the rest of
  // the job, which later pages inherit.
  if (page_number == 1) {
    fputs(tray_command(options_.first_page_slot.empty()
                           ? options_.input_slot
                           : options_.first_page_slot), out_);
  } else if (page_number == 2 && !options_.first_page_slot.empty()) {
    fputs(tray_command(options_.input_slot), out_);
  }
}

void hl_job::write_raster(const page_header &header, int shift_bytes,
                          const std::vector<uint8_t> &pixels) {
  const int bpl = header.bytes_per_line;
  std::vector<uint8_t> line(shift_bytes + bpl);
  std::vector<uint8_t> reference(shift_bytes + bpl);
  auto load_line = [&](int y) {
    std::copy_n(pixels.begin() + (size_t)y * bpl, bpl,
                line.begin() + shift_bytes);
  };

  fputs("\033*b1030m", out_);
  if (header.lines > 0) {
    block block;
    load_line(0);
    block.add_line(encode_line(line));
    std::swap(line, reference);
    for (int y = 1; y < header.lines; ++y) {
      load_line(y);
      std::vector<uint8_t> encoded;
      if (y % lines_per_band == 0) {
        block.flush(out_);
        encoded = encode_line(line);
      } else {
        encoded = encode_line(line, reference);
        if (!block.line_fits(encoded.size())) {
          block.flush(out_);
          encoded = encode_line(line);
        }
      }
      block.add_line(std::move(encoded));
      std::swap(line, reference);
    }
    block.flush(out_);
  }
  fputs("1030M\f", out_);
}

bool hl_job::add_page(const page_header &header,
                      const std::vector<uint8_t> &pixels) {
  if (!started_) {
    write_job_start();
    started_ = true;
  }

  paper_selection paper = select_paper(header.width_pt, header.height_pt,
                                       header.yres);
  settings s;
  s.resolution = header.xres == 600 || header.xres == 1200 ? header.xres : 300;
  s.duplex = header.duplex;
  s.tumble = header.tumble;
  s.media_type = header.media_type;
  s.paper = paper.pjl_name;
  s.postcard = paper.pjl_name == "POSTCARD";
  if (!have_settings_ || !(current_ == s)) {
    write_settings(s);
  }

  if (options_.skip_blank && all_zero(pixels)) {
    return false;
  }

  ++pages_printed_;
  write_tray(pages_printed_);
  fprintf(out_, "\033&l%dX", std::max(1, header.copies));
  write_raster(header, paper.shift_bytes, pixels);
  fflush(out_);
  return true;
}

void hl_job::finish() {
  if (!started_ || finished_)
    return;
  finished_ = true;
  fprintf(out_, "%s@PJL\n", uel);
  fprintf(out_, "@PJL EOJ NAME=\"%s\"\n", title_.c_str());
  fputs(uel, out_);
  fflush(out_);
}
