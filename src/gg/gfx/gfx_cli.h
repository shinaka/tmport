#pragma once
// Headless tools of the G&G graphics area (no window, no sound): renders of the user's pictures,
// palettes, fonts, the final movie and composed screens as PNG files, for inspection and for pixel
// comparison with the reference decoders (tools/gg/lpx.py, fnt.py, movie.py) and the DOSBox-X
// captures (re/gg/docs/REFERENCE_CAPTURES.md). The ggport-cli dispatcher calls gfxCli with the
// program's arguments without argv[0]; it returns false when none of them is its command.
//
//   --gg-pic ID OUT.png [--pal ID]... [--opaque] [--scale N]
//        a picture (ID "1002", "0x1002" or a bundle member "0001/3"; a bundle id gives a sheet of its
//        pictures) with its palette as the game pairs them (fmt_graphics.md §4: the code's picture
//        and palette pairs, the level palettes, id + 1, then the bundle's palette members), over the
//        base palette DS:024e; --pal replaces the context palette. RGBA: pixels an RLE row skips and
//        a raw picture's key colour are transparent (--opaque: raw pictures unkeyed), as lpx.py.
//   --gg-pal ID|base OUT.png         a 16x16 swatch of a palette resource over the base palette
//                                    (colours outside its range dimmed), or of the base palette
//   --gg-font ID OUT.png [--scale N] a sheet of the font's glyphs (16 per row, ink black on white)
//   --gg-movie FIRST END DIR [--blank] [--pal ID]...
//        frames FIRST..END-1 of the final movie (ids, e.g. 3000 30ca) decoded in order into one VRAM
//        that starts with the results picture 0x1076 (--blank: colour 0), as movie_NNN.png (NNN =
//        id - 0x3000), shown through the base palette + 0x1077 (the results screen's; --pal replaces it)
//   --gg-title OUT.png [--phase 1|2|3] [--anim-step N,M] [--fade-dir DIR] [--scale N]
//        the title screen as Attract_TitleAnim (2c11:043f) draws it: picture 0x1002 with palette
//        0x1000, the picture set 0x1001, the title pictures of bundle 0x1028 of the phase (3: all
//        four), the two screen animations 0x1005 / 0x1007 at their N-th / M-th frame step (default
//        0 0), flushed to VRAM and shown through the faded-in DAC; --fade-dir also writes the
//        first frame's fade-in (Pal_FadeIn from black) as fade_NNN.png, one per video frame
//   --gg-fade in|out PIC PAL DIR      a full-screen picture with its palette faded in (Pal_FadeIn) or
//                                    out (Pal_FadeOut): one PNG per video frame (2 per palette load)
//   --gg-wipe ID DIR                 the wipe of pattern ID (W 0x11d6..0x11dd) from a black VRAM to a
//                                    white screen: wipe_NNN.png after each step (2 ticks each)
//   --gg-lpx-list                    TSV of every LPX picture (entries and bundle members): path,
//                                    format, size, key, data offset, bytes used, CRC-32 of the pixels
//                                    and of the opacity mask (the comparison with lpx.py)
//   --gg-movie-list [--blank]        TSV of the 202 movie frames decoded in order from the results
//                                    picture: size, bytes used, end code reached, end offset, CRC-32 of
//                                    VRAM after the frame (the comparison with movie.py)
//   --gg-font-list ID                TSV of a font's glyph records: code, y offset, width, rows, bytes,
//                                    CRC-32 of the glyph bitmap, advance
// Options: --data DIR (else GG_DATA, else next to the program). Output never goes into the game
// folder.

#include <string>
#include <vector>

namespace tmport::gg {

bool gfxCli(const std::vector<std::string>& args, int& exitCode);

}  // namespace tmport::gg
