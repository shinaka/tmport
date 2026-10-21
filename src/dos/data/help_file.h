#pragma once
// HELP.DAT of the DOS edition and the per-page pictures in SST.EXE, as the help pager of ui.c uses
// them (Help_LoadFile 43cb:107a, Help_DrawPage 43cb:10c4): 4 page counts (one per Help menu topic),
// u16 absolute page offsets, NUL-terminated page texts with Text_Draw markup. The original reads at
// most 0xED8 bytes into DS:9325. Specs: re/dos/docs/fmt_data.md §3, code_ui.md §6.1.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "dos/data/dos_exe.h"

namespace tmport::dos {

class HelpFile {
public:
    static constexpr int kTopics = 4;          // Help menu items, titles at DS:52ee
    static constexpr int kMaxPages = 15;       // entries of the page picture tables DS:5bed..5c65
    static constexpr size_t kBufferSize = 0xED8;

    // Keeps the first 0xED8 bytes (zero-padded, as the BSS buffer) and checks the counts, offsets
    // and that every page is NUL-terminated inside the buffer. Throws mischief::FormatError.
    static HelpFile parse(std::span<const uint8_t> helpDat);

    int pageCount(int topic) const { return counts_.at(static_cast<size_t>(topic)); }
    int totalPages() const { return total_; }
    // 43cb:10c4: the sum of the page counts of the earlier topics + page.
    int pageIndex(int topic, int page) const;
    uint16_t pageOffset(int index) const;
    // Text of a page (raw markup bytes) up to its NUL.
    std::string_view pageText(int index) const;

private:
    std::vector<uint8_t> buffer_;
    std::array<int, kTopics> counts_{};
    int total_ = 0;
};

// The pictures drawn on a help page (DS:5bed, 5c0b, 5c29, 5c47, 5c65; Help_DrawPage step 5): frame
// codes (frame * 2, -1 = none) of the loaded sprite bank, drawn with flags 0x40 at (x1, y) and (x2, y).
struct HelpPagePictures {
    int16_t frame1 = -1;
    int16_t frame2 = -1;
    int16_t x1 = 0;
    int16_t x2 = 0;
    int16_t y = 0;
};
HelpPagePictures helpPagePictures(const DosExe& exe, int pageIndex);

}  // namespace tmport::dos
