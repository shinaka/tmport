#pragma once
// USERS.DAT of the 1990 DOS edition: the sign-in roster and the saved games of 9 players
// (re/dos/docs/fmt_data.md §4, code_ui.md §9.7-9.9, PORTING_NOTES.md §5, tools/dos/users_dos.py).
//
// File = 3-byte header + 9 records of 0x168 (360) bytes at 3 + 0x168 * i (offset table DS:6132)
// = 0xCAB = 3243 bytes. The original reads the whole file into DS:ba14 (Users_ReadFile 47c7:0514)
// and always writes it whole (Users_WriteFile 47c7:055a). Every byte of the file is a typed field
// below, so parse() followed by bytes() reproduces any file byte for byte.
//
// Header: byte 0 is never referenced; bytes 1-2 are one u16, loaded into DS:929F by Game_Restore
// (47c7:0de0 MOV AX,[ba15]; MOV [929f],AX) and stored back by Users_Pack (47c7:0f80). DS:929F is
// the high-water mark Sprites_DrawSorted (4306:0664) keeps of the sprite save-under offset.
//
// Record field names follow code_ui.md §9.8 and re/names/dos/dgroup.tsv (the global each field is
// copied to or used through). Fields used in place through far pointers (Users_SelectRecord
// 47c7:09dd): name, +0x09, +0x0A, +0x0C, +0x66, +0x78, +0x81, +0x8A, +0x130, +0x133, +0x13F.
// Fields copied by Users_Pack (47c7:0e0e) / Game_Restore (47c7:0c62): +0x0BB..+0x15A (except
// +0x15A/+0x15C) and +0x15E..+0x166.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::dos {

inline constexpr size_t kUsersHeaderSize = 3;
inline constexpr size_t kUserRecordSize = 0x168;  // 360
inline constexpr int kUserRecordCount = 9;
inline constexpr size_t kUsersFileSize = kUsersHeaderSize + kUserRecordSize * kUserRecordCount;  // 0xCAB = 3243
inline constexpr size_t kUserNameSize = 9;  // char[9]: at most 8 characters + NUL
inline constexpr size_t kUserMaxNameLength = 8;
inline constexpr int kQuizQuestions = 360;   // 2 history bits each (ALL_PUZS, fmt_data.md §2.3)
inline constexpr int kHidingCells = 48;      // g_hidingCells DS:11aa
inline constexpr int kVariableCells = 9;     // g_variableCells DS:120a
inline constexpr int kQuizSlotCount = 9;

// Field offsets inside a record.
namespace urec {
inline constexpr size_t kName = 0x000;              // char[9]
inline constexpr size_t kSavedGame = 0x009;         // u8: 0 = start a new game, else continue (Game_Start 47c7:08b5)
inline constexpr size_t kTotalTreasures = 0x00A;    // i16 (DS:9138 points here): sort key, rank
inline constexpr size_t kQuizHistory = 0x00C;       // u8[90]: 2 bits per question (DS:8fd2)
inline constexpr size_t kQuizSlots = 0x066;         // u16[9]: dealt question, 0xFFFF none, bit 15 solved (DS:921a)
inline constexpr size_t kAnswerMasks = 0x078;       // u8[9]: answers still offered, 7 = all (DS:9107)
inline constexpr size_t kClueWords = 0x081;         // u8[9]: 3 levels x 3 word indices, bit 7 = not earned (DS:91fd)
inline constexpr size_t kCollected = 0x08A;         // u8[48]: collected treasure ids, 0x80 none (DS:927e)
inline constexpr size_t kUnusedBA = 0x0BA;          // u8: never referenced
inline constexpr size_t kHidingPictures = 0x0BB;    // u8[48]: low byte of g_cellPicture[g_hidingCells[i]] (DS:0caa)
inline constexpr size_t kVariablePictures = 0x0EB;  // u8[9]: low byte of g_cellPicture[g_variableCells[j]]
inline constexpr size_t kHidingContents = 0x0F4;    // u8[48]: low byte of g_cellContent[g_hidingCells[i]] (DS:0faa)
inline constexpr size_t kLevelPicWords = 0x124;     // u16[6]: g_levelPicData DS:122e[{1,2,5,6,9,10}] (DS:62ab/62b1)
inline constexpr size_t kFoundMasks = 0x130;        // u8[3]: found-treasure bits per level (DS:9209)
inline constexpr size_t kShelfBits = 0x133;         // u8[12]: collected-treasure shelf bits (DS:8fb6)
inline constexpr size_t kQuizTier = 0x13F;          // u8: question level 0..3, then 5 (DS:9101)
inline constexpr size_t kTier = 0x140;              // u16 g_tier DS:9130
inline constexpr size_t kLevel = 0x142;             // u16 g_level DS:928f
inline constexpr size_t kCoins = 0x144;             // u16 g_coins DS:9134
inline constexpr size_t kNets = 0x146;              // u16 g_nets DS:910f
inline constexpr size_t kTreasuresFound = 0x148;    // u16 g_treasuresFound DS:9113
inline constexpr size_t kKeyState = 0x14A;          // u16 g_keyState DS:9120
inline constexpr size_t kPromptIcon = 0x14C;        // u16 g_promptIcon DS:90f8
inline constexpr size_t kPlayerX = 0x14E;           // i16 DS:37e2
inline constexpr size_t kPlayerY = 0x150;           // i16 DS:37e4
inline constexpr size_t kCameraX = 0x152;           // i16 g_cameraX DS:37ea
inline constexpr size_t kCameraY = 0x154;           // i16 g_cameraY DS:37ec
inline constexpr size_t kArea = 0x156;              // u16 g_area DS:910b
inline constexpr size_t kShopCoinsOwed = 0x158;     // i16 g_shopCoinsOwed DS:92a1
inline constexpr size_t kBankedNets = 0x15A;        // u16: nets for the next game (Game_End 47c7:09a2)
inline constexpr size_t kBankedCoins = 0x15C;       // u16: coins for the next game (Game_End)
inline constexpr size_t kMusicOn = 0x15E;           // u16 g_musicOn DS:63b6
inline constexpr size_t kSoundOn = 0x160;           // u16 g_soundOn DS:63b4
inline constexpr size_t kGamesPlayed = 0x162;       // u16 g_gamesPlayed DS:926e
inline constexpr size_t kShownTreasure = 0x164;     // i16 g_shownTreasure DS:8fb0
inline constexpr size_t kCastleLayout = 0x166;      // i16: sign-extended g_castleLayout DS:8f9e (low byte restored)

struct FieldInfo {
    size_t offset;
    size_t size;
    std::string_view name;
};
// Every field in file order; the entries tile the record (tests and dumps use it).
extern const std::array<FieldInfo, 36> kFields;
}  // namespace urec

// One player record, every field typed. Words are little-endian in the file.
struct UserRecord {
    std::array<uint8_t, kUserNameSize> name{};  // NUL-terminated; empty = free slot
    uint8_t savedGame = 0;
    int16_t totalTreasures = 0;
    std::array<uint8_t, 90> quizHistory{};
    std::array<uint16_t, kQuizSlotCount> quizSlots{};
    std::array<uint8_t, kQuizSlotCount> answerMasks{};
    std::array<uint8_t, 9> clueWords{};
    std::array<uint8_t, kHidingCells> collected{};
    uint8_t unusedBA = 0;
    std::array<uint8_t, kHidingCells> hidingPictures{};
    std::array<uint8_t, kVariableCells> variablePictures{};
    std::array<uint8_t, kHidingCells> hidingContents{};
    std::array<uint16_t, 6> levelPicWords{};  // per level: key picture, unused word
    std::array<uint8_t, 3> foundMasks{};
    std::array<uint8_t, 12> shelfBits{};
    uint8_t quizTier = 0;
    uint16_t tier = 0;
    uint16_t level = 0;
    uint16_t coins = 0;
    uint16_t nets = 0;
    uint16_t treasuresFound = 0;
    uint16_t keyState = 0;
    uint16_t promptIcon = 0;
    int16_t playerX = 0;
    int16_t playerY = 0;
    int16_t cameraX = 0;
    int16_t cameraY = 0;
    uint16_t area = 0;
    int16_t shopCoinsOwed = 0;
    uint16_t bankedNets = 0;
    uint16_t bankedCoins = 0;
    uint16_t musicOn = 0;
    uint16_t soundOn = 0;
    uint16_t gamesPlayed = 0;
    int16_t shownTreasure = 0;
    int16_t castleLayout = 0;

    static UserRecord fromBytes(std::span<const uint8_t, kUserRecordSize> bytes);
    void toBytes(std::span<uint8_t, kUserRecordSize> out) const;

    // A record whose name starts with NUL is a free slot (roster 47c7:0452, list 47c7:01e0).
    bool empty() const { return name[0] == 0; }
    // The name bytes up to the first NUL (at most 9).
    std::string nameString() const;
    // Users_AddPlayer (47c7:07b9) copies the 9-byte name buffer: up to 8 characters, the rest NUL.
    void setName(std::string_view s);

    // 2-bit status of question i (0..359): 0 unseen, 1 right first time, 2 right after a miss,
    // 3 missed (fmt_data.md §2.3). Bits (i & 3) * 2 of byte i >> 2.
    uint8_t history(int question) const;
    void setHistory(int question, uint8_t status);

    // Users_ClearRecord (47c7:075d): 360 zero bytes, then the answer masks +0x78..+0x80 = 7.
    void clear();

    bool operator==(const UserRecord&) const = default;
};

struct UsersDat {
    uint8_t header0 = 0;            // file byte 0, never referenced
    uint16_t spriteHighWater = 0;   // file bytes 1-2 <-> DS:929F
    std::array<UserRecord, kUserRecordCount> records{};

    // A complete file: at least 3243 bytes (bytes after them are ignored); nullopt otherwise.
    static std::optional<UsersDat> parse(std::span<const uint8_t> file);
    // Users_ReadFile (47c7:0514) semantics: the image is zeroed, then up to 3243 bytes are read
    // over it (a short file leaves the rest zero). `complete` tells whether the file was whole.
    static UsersDat fromImage(std::span<const uint8_t> file, bool* complete = nullptr);
    // The 3243 bytes the original writes.
    std::vector<uint8_t> bytes() const;

    // File offset of record i (DS:6132).
    static constexpr size_t recordOffset(int i) { return kUsersHeaderSize + kUserRecordSize * static_cast<size_t>(i); }

    // Users_EnsureLoaded (47c7:0452) result: any record with a non-empty name.
    bool anyPlayer() const;
    // Number of non-empty records (the sign-in list's c6cc, before sorting).
    int playerCount() const;
    // Users_AddPlayer (47c7:07b9): the first record with an empty name, or -1 when all 9 are used.
    int firstFreeRecord() const;
    // Users_NameExists (47c7:04a4): trailing blanks and NULs of the typed name are dropped, then
    // the name is compared with every non-empty record name ignoring ASCII case (Turbo C stricmp).
    // Returns the record index of the match, or -1.
    int findName(std::string_view typed) const;
    bool nameExists(std::string_view typed) const { return findName(typed) >= 0; }

    bool operator==(const UsersDat&) const = default;
};

}  // namespace tmport::dos
