#include "dos/data/help_file.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "dos/data/dgroup_offsets.h"
#include "util/error.h"

namespace tmport::dos {

namespace {

[[noreturn]] void bad(const std::string& why) { throw FormatError("HELP.DAT: " + why); }

}  // namespace

// 43cb:107a Help_LoadFile: read(handle, DS:9325, 0xED8)
HelpFile HelpFile::parse(std::span<const uint8_t> helpDat) {
    HelpFile h;
    h.buffer_.assign(kBufferSize, 0);
    std::copy_n(helpDat.begin(), std::min(helpDat.size(), kBufferSize), h.buffer_.begin());
    if (helpDat.size() < kTopics) bad("too short");
    for (int t = 0; t < kTopics; ++t) {
        h.counts_[static_cast<size_t>(t)] = helpDat[static_cast<size_t>(t)];
        h.total_ += helpDat[static_cast<size_t>(t)];
        if (helpDat[static_cast<size_t>(t)] == 0) bad("topic " + std::to_string(t) + " has no page");
    }
    if (h.total_ > kMaxPages) bad(std::to_string(h.total_) + " pages, the picture tables hold " + std::to_string(kMaxPages));
    const size_t textStart = kTopics + 2 * static_cast<size_t>(h.total_);
    for (int i = 0; i < h.total_; ++i) {
        const uint16_t off = h.pageOffset(i);
        if (off < textStart || off >= std::min(helpDat.size(), kBufferSize))
            bad("page " + std::to_string(i) + " outside the file");
        const auto first = h.buffer_.begin() + off;
        if (std::find(first, h.buffer_.end(), uint8_t{0}) == h.buffer_.end())
            bad("page " + std::to_string(i) + " is not NUL-terminated");
    }
    return h;
}

int HelpFile::pageIndex(int topic, int page) const {
    int idx = page;
    for (int t = 0; t < topic; ++t) idx += counts_.at(static_cast<size_t>(t));
    return idx;
}

uint16_t HelpFile::pageOffset(int index) const {
    if (index < 0 || index >= total_) throw std::out_of_range("help page index");
    const size_t at = kTopics + 2 * static_cast<size_t>(index);
    return static_cast<uint16_t>(buffer_[at] | (buffer_[at + 1] << 8));
}

std::string_view HelpFile::pageText(int index) const {
    const uint16_t off = pageOffset(index);
    const auto first = buffer_.begin() + off;
    const auto nul = std::find(first, buffer_.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(buffer_.data() + off), static_cast<size_t>(nul - first)};
}

// 43cb:10c4 Help_DrawPage, step 5
HelpPagePictures helpPagePictures(const DosExe& exe, int pageIndex) {
    if (pageIndex < 0 || pageIndex >= HelpFile::kMaxPages) throw std::out_of_range("help page index");
    const auto at = [&](uint16_t table) { return exe.dsS16(static_cast<uint16_t>(table + 2 * pageIndex)); };
    return {at(dg::kHelpPicFrame1), at(dg::kHelpPicFrame2), at(dg::kHelpPicX1), at(dg::kHelpPicX2), at(dg::kHelpPicY)};
}

}  // namespace tmport::dos
