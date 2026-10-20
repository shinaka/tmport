#pragma once
// The rank and points helpers of ovl04 (2bd4, "helpers"; re/gg/docs/code_ui.md §11.2,
// ARCHITECTURE.md §6.4) that the sign-in rows and the results screens draw with: the rank of a
// points total (8 ranks over 7 thresholds DS:1f9c, names DS:1f7c) and the points with thousands
// commas.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace tmport::gg {

class GgExe;

// The rank tables of ssg.exe: u32 thresholds DS:1f9c (5000 .. 750000) and far names DS:1f7c.
struct RankTable {
    static constexpr int kRanks = 8;
    std::array<uint32_t, kRanks - 1> thresholds{};
    std::array<std::string_view, kRanks> names{};  // into the exe's DGROUP

    static RankTable fromExe(const GgExe& exe);

    // 2bd4:0225 Rank_FromPoints: the number of thresholds <= points (unsigned compare), counted
    // from the first until one is greater: 0..7.
    int rankOf(uint32_t points) const;
    std::string_view nameOf(uint32_t points) const { return names[static_cast<size_t>(rankOf(points))]; }
};

// 2bd4:024d Str_FormatThousands(n, buf): ultoa, then a ',' before every group of three digits (a
// switch on strlen - 1 that falls through). More than 7 digits (n >= 10 000 000) gives an empty
// string: the switch has no case for them [quirk kept; no player reaches it].
std::string formatThousands(uint32_t n);

}  // namespace tmport::gg
