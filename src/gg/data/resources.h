#pragma once
// Bundles and the container layouts of the resource types (re/gg/docs/fmt_data.md §4-§7,
// fmt_graphics.md §2-§7, fmt_sound.md §2-§3). These accessors check and split a payload into its
// header fields and data; decoding (pictures, fonts, songs, DAM, the movie, the game tables) is the
// job of the modules that use them (gg/gfx, gg/audio, gg/ui, gg/game).
//
// Bundle (type 0x01): u16 off[0..n] (off[0] = 2 (n + 1), off[n] = payload length); member i =
// payload[off[i] .. off[i + 1]) = u8 type, u8 method (always 0), member payload. Bundles never
// nest; members have no ids (the docs write member m of bundle id as id/m).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gg/data/archive.h"

namespace tmport::gg {

// --- Bundles (fmt_data.md §4) ---------------------------------------------------------------------

struct BundleMember {
    int index = 0;
    uint8_t type = 0;
    uint8_t method = 0;
    size_t offset = 0;  // of the member's type byte in the bundle payload
    std::span<const uint8_t> payload;
    ResType resType() const { return static_cast<ResType>(type); }
};

// The members of a bundle payload (spans into `payload`). Throws tmport::FormatError if the
// offset table is broken or a member is shorter than its 2 header bytes.
std::vector<BundleMember> parseBundle(std::span<const uint8_t> payload);
// True when `payload` parses as a bundle table (what ggarc.is_bundle_payload checks).
bool looksLikeBundle(std::span<const uint8_t> payload);

// A loaded bundle that owns its bytes (members are re-derived on access, so copies stay valid).
class Bundle {
public:
    Bundle() = default;
    // Throws tmport::FormatError when the resource is not a bundle or does not parse.
    explicit Bundle(Resource res);
    static Bundle load(const Archive& archive, uint16_t id);

    uint16_t id() const { return res_.id; }
    size_t size() const { return offsets_.empty() ? 0 : offsets_.size() - 1; }
    BundleMember member(size_t i) const;
    std::vector<BundleMember> members() const;
    std::span<const uint8_t> bytes() const { return res_.bytes; }

private:
    Resource res_;
    std::vector<uint16_t> offsets_;
};

// --- LPX pictures, types G and P (fmt_graphics.md §2) ----------------------------------------------

struct LpxHeader {
    static constexpr size_t kSize = 14;  // the data offset is relative to +12
    uint16_t format = 0;                 // 0 raw, 1 RLE
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t key = 0;                    // the keyed raw blit compares with the low byte
    uint16_t dataOffset = 0;             // relative to +12: 2 in most pictures, 0 in 55 RLE frames
    std::span<const uint8_t> data;       // payload from +12 + dataOffset to the end
    bool rle() const { return format == 1; }
};
bool isLpx(std::span<const uint8_t> payload);
// Throws tmport::FormatError: no LPX magic, a format other than 0/1, data outside the payload, or a
// raw picture shorter than width * height.
LpxHeader parseLpx(std::span<const uint8_t> payload);

// --- Palette ranges, type P (fmt_graphics.md §3.1) -------------------------------------------------

struct PaletteRange {
    uint16_t byteOffset = 0;         // into the 768-byte DAC table (3 per colour)
    uint16_t byteCount = 0;
    std::span<const uint8_t> rgb;    // byteCount bytes of 6-bit R, G, B (bytes after them are ignored)
    int firstColor() const { return byteOffset / 3; }
    int colorCount() const { return byteCount / 3; }
};
bool isPaletteRange(std::span<const uint8_t> payload);
PaletteRange parsePaletteRange(std::span<const uint8_t> payload);

// --- Fonts, type F ids 9 and 10 (fmt_graphics.md §5) ---------------------------------------------

struct FontHeader {
    static constexpr size_t kSize = 12;
    uint16_t flags = 0;        // bit 0: 1-bpp glyphs (both fonts)
    uint16_t records = 0;      // glyph records, indexed by character code from 0
    uint16_t height = 0;       // line height = height + 1
    uint16_t spacing = 0;      // added to every glyph's width for the advance
    std::span<const uint8_t> glyphData;  // the glyph records (u16 y offset, u16 width, ...)
};
bool isFont(std::span<const uint8_t> payload);
FontHeader parseFont(std::span<const uint8_t> payload);

// --- DAM scripts, type D 0x114b..0x1156 (fmt_graphics.md §6) -------------------------------------

struct DamScript {
    std::span<const uint8_t> words;  // the 16-bit words after the magic (opcode 7FF8..7FFF, operands)
    size_t wordCount() const { return words.size() / 2; }
    uint16_t word(size_t i) const { return static_cast<uint16_t>(words[2 * i] | (words[2 * i + 1] << 8)); }
};
bool isDam(std::span<const uint8_t> payload);
DamScript parseDam(std::span<const uint8_t> payload);

// --- BMF songs, type S (fmt_sound.md §3) -----------------------------------------------------------

struct BmfSong {
    static constexpr size_t kBlockOffset = 0x10;  // the song block registered with the driver
    uint8_t songClass = 0;                        // 1 speaker, 3 OPL2 (block +0)
    std::span<const uint8_t> block;               // payload from +0x10 to the end
};
bool isBmf(std::span<const uint8_t> payload);
BmfSong parseBmf(std::span<const uint8_t> payload);

// --- Texts, types T, H and string S (fmt_data.md §6) -----------------------------------------------

// The bytes before the terminating NUL, FF escapes and control codes kept as they are. Throws
// tmport::FormatError when there is no NUL.
std::span<const uint8_t> textBytes(std::span<const uint8_t> payload);
// The same as a std::string of raw bytes (for logs and tools; not decoded).
std::string textString(std::span<const uint8_t> payload);
// A NUL-terminated printable string (what ggarc calls 'string' as opposed to 'struct').
bool isPrintableString(std::span<const uint8_t> payload);

// New Player Hint (H 0x1043..0x1055): `FF 06` designer's note `FF`, then the text.
struct HintText {
    std::span<const uint8_t> note;  // between FF 06 and FF (empty without a note)
    std::span<const uint8_t> text;  // after the note, up to the NUL
};
HintText parseHint(std::span<const uint8_t> payload);

// --- Integer arrays and records (types I, R, M, L, C) ---------------------------------------------

// Little-endian words of a payload (an odd last byte is dropped).
std::vector<uint16_t> u16Array(std::span<const uint8_t> payload);
std::vector<int16_t> i16Array(std::span<const uint8_t> payload);

// R race paths (fmt_data.md §7.4, code_vehicles_races.md §9.3): records {x, y, code}, code >= 0 a
// frame, negative codes commands; the last record has code -1. Throws tmport::FormatError when the
// size is not a multiple of 6 or the last code is not -1.
struct PathRecord {
    int16_t x = 0;
    int16_t y = 0;
    int16_t code = 0;
};
std::vector<PathRecord> parsePath(std::span<const uint8_t> payload);

// M members of music banks: `01 c c ... FF` = channels to mute for an arrangement
// (SndRes_SelectArrangement 1cdd:0423). The channels up to the FF (empty when the payload does not
// start with 1). `terminated` is false when no FF ends the list (the speaker movie bank's list 4
// ends in FD: the original then reads on into the next member, fmt_sound.md §2.1).
struct MuteList {
    std::vector<uint8_t> channels;
    bool terminated = false;
};
MuteList parseMuteList(std::span<const uint8_t> payload);

// --- Classification (as tools/gg/ggarc.py classify) ------------------------------------------------

// The kind of an entry or bundle member, by type letter, magic and place: "bundle", "picture",
// "palette", "font", "dam", "movie", "level", "song", "string", "struct", "text", "hint", "grid",
// "integer", "integrity", "table" or "unknown". `member` < 0 for an entry.
std::string_view classify(uint8_t type, std::span<const uint8_t> payload, uint16_t id, int member = -1);

// Printable form of a type byte: the letter, or two hex digits (bundles are "01").
std::string typeName(uint8_t type);

}  // namespace tmport::gg
