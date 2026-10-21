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

#include "options.h"
#include <stdlib.h>
#include <strings.h>
#include <cups/cups.h>
#include <cups/ppd.h>

// The PPD API is deprecated but remains the only way to resolve a printer's
// default option values from inside a filter.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace {

class option_source {
 public:
  option_source(const char *options, const char *ppd_path)
      : num_options_(0), options_(nullptr), ppd_(nullptr) {
    if (options) {
      num_options_ = cupsParseOptions(options, 0, &options_);
    }
    if (ppd_path && *ppd_path) {
      ppd_ = ppdOpenFile(ppd_path);
      if (ppd_) {
        ppdMarkDefaults(ppd_);
        cupsMarkOptions(ppd_, num_options_, options_);
      }
    }
  }

  ~option_source() {
    if (ppd_)
      ppdClose(ppd_);
    cupsFreeOptions(num_options_, options_);
  }

  option_source(const option_source &) = delete;
  option_source &operator=(const option_source &) = delete;

  // A PPD option: the marked choice, else the job option, else `fallback`.
  std::string ppd_option(const char *name, const char *fallback) const {
    if (ppd_) {
      ppd_choice_t *choice = ppdFindMarkedChoice(ppd_, name);
      if (choice)
        return choice->choice;
    }
    const char *value = cupsGetOption(name, num_options_, options_);
    return value ? value : fallback;
  }

  // A job option that is not part of the PPD (for example the ones the
  // macOS Paper Feed pane adds).
  const char *job_option(const char *name) const {
    return cupsGetOption(name, num_options_, options_);
  }

 private:
  int num_options_;
  cups_option_t *options_;
  ppd_file_t *ppd_;
};

bool is_on(const std::string &value) {
  const char *v = value.c_str();
  return !strcasecmp(v, "ON") || !strcasecmp(v, "true")
      || !strcasecmp(v, "yes") || !strcmp(v, "1");
}

// A boolean job option given without a value ("-o ManualFeed") or with a
// true value.
bool flag_set(const char *value) {
  return value && (!*value || is_on(value));
}

// BRSleepTime accepts "Default", a number of minutes, or "Custom.N".
int parse_sleep(const std::string &value) {
  const char *v = value.c_str();
  if (!strncasecmp(v, "Custom.", 7))
    v += 7;
  if (!*v || !strcasecmp(v, "Default"))
    return -1;
  char *end;
  long minutes = strtol(v, &end, 10);
  if (*end != '\0' || minutes < 0 || minutes > 210)
    return 5;  // Same fallback as Brother's driver for invalid values.
  return (int)minutes;
}

}  // namespace

job_options parse_job_options(const char *user,
                              const char *title,
                              const char *options,
                              const char *ppd_path) {
  option_source src(options, ppd_path);
  job_options o;

  o.toner_save = is_on(src.ppd_option("TonerSaveMode", "OFF"));

  std::string improve = src.ppd_option("BRImproveOutput", "OFF");
  if (improve == "BRLessPaperCurl")
    o.improve = improve_output::less_paper_curl;
  else if (improve == "BRFixIntensity")
    o.improve = improve_output::fix_intensity;

  o.reduce_ghost = is_on(src.ppd_option("BRReduceGhost", "OFF"));
  o.sleep_minutes = parse_sleep(src.ppd_option("BRSleepTime", "Default"));
  o.skip_blank = is_on(src.ppd_option("BRSkipBlank", "OFF"));

  o.input_slot = src.ppd_option("InputSlot", "AutoSelect");
  if (flag_set(src.job_option("ManualFeed")))
    o.input_slot = "Manual";
  if (const char *first = src.job_option("AP_FIRSTPAGE_InputSlot"))
    o.first_page_slot = first;
  if (flag_set(src.job_option("AP_FIRSTPAGE_ManualFeed")))
    o.first_page_slot = "Manual";

  o.user = user ? user : "";
  o.title = title ? title : "";
  return o;
}

std::string pjl_safe_string(const std::string &s, size_t max_len) {
  std::string out;
  for (unsigned char c : s) {
    if (out.size() >= max_len)
      break;
    out += (c >= 32 && c < 127 && c != '"' && c != '\\') ? (char)c : '_';
  }
  return out;
}
