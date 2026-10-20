#pragma once
// The part catalogue of segment 153f, its data half (re/gg/docs/code_vehicles_races.md §3,
// code_world.md §4.3, §7.2): the level description 'D' 0x2f5 + race index with its part records
// and Morty's vehicle, the HUD icon and name of every part, and the deck that deals parts and banana
// crates to the rooms (Parts_ShuffleDeck / Parts_DealNext, the first rand() calls of a building
// set-up). The inventory rules and the part lists are wave 3's.
//
//   +0x00 building   +0x02 n parts   +0x04 categories   +0x06 k needed categories
//   +0x08 workshop palette   +0x0A sprite bundle of the HUD icons   +0x0C paint width
//   +0x0E u16[4] paint colour bases   +0x16 u8[15] paintable groups   +0x25, +0x29 far (patched)
//   +0x2D u16[k] (unused)   then n part records of 0x8E bytes, then Morty's parts (i16, -1 ends)

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gg/gfx/sprites.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class PicCache;

// A part record (0x8E bytes; code_vehicles_races.md §3.2). The file fields and the run-time fields
// the world and the HUD use.
struct PartRecord {
    static constexpr size_t kSize = 0x8E;
    static constexpr uint16_t kNoPoints = 0x4000;  // +0x36: recycled once, no points on pick-up
    static constexpr uint16_t kInstalled = 0x8000; // +0x36: built into the vehicle

    int16_t category = 0;      // +0x08
    int16_t variant = 0;       // +0x0A
    int16_t holder = -1;       // +0x0C: -1 none, 0 the player, 2..11 a chimp
    uint16_t required = 0;     // +0x0E
    uint16_t iconMember = 0;   // +0x10: member of the catalogue's sprite bundle
    uint16_t shelfPicture = 0; // +0x12
    uint16_t vehiclePicture = 0; // +0x14
    uint16_t nameId = 0;       // +0x16: 'S' string resource
    uint16_t instances = 0;    // +0x18
    int16_t pair = -1;         // +0x1C
    std::array<int16_t, 4> instX{}, instY{}, instLayer{};  // +0x1E, +0x26, +0x2E
    uint16_t flags = 0;        // +0x36
    uint8_t bodyColour = 0;    // +0x38
    Sprite hudSprite;          // +0x3A: the HUD strip sprite (x = -1: not on the strip)
    std::span<const uint8_t> icon;  // +0x82: the HUD icon (picture iconMember of the bundle)
    std::vector<uint8_t> name;      // +0x86: Res_LoadCopy(nameId)
    int index = 0;             // +0x8C
};

class PartCatalogue {
public:
    static constexpr uint16_t kFirstId = 0x02F5;  // + race index 0..14

    // The fields of the file (no resources loaded). Throws tmport::FormatError for a record table
    // that does not fit.
    static PartCatalogue parse(std::span<const uint8_t> payload);
    // 153f:0002 Parts_LoadCatalogue: the description of `raceIndex`, the HUD icon bundle loaded into
    // the cache (its palette members applied), every record's icon, name and own index.
    static PartCatalogue load(const Archive& archive, PicCache& cache, int raceIndex);

    int building = 0;            // +0x00
    int categories = 0;          // +0x04
    int neededCount = 0;         // +0x06 k (Parts_NeededCount 153f:0316)
    uint16_t workshopPalette = 0; // +0x08
    uint16_t iconBundle = 0;     // +0x0A
    int paintWidth = 0;          // +0x0C
    std::array<uint16_t, 4> paintBase{};   // +0x0E
    std::array<uint8_t, 15> paintable{};   // +0x16
    std::vector<uint16_t> categoryList;    // +0x2D (unused by the game)
    std::vector<PartRecord> parts;         // +0x2D + 2k
    std::vector<int16_t> morty;            // Morty's vehicle (without the -1)

    size_t count() const { return parts.size(); }  // +0x02
};

// The deck g_partDeck DS:659c and g_partDealIndex DS:6614 (153f:01b6, 153f:0203).
class PartDeck {
public:
    static constexpr int kCrate = 1000;  // a banana crate

    // 153f:01b6 Parts_ShuffleDeck(total): Rand_Shuffle(total) (2bd4:0000: entries 0..total-1, then
    // 10 * total swaps of R(total), R(total), two rand() calls per swap), every entry >= the part
    // count becomes 1000; the deal starts at 0.
    void shuffle(int total, int partCount, BorlandRand& rng);
    // 153f:0203 Parts_DealNext (no bounds check in the original; past the end the engine deals 1000).
    int dealNext();

    const std::vector<int>& entries() const { return deck_; }
    int dealIndex() const { return next_; }

private:
    std::vector<int> deck_;
    int next_ = 0;
};

// 2bd4:0000 Rand_Shuffle(n, out): out[i] = i, then 10 * n swaps of out[R(n)] and out[R(n)].
void randShuffle(std::span<int> out, BorlandRand& rng);

}  // namespace tmport::gg
