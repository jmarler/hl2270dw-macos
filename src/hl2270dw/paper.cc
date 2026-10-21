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

#include "paper.h"

namespace {

struct paper {
  const char *pjl_name;
  int width;   // points
  int height;  // points
};

// Every size the HL-2270DW accepts, sorted by width and then by height.
// The first entry a page fits in is the paper that gets selected, so the
// order matters.
const paper papers[] = {
  { "P3X5",      216,  360 },  // 3 x 5 in
  { "MONARCH",   279,  540 },  // Monarch envelope
  { "POSTCARD",  284,  419 },  // Hagaki
  { "A6",        297,  420 },
  { "COM10",     297,  684 },  // #10 envelope
  { "ENVYOU4",   298,  666 },  // Envelope #4 (You 4)
  { "DL",        312,  624 },  // DL envelope
  { "ENVCHOU3",  340,  666 },  // Envelope MAX (Chou 3)
  { "B6",        354,  499 },  // ISO B6
  { "A5",        420,  595 },
  { "C5",        459,  649 },  // C5 envelope
  { "B5",        499,  709 },  // ISO B5
  { "JISB5",     516,  729 },
  { "EXECUTIVE", 522,  756 },
  { "A5L",       595,  420 },  // A5 long edge
  { "A4",        595,  842 },
  { "LETTER",    612,  792 },
  { "FOLIO",     612,  936 },
  { "LEGAL",     612, 1008 },
  { "DLL",       624,  312 },  // DL long edge
};

// Paper width used for custom sizes that fit no standard paper.
const int max_paper_width = 612;

// The fixed part of the left shift, in 1/720 inch.
const int base_shift_720 = 90;

}  // namespace

paper_selection select_paper(int width_pt, int height_pt, int yres) {
  const char *name = "USERDEF";
  int paper_width = max_paper_width;
  for (const paper &p : papers) {
    if (width_pt <= p.width && height_pt <= p.height) {
      name = p.pjl_name;
      paper_width = p.width;
      break;
    }
  }
  // Half the unused paper width (in 1/720 inch) plus the fixed offset,
  // converted to device pixels and then to whole bytes.
  long shift_720 = (long)(paper_width - width_pt) * 5 + base_shift_720;
  long shift_bytes = shift_720 * yres / 5760;
  if (shift_bytes < 0)
    shift_bytes = 0;
  return paper_selection{ name, (int)shift_bytes };
}
