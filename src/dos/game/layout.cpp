// layout.c (segment 1a54) and Clue_BuildWords (46ce:079c): the new-game layout of the 44 hiding
// places (code_elves_layout_hints.md §5). Every rejection loop draws from g.rand exactly as the
// original does: the layout fixes the random sequence of everything after it (quiz deal, elves).
#include "dos/game/layout.h"

#include <ctime>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"

namespace tmport::dos {

namespace {

// The slots of each level in g_hidingCells DS:11AA (Layout_PutInSlot 1a54:0059); 11, 12, 27, 28
// are the mirror cells and never chosen.
constexpr unsigned kSlotFirst[3] = {29, 13, 0};
constexpr unsigned kSlotLast[3] = {47, 26, 10};
constexpr uint16_t kContentEmpty = 0x80;
constexpr uint16_t kContentKeyL0 = 0x66;  // + 2 * level
constexpr int kTreasureIds = 48;          // treasure k has content 6 + 2k

// Byte `offset` of a level's attribute table (g_pictureAttrTables DS:1246: 4 bytes per picture, so
// byte b of picture k is offset 4k + b).
uint8_t attrByte(const DosGame& g, int level, unsigned offset) {
    return g.exe().ds8(static_cast<uint16_t>(g.tables.world.pictureAttrOffset[static_cast<size_t>(level)] + offset));
}

// The player record fields the generator reads through far pointers (*g_totalTreasures DS:9138,
// g_collected DS:927E) and writes (g_clueWords DS:91FD). Game_NewSetup always has a record; without
// one the engine plays as a new player and drops the clue words.
uint16_t totalTreasures(const DosGame& g) { return g.hasRec() ? static_cast<uint16_t>(g.rec().totalTreasures) : 0; }
int8_t collectedAt(const DosGame& g, int i) {
    return g.hasRec() ? static_cast<int8_t>(g.rec().collected[static_cast<size_t>(i)]) : static_cast<int8_t>(0x80);
}

// The size rule of Layout_PlaceTreasures: the key's lowest-order non-zero 2-bit field value, found
// in any field of the candidate (value only, whichever dimension). Arithmetic shifts of the signed
// bytes as the original; only the low 2 bits are compared.
bool sizeMatch(uint8_t key, uint8_t cand) {
    for (int i = 0; (key & 3) == 0 && i < 3; ++i) key = static_cast<uint8_t>(static_cast<int8_t>(key) >> 2);
    for (int i = 0; (key & 3) != (cand & 3) && i < 3; ++i) cand = static_cast<uint8_t>(static_cast<int8_t>(cand) >> 2);
    return (key & 3) == (cand & 3);
}

// How many of the scheme's 3 attributes candidate record `c` shares with key record `k` (bytes
// b0..b3): number (schemes 0, 1), size b1 (0) / b3 (2), shape (1, 2: a key rectangle also matches a
// square), object (all).
int matchCount(int scheme, const uint8_t (&k)[4], const uint8_t (&c)[4]) {
    int m = 0;
    if ((scheme == 0 || scheme == 1) && (k[0] & 0x0F) == (c[0] & 0x0F)) ++m;
    if (scheme == 0 && sizeMatch(k[1], c[1])) ++m;
    if (scheme == 2 && sizeMatch(k[3], c[3])) ++m;
    if (scheme == 1 || scheme == 2) {
        const int ks = k[2] & 0xE0;
        const int cs = c[2] & 0xE0;
        if (ks == 0x80) {
            if (cs == 0x80 || cs == 0x60) ++m;
        } else if (ks == cs) {
            ++m;
        }
    }
    if ((k[2] & 0x1F) == (c[2] & 0x1F)) ++m;
    return m;
}

}  // namespace

// 1a54:0003 Layout_Generate
void layoutGenerate(DosGame& g) {
    g.rand.srand(layoutTimeSeed(g));  // the only srand of the program
    g.elves.clueScheme = static_cast<uint8_t>(g.rand.rand() % 3);
    for (int level = 0; level < 3; ++level) {
        layoutPlaceKey(g, level);
        layoutPlaceTreasures(g, level);
    }
    layoutMirrorSharedCells(g);
    castlePickLayout(g);
    castleApplyLayout(g);
    cellsAdjustForCpu(g);
}

// 4d1a:0050 time() as Turbo C 2.0 computes it (dostounix 4d2d:0009 with the default TZ
// "EST5EDT": timezone 18000 s, daylight on), for a local wall-clock date and time.
uint32_t turboCTime(int year, int month, int day, int hour, int minute, int second) {
    static constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // DS:8F4B
    constexpr int64_t kDay = 24 * 60 * 60;
    constexpr int64_t kTimezone = 5 * 60 * 60;
    int64_t t = 3652 * kDay + kTimezone;  // 1970-01-01 to 1980-01-01
    const int y = year - 1980;
    t += (y >> 2) * 1461 * kDay + (y & 3) * 365 * kDay;
    if ((y & 3) != 0) t += kDay;  // the leap day of the first year of the 4-year block
    int yday = 0;
    for (int m = 1; m < month && m <= 12; ++m) yday += kMonthDays[m - 1];
    yday += day - 1;
    if (month > 2 && (year & 3) == 0) ++yday;
    int hours = yday * 24 + hour;
    if (turboCIsDst(year - 1970, 0, static_cast<unsigned>(yday), static_cast<unsigned>(hour))) --hours;
    t += hours * int64_t{3600} + minute * 60 + second;
    return static_cast<uint32_t>(t);
}

// 4d63:01e5 __isDST(hour, yday, month, year since 1970) of Turbo C: US rule of 1987 (from the first
// Sunday of April, 2:00, to the last Sunday of October, 2:00), the last Sunday of April before 1987.
bool turboCIsDst(int year70, unsigned month, unsigned yday, unsigned hour) {
    static constexpr uint16_t kYdays[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};  // DS:8F64
    const bool leap = ((year70 + 70) & 3) == 0;
    if (month == 0) {
        unsigned temp = yday;
        if (yday > 58 && leap) --temp;
        while (month < 12 && kYdays[month] <= temp) ++month;
    } else {
        if (month < 3 || !leap) --yday;
        yday += kYdays[month - 1];
    }
    if (month < 4 || month > 10) return false;
    if (month != 4 && month != 10) return true;
    unsigned last = (year70 < 17 || month != 4) ? kYdays[month] : kYdays[3] + 7u;
    if (!leap) --last;
    const unsigned sunday =
        last - static_cast<uint16_t>(year70 * 365 + ((year70 + 1) >> 2) + static_cast<int>(last) + 4) % 7u;
    if (month == 4) return sunday < yday || (yday == sunday && hour > 1);
    return yday < sunday || (yday == sunday && hour < 2);
}

// The srand value of Layout_Generate (engine helper).
uint16_t layoutTimeSeed(const DosGame& g) {
    if (g.layoutSeedOverride) return *g.layoutSeedOverride;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return static_cast<uint16_t>(turboCTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                                            local.tm_min, local.tm_sec) &
                                 0xFFFF);
}

// 1a54:0059 Layout_PutInSlot
void layoutPutInSlot(DosGame& g, int picture, int content, int level) {
    if (level < 0 || level > 2) return;
    const auto& hc = g.tables.world.hidingCells;
    auto& pic = g.world.cellPicture;
    // rand() & 0x3F until a free slot of the level: a rejection loop (one rand() per try).
    unsigned s = 0;
    do {
        s = static_cast<unsigned>(g.rand.rand()) & 0x3F;
    } while (s < kSlotFirst[level] || s > kSlotLast[level] || pic[hc[s]] != 0);
    const uint16_t cell = hc[s];
    pic[cell] = static_cast<uint16_t>(picture);
    g.world.cellContent[cell] = static_cast<uint16_t>(content);
    if (level == 1 && g.world.cellContent[cell] == kContentKeyL0 + 2) g.elves.keyCellL1 = cell;
    if (level == 2 && g.world.cellContent[cell] == kContentKeyL0 + 4) g.elves.keyCellL2 = cell;
}

// 1a54:0179 Layout_PlaceKey
void layoutPlaceKey(DosGame& g, int level) {
    if (level < 0 || level > 2) return;
    auto& pd = g.world.levelPicData[static_cast<size_t>(level)];  // {count, key picture, -, places}
    const int scheme = g.elves.clueScheme;
    bool bad = false;
    do {
        bad = false;
        // The candidate is written to g_levelPicData[level][1] on every draw.
        do {
            do {
                pd[1] = static_cast<uint16_t>(g.rand.rand() & 0x3F);
            } while (pd[1] == 0);
        } while (pd[0] < pd[1]);
        g.elves.keyPicture = static_cast<int8_t>(pd[1]);
        // Reject a key without the attribute the scheme's clue needs (size b1, shape, size b3).
        const unsigned r = pd[1] * 4u;
        if (scheme == 0 && attrByte(g, level, r + 1) == 0) bad = true;
        if ((scheme == 1 || scheme == 2) && (attrByte(g, level, r + 2) & 0xE0) == 0) bad = true;
        if (scheme == 2 && attrByte(g, level, r + 3) == 0) bad = true;
    } while (bad);
    layoutPutInSlot(g, g.elves.keyPicture, kContentKeyL0 + 2 * level, level);
    clueBuildWords(g, level * 3);
}

// 1a54:029a Layout_PlaceTreasures
void layoutPlaceTreasures(DosGame& g, int level) {
    if (level < 0 || level > 2) return;
    const auto& pd = g.world.levelPicData[static_cast<size_t>(level)];
    const uint16_t count = pd[0];
    if (count == 0) return;  // a division by zero in the original; not in the shipped tables
    // Byte offset of the candidate record; the first one tried is record (r % count) + 1.
    uint16_t off = static_cast<uint16_t>((static_cast<unsigned>(g.rand.rand()) % count) << 2);
    // Treasures per level t and the tier from the player's total (unsigned compares); below 25 the
    // tier keeps its record value.
    const uint16_t total = totalTreasures(g);
    int t = 2;
    if (total >= 0xE6) {
        t = 5;
        g.core.tier = 3;
    }
    if (total >= 0x73 && total < 0xE6) {
        t = 4;
        g.core.tier = 2;
    }
    if (total >= 0x19 && total < 0x73) {
        t = 3;
        g.core.tier = 1;
    }
    if (total < 0x19) t = 2;
    int decoys = 0;
    if (level == 0) decoys = t + 3;
    if (level == 1) decoys = t + 2;
    if (level == 2) decoys = total >= 0x17D ? t : t + 1;

    const int scheme = g.elves.clueScheme;
    const unsigned keyRec = pd[1] * 4u;
    const uint8_t key[4] = {attrByte(g, level, keyRec), attrByte(g, level, keyRec + 1),
                            attrByte(g, level, keyRec + 2), attrByte(g, level, keyRec + 3)};
    const auto& hc = g.tables.world.hidingCells;
    int want = 2;  // treasures match 2 of the 3 clue attributes, decoys 1, the other places 0
    for (int i = 0; i < static_cast<int>(pd[3]); ++i) {
        if (t <= i && i < decoys + t) want = 1;
        if (decoys + t <= i) want = 0;
        // The next candidate (cyclic over records 1..count; candidates are not used up, so a class
        // with fewer pictures than places repeats pictures).
        int m = -1;
        do {
            if (count * 4u > off)
                off = static_cast<uint16_t>(off + 4);
            else
                off = 4;
            const uint8_t cand[4] = {attrByte(g, level, off), attrByte(g, level, off + 1u), attrByte(g, level, off + 2u),
                                     attrByte(g, level, off + 3u)};
            m = matchCount(scheme, key, cand);
        } while (m != want);
        if (i < t) {
            // A treasure id not collected, not on show and not placed yet, cycling from a random
            // start; after 48 tries the id is taken anyway (it then equals the first candidate).
            int tries = 0;
            int k = g.rand.rand() % kTreasureIds;
            int e = 0;
            do {
                ++tries;
                k = k < kTreasureIds - 1 ? k + 1 : 0;
                const int v = 2 * k + 6;
                e = 0;
                while (e != kTreasureIds && v != collectedAt(g, e) && v != g.player.shownTreasure &&
                       v != static_cast<int16_t>(g.world.cellContent[hc[static_cast<size_t>(e)]]))
                    ++e;
            } while (e != kTreasureIds && tries != kTreasureIds);
            layoutPutInSlot(g, off / 4, 2 * k + 6, level);
        } else {
            layoutPutInSlot(g, off / 4, kContentEmpty, level);
        }
    }
}

// 1a54:06dc Layout_MirrorSharedCells
void layoutMirrorSharedCells(DosGame& g) {
    auto& pic = g.world.cellPicture;
    auto& con = g.world.cellContent;
    static constexpr int kPairs[3][2] = {{0xE3, 0xFD}, {0xAB, 0xC0}, {0x77, 0x86}};  // source -> mirror, 3 cells each
    for (const auto& pr : kPairs) {
        for (int i = 0; i < 3; ++i) {
            pic[static_cast<size_t>(pr[1] + i)] = pic[static_cast<size_t>(pr[0] + i)];
            con[static_cast<size_t>(pr[1] + i)] = con[static_cast<size_t>(pr[0] + i)];
        }
    }
}

// 1a54:078b Layout_UnmarkSharedCells
void layoutUnmarkSharedCells(DosGame& g) {
    // Quirk kept: & 0x7F also turns an empty place (0x80) into 0.
    for (int first : {0xFD, 0xC0, 0x86})
        for (int i = 0; i < 3; ++i) g.world.cellContent[static_cast<size_t>(first + i)] &= 0x7F;
}

// 1a54:07d6 Layout_ClearSlots
void layoutClearSlots(DosGame& g) {
    for (const uint16_t cell : g.tables.world.hidingCells) {
        g.world.cellPicture[cell] = 0;
        g.world.cellContent[cell] = kContentEmpty;
    }
}

// 1a54:0803 Cells_AdjustForCpu
void cellsAdjustForCpu(DosGame& g) {
    auto& pic = g.world.cellPicture;
    auto& type = g.world.cellType;
    if (g.core.slowCpu == 0) {
        pic[0xBF] = 0x41;
        pic[0x7E] = 0x37;
        // The two obstacle cells become animated (type 45) unless already changed in play.
        if (pic[0x84] == 0x2E) {
            pic[0x84] = 0x2F;
            type[0x84] = 0x2D;
        }
        if (pic[0xB7] == 0x2E) {
            pic[0xB7] = 0x2F;
            type[0xB7] = 0x2D;
        }
    } else {
        pic[0xBF] = 0x40;
        pic[0x7E] = 0x2E;
        pic[0x84] = 0x2E;
        pic[0xB7] = 0x2E;
        type[0x84] = 0x1C;
        type[0xB7] = 0x1C;
    }
}

// 1a54:085f Cells_ResetVariable
void cellsResetVariable(DosGame& g) {
    const auto& t = g.tables.world;
    for (size_t j = 0; j < t.variableCells.size(); ++j) {
        g.world.cellPicture[t.variableCells[j]] = t.variableCellDefaults[j];
        g.world.cellContent[t.variableCells[j]] = kContentEmpty;
    }
}

// 1a54:0892 Castle_PickLayout
int castlePickLayout(DosGame& g) {
    if (g.core.tier <= 3) g.world.castleLayout = static_cast<uint8_t>(g.core.tier + 1);
    // One rand() even at tier 0 (where the result is 0). With a tier above 3 the old value is the
    // divisor; a zero there would fault in the original (unreachable), the engine keeps 0.
    const int r = g.rand.rand();
    const int div = static_cast<int8_t>(g.world.castleLayout);
    g.world.castleLayout = static_cast<uint8_t>(div != 0 ? r % div : 0);
    return static_cast<int8_t>(g.world.castleLayout);
}

// 1a54:08dc Castle_ApplyLayout
void castleApplyLayout(DosGame& g) {
    const auto& layouts = g.tables.world.castleLayouts;
    const int layout = static_cast<int8_t>(g.world.castleLayout);
    if (layout < 0 || layout >= static_cast<int>(layouts.size())) return;  // never in play (0..3)
    size_t p = 0;
    for (int row = 0x32; row < 0x73; row += 0x10)
        for (int c = 0; c < 4; ++c) g.world.cellPicture[static_cast<size_t>(row + c)] = layouts[static_cast<size_t>(layout)][p++];
}

// 46ce:079c Clue_BuildWords
void clueBuildWords(DosGame& g, int base) {
    const ElfTables& t = g.tables.elves;
    const int level = base / 3;
    if (level < 0 || level > 2) return;  // only called with 0, 3, 6
    // The attribute tables by level are fixed addresses here (DS:0184 / 0250 / 02EC, the targets of
    // g_pictureAttrTables).
    static constexpr uint16_t kAttr[3] = {dg::kPictureAttrL0, dg::kPictureAttrL1, dg::kPictureAttrL2};
    const int key = g.elves.keyPicture;
    for (int a = 2; a != -1; --a) {
        const int at = static_cast<int8_t>(t.clueSchemes[g.elves.clueScheme][static_cast<size_t>(a)]);
        const auto byteAt = static_cast<uint16_t>(kAttr[level] + t.clueAttrByte[static_cast<size_t>(at)] + key * 4);
        const uint8_t b = g.exe().ds8(byteAt);
        uint8_t v = 0;
        if (at < 3) {
            // Number, shape or object: no rand().
            v = static_cast<uint8_t>((b & t.clueAttrMask[static_cast<size_t>(at)]) >> (t.clueAttrShift[static_cast<size_t>(at)] & 0x1F));
        } else {
            // A size: a random non-zero 2-bit field f of b1 / b3, word 2f + value (Layout_PlaceKey
            // guarantees a non-zero byte, so the loop ends).
            int f = 0;
            do {
                f = g.rand.rand() % 4;
                v = static_cast<uint8_t>((b & t.clueSizeMask[static_cast<size_t>(f)]) >> (t.clueSizeShift[static_cast<size_t>(f)] & 0x1F));
            } while (v == 0);
            v = static_cast<uint8_t>(f * 2 + v);
        }
        // Bit 7: not revealed yet.
        const auto word = static_cast<uint8_t>((t.clueWordBase[static_cast<size_t>(at)] + v - 1) | 0x80);
        if (g.hasRec()) g.rec().clueWords[static_cast<size_t>(base + a)] = word;
    }
}

}  // namespace tmport::dos
