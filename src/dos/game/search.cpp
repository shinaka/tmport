// search.c (segment 4a03): the per-frame hiding-place search, the showcase treasure choice and
// hand-over at the end of a game, and the treasure shelf rooms (code_player.md §5).
#include "dos/game/search.h"

#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/game/sound.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/pak.h"

namespace tmport::dos {

namespace {

constexpr int kCollectedCount = 48;  // record +0x8A, g_collected DS:927E

// g_collected[i] (signed chars, 0x80 = empty). Index 48 is one past the list, record +0xBA, which
// the unbounded scans of the original reach only with a full list.
uint8_t& collectedAt(DosGame& g, int i) {
    UserRecord& r = g.rec();
    return i < kCollectedCount ? r.collected[static_cast<size_t>(i)] : r.unusedBA;
}

// g_cellContent / g_cellPicture of the searched cell (g_searchCell is in 0..255 in play; the strip
// ends would address the words around the tables, which no hiding place reaches).
uint16_t contentOf(const DosGame& g, unsigned cell) { return cell < 256 ? g.world.cellContent[cell] : 0; }
void setPicture(DosGame& g, unsigned cell, uint16_t v) {
    if (cell < 256) g.world.cellPicture[cell] = v;
}
void orContent(DosGame& g, unsigned cell, uint16_t bits) {
    if (cell < 256) g.world.cellContent[cell] |= bits;
}

void armHint(DosGame& g, int n) {
    uint8_t& h = g.elves.hintFlags[static_cast<size_t>(n)];
    if ((h & 0x80) == 0) h = 0;
}

// The opened picture of the searched place: bits 4-5 of attribute byte 0 of its closed picture in
// the level's table (4-byte records at DS:0184 / 0250 / 02EC) plus the level's first opened
// picture (DS:8FB4 = 0x32 for level 0, 0x26, 0x1F). Levels 3 and 4 write nothing.
void showOpenedPicture(DosGame& g) {
    static constexpr uint16_t kAttr[3] = {dg::kPictureAttrL0, dg::kPictureAttrL1, dg::kPictureAttrL2};
    const uint16_t level = g.core.level;
    if (level > 2) return;
    const int pic = static_cast<int8_t>(g.player.searchPicture);  // sign-extended byte
    const uint8_t a = g.exe().ds8(static_cast<uint16_t>(kAttr[level] + 4 * pic));
    const uint16_t base = level == 0 ? g.player.searchOpenBaseL0 : level == 1 ? 0x26 : 0x1F;
    setPicture(g, g.player.searchCell, static_cast<uint16_t>(((a & 0x30) >> 4) + base));
}

// The searched place's content as a sprite in slot 2: a child of the player at the coin's x
// (g_slot2X is still -25/+25) and 24 rows above his feet, drawn behind everything (z 0x50). A
// direct slot write (prev = 0 for the snapshot rule): it shows one frame later. The content is
// the frame code (treasure 6 + 2k = sprite 3 + k, keys 0x66/0x68/0x6A = sprites 51-53).
void showContent(DosGame& g) {
    const uint16_t c = contentOf(g, g.player.searchCell);
    if (c == 0 || (c & 0x80) != 0) return;
    SpriteSlot& s2 = g.player.slots[2];
    s2.y = -24;
    s2.z = 0x50;
    s2.frame = c;
    s2.owner = 1;
    s2.prev = 0;
}

// The result, on the frame where the timer is 1.
void searchResult(DosGame& g) {
    const unsigned cell = g.player.searchCell;
    const uint16_t c = contentOf(g, cell);
    // A content of exactly 0 would count as a treasure; the layout never leaves 0 in a hiding
    // place (empty = 0x80).
    if (static_cast<int16_t>(c) < 0x66 && (c & 0x80) == 0) {
        ++g.core.treasuresFound;
        // DS:9259 is the count the HUD drew last; it lags, so the hint is armed in practice.
        if (g.core.treasuresFound != g.ui.hudFound) armHint(g, 4);
        sfxPlay(g, 7);
        if (g.core.level < 3) {  // the lowest clear bit of the level's found mask (record +0x130)
            uint8_t& m = g.rec().foundMasks[g.core.level];
            uint8_t bit = 1;
            while (bit & m) bit = static_cast<uint8_t>(bit << 1);
            m |= bit;
        }
    }
    static constexpr struct {
        uint16_t content, keyState;
    } kKeys[] = {{0x66, 2}, {0x68, 4}, {0x6A, 6}};
    for (const auto& k : kKeys) {
        if (c != k.content) continue;
        g.core.keyState = k.keyState;
        g.ui.hudRedraw = 1;
        sfxPlay(g, 6);
        armHint(g, 9);
    }
    orContent(g, cell, 0x80);  // searched: a second search finds nothing (the picture looks unchanged)
    // The mirror copy of a cell shared by two strip rows (Layout_MirrorSharedCells) is marked too.
    if (cell > 0xE2 && cell < 0xE6) orContent(g, cell + 0x1A, 0x80);
    if (cell > 0xFC && cell < 0x100) orContent(g, cell - 0x1A, 0x80);
    if (cell > 0xAA && cell < 0xAE) orContent(g, cell + 0x15, 0x80);
    if (cell > 0xBF && cell < 0xC3) orContent(g, cell - 0x15, 0x80);
    if (cell > 0x76 && cell < 0x7A) orContent(g, cell + 0x0F, 0x80);
    if (cell > 0x85 && cell < 0x89) orContent(g, cell - 0x0F, 0x80);
    g.player.slots[2].y = 0;
    g.core.menuLock = 0;
    animClear(g, 2);
}

}  // namespace

// 4a03:0003 Search_Update
void searchUpdate(DosGame& g) {
    PlayerState& p = g.player;
    if (p.searchTimer < 1 || g.core.uiMode != 0) {
        p.searchSide = 0;
        return;
    }
    // The schedule counts frames (kept per frame in both frame speeds, PORTING_NOTES §2.3): timer
    // 57..42 blinks closed/opened every frame (the content sprite shows with the opened picture),
    // 41..3 holds the opened picture (2 per frame), 1 closes and gives the result: 37 frames.
    const int16_t t = p.searchTimer;
    if (t % 2 == 0) {  // an "open" frame
        showOpenedPicture(g);
        animClear(g, 2);
    } else if (t < 0x2A && t != 1) {  // held open
        if (g.core.slowCpu == 1 && t > 0x26) p.searchTimer = static_cast<int16_t>(p.searchTimer - 0x20);  // 21 frames
        --p.searchTimer;
        showContent(g);
    } else {  // a "closed" frame: t >= 0x2A odd, or t == 1
        setPicture(g, p.searchCell, static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(p.searchPicture))));
        showContent(g);
    }
    if (p.searchTimer == 1) searchResult(g);
    --p.searchTimer;
    ++g.world.fullRedraw;  // the searched cell's band is redrawn (Playfield_Redraw)
    ++g.world.framePartialRedraw;
}

// 4a03:038c Treasure_PickShowcase
void treasurePickShowcase(DosGame& g) {
    const auto contentAt = [&g](int slot) -> int16_t {
        return static_cast<int16_t>(g.world.cellContent[hidingCellAt(g, g.tables, slot) & 0xFF]);
    };
    const auto foundThisGame = [](int16_t c) { return c > 0x80 && c < 0xE6; };  // 0x81..0xE5
    const int shown = g.player.shownTreasure;  // normally -1 during a game (parked by the clubhouse door)
    const int first = static_cast<int8_t>(g.rec().collected[0]);
    int di = -2;  // -2: nothing found this game, -1: only "known" treasures found
    for (int si = 0; si < 48 && di < 0; ++si) {
        const int16_t c = contentAt(si);
        if (!foundThisGame(c)) continue;
        const int t = c & 0x7F;
        // Quirk kept (PORTING_NOTES §6): the loop over the 48 collected entries exits after its
        // first comparison, so only g_collected[0] is ever compared.
        if (t == first || t == shown) {
            di = -1;
        } else {
            di = si;
            g.player.showcasePick = static_cast<uint8_t>(c);  // bit 7 set: a new treasure
        }
    }
    if (di != -1) return;  // -2: g_showcasePick unchanged (0xFF after the last hand-over, 0 at start-up)
    // The last found treasure before the first one equal to the shown treasure. Quirk kept: if
    // the very first found one equals it, DI stays -1 and g_hidingCells[-1] = g_cellContent[255] is
    // read as the cell (hidingCellAt).
    for (int si = 0; si < 48; ++si) {
        const int16_t c = contentAt(si);
        if (!foundThisGame(c)) continue;
        if ((c & 0x7F) == static_cast<uint16_t>(shown)) break;
        di = si;
    }
    g.player.showcasePick = static_cast<uint8_t>(contentAt(di) & 0x7F);  // bit 7 clear
}

// 4a03:04a8 ShelfRoom_DrawShelf
void shelfRoomDrawShelf(DosGame& g) {
    WorldState& w = g.world;
    for (int room = 0; room < 4; ++room) {
        if (w.frameInShelfRoom[static_cast<size_t>(room)] != 1) continue;
        // Only the first flagged room is drawn per call: the redraw request is cleared below.
        if (w.playfieldRedrawn != 0) {
            g.back.setColor(0);  // 1000:18af: no dirty rectangles (the play-field was redrawn whole)
            const int base = static_cast<int8_t>(w.shelfRoomRedraw[static_cast<size_t>(2 * room + 1)]);  // 0, 12, 24, 36
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 6; ++c)
                    for (int k = 0; k < 2; ++k) {
                        // k == 0: the raised copy (4 rows higher, 4 px to the left), only where a
                        // duplicate was removed (the shelf bit); k == 1: the shelf slot itself.
                        int lift = 0;
                        if (k == 0) {
                            const int bit = 12 * room + 6 * r + c;
                            if ((g.rec().shelfBits[static_cast<size_t>(bit / 8)] & (1 << (bit % 8))) == 0) continue;
                            lift = 2;
                        }
                        const int i = 6 * r + c + base;
                        const int8_t a = static_cast<int8_t>(collectedAt(g, i));
                        const int8_t b = static_cast<int8_t>(collectedAt(g, i + 1));
                        if (!(a > 0 || b > 0)) continue;
                        int frame = -1;
                        if (a == static_cast<int8_t>(0x80)) {
                            if (k == 0) frame = g.player.shownTreasure;  // an empty slot shows the showcase copy
                        } else {
                            frame = a;
                        }
                        if (frame <= 0 || !g.bank || (frame >> 1) >= g.bank->count()) continue;
                        const int y = 0x28 * r + 0x2D - 2 * lift;
                        // Shelf x relative to camera 0x440 in the room entered from its left end
                        // (g_shelfCamRoom), else to 0x480 (16-bit unsigned shifts).
                        const int x = room == static_cast<int8_t>(w.shelfCamRoom)
                                          ? static_cast<int16_t>(0x2C - (static_cast<uint16_t>(w.cameraX - 0x440) >> 1) + 0x14 * c + 2 * k)
                                          : static_cast<int16_t>((static_cast<uint16_t>(0x480 - w.cameraX) >> 1) + 0x14 * c + 2 * k + 0x0C);
                        drawSprite(g.back, g.bank->frame(frame >> 1), x, y, sprite_flags::kMasked);
                    }
            g.back.setColor(1);
            w.shelfRoomRedraw[static_cast<size_t>(2 * room)] = 0;
        }
        w.playfieldRedrawn = 0;
    }
}

// 4a03:078c Treasure_AddCollected
void treasureAddCollected(DosGame& g) {
    uint8_t pick = g.player.showcasePick;
    if (pick == 0 || pick == 0xFF || pick == 0x80) return;
    int gap = 0;
    while (gap < kCollectedCount && collectedAt(g, gap) != 0x80) ++gap;  // the first free slot
    // The old showcase treasure goes to the shelf. Quirk kept: it is -1 while the player is out of
    // the clubhouse (parked in DS:9254), and is then replaced, not shelved.
    if (g.player.shownTreasure > 0) collectedAt(g, gap) = static_cast<uint8_t>(g.player.shownTreasure);
    if (pick & 0x80) {  // a new treasure: remove its duplicates from the shelves
        pick &= 0x7F;
        g.player.showcasePick = pick;
        for (int j = 0; j < kCollectedCount; ++j) {
            if (collectedAt(g, j) != pick) continue;
            g.rec().shelfBits[static_cast<size_t>(j / 8)] |= static_cast<uint8_t>(1 << (j % 8));
            collectedAt(g, j) = 0x80;
        }
    }
    g.player.shownTreasure = pick;
    g.player.storedShowcase = static_cast<int8_t>(pick);
    g.player.showcasePick = 0xFF;
}

}  // namespace tmport::dos
