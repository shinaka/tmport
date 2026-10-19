#pragma once
// What the DOS autoplayer sees (area autoplay, wave 5): the sprites on the screen.
//
// Each frame the game draws its actors from the draw records (g.sprites.records, Sprites_DrawSorted
// 4306:0497): a sprite picture of the loaded bank at a screen position. That draw list is what the
// screen shows, so the bot looks at it the way a player looks at the screen: it recognises an elf by
// its picture (the walking frames of the clue carriers differ from the other walkers', the dust
// throwers', the plain walkers' and the coin creature's; the frames come from the elf animation
// scripts of SST.EXE, read at start-up) and takes its position from where it was drawn. It never
// reads the elf table. Each recognised sprite is also checked against the pixels: the screen
// (g.screen, the presented picture) must differ from the background (g.back holds no sprites between
// frames) inside the sprite's rectangle, or the sprite does not count as seen.
//
// --autoplay-vision-check compares what the bot sees with the elf table (dos_autoplay.h).

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace tmport::dos {
class DosGame;
}

namespace tmport::dos::autoplay {

enum class SpriteKind : uint8_t {
    Other,
    Carrier,        // a walking clue carrier (scripts 0x41/0x42)
    Walker,         // a type-A walker (0x1F/0x20), or any type-A elf pausing (0x21/0x43 share a frame)
    Thrower,        // an elf-dust thrower walking or throwing (0x23-0x26)
    PlainWalker,    // a type-C walker (0x2D/0x2E)
    CoinCreature,   // the coin lying on the path (0x56) or running away (0x57/0x58)
    CaughtElf,      // an elf in the net or leaving it (0x2F-0x35, 0x45-0x4B, 0x4D/0x4E)
    FrozenElf,      // an elf of any kind standing still during a search (0x22)
};
std::string_view kindName(SpriteKind k);
bool isElf(SpriteKind k);

struct SeenSprite {
    int record = 0;            // draw record (sprite slot) index
    SpriteKind kind = SpriteKind::Other;
    uint16_t frame = 0xFFFF;   // frame code (PAK frame * 2 + mirror bit)
    int x = 0, y = 0;          // where it was drawn (screen bytes / rows, the slot position)
    int left = 0, top = 0, right = 0, bottom = 0;  // its rectangle inside the play field (pixels, exclusive end)
    int differing = 0;         // pixels of the rectangle where the screen differs from the background
};

class Vision {
public:
    // The elf frames of the animation scripts (the game's writable copy, as loaded).
    explicit Vision(const DosGame& g);

    // The kind a picture shows. A frame that several kinds use (the turning frame 97 of the walkers,
    // the throwers and the plain walkers; the pause frame 101 of a clue carrier and a walker) gives the
    // most specific of them; mayBe() tells whether a kind uses the frame at all.
    SpriteKind classify(uint16_t frameCode) const;
    bool mayBe(uint16_t frameCode, SpriteKind k) const;
    // The actors of the last drawn frame (draw records 1..12 with a frame), with their kind, their
    // rectangle and the differing pixels. Records of absolute frames (title, end sequence) are
    // reported as Other.
    std::vector<SeenSprite> look(const DosGame& g) const;
    // The elves among them that are really visible (at least minPixels differing pixels).
    std::vector<SeenSprite> elves(const DosGame& g, int minPixels = 12) const;
    // The player's draw record (record 1): its screen x in bytes, or -1 when not drawn.
    static int playerScreenX(const DosGame& g);

private:
    std::array<SpriteKind, 512> kindOfFrame_{};  // by PAK frame index (frame code >> 1)
    std::array<uint8_t, 512> kindsOfFrame_{};    // bit k: SpriteKind k uses the frame
};

}  // namespace tmport::dos::autoplay
