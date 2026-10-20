#include "gg/ui/rank.h"

#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"

namespace tmport::gg {

RankTable RankTable::fromExe(const GgExe& exe) {
    RankTable t;
    for (size_t i = 0; i < t.thresholds.size(); ++i)
        t.thresholds[i] = exe.ds32(static_cast<uint16_t>(dg::kRankThresholds + 4 * i));
    for (size_t i = 0; i < t.names.size(); ++i) {
        const auto p = exe.dsFarPtr(static_cast<uint16_t>(dg::kRankNames + 4 * i));
        t.names[i] = exe.dsString(exe.dsOffsetOf(p));
    }
    return t;
}

// 2bd4:0225 Rank_FromPoints
int RankTable::rankOf(uint32_t points) const {
    int r = 0;
    while (r < kRanks - 1 && thresholds[static_cast<size_t>(r)] <= points) ++r;
    return r;
}

// 2bd4:024d Str_FormatThousands
std::string formatThousands(uint32_t n) {
    const std::string digits = std::to_string(n);
    if (digits.size() > 7) return {};
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        out += digits[i];
        const size_t left = digits.size() - 1 - i;  // digits still to come
        if (left > 0 && left % 3 == 0) out += ',';
    }
    return out;
}

}  // namespace tmport::gg
