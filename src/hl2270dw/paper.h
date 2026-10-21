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

#ifndef HL2270DW_PAPER_H
#define HL2270DW_PAPER_H

#include <string>

// Paper selection as the printer expects it.
//
// The printer is told the paper by name ("@PJL SET PAPER = LETTER"). The
// raster that macOS hands us covers only the printable area, so every line
// is shifted right by a small amount before it is sent. For the standard
// sizes the shift is a fixed 1/8 inch. A custom size is sent as the
// smallest standard size it fits in, and is centered within that paper's
// width (the HL-2270DW feeds paper center-aligned).
struct paper_selection {
  std::string pjl_name;   // value for "@PJL SET PAPER ="
  int shift_bytes;        // zero bytes to prepend to every raster line
};

// width_pt/height_pt are the page dimensions in points (cups PageSize),
// yres is the vertical resolution in dpi.
paper_selection select_paper(int width_pt, int height_pt, int yres);

#endif  // HL2270DW_PAPER_H
