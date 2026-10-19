#include "dos/save/users_dat.h"

#include <algorithm>
#include <cstring>

namespace tmport::dos {

namespace urec {
const std::array<FieldInfo, 36> kFields = {{
    {kName, 9, "name"},
    {kSavedGame, 1, "savedGame"},
    {kTotalTreasures, 2, "totalTreasures"},
    {kQuizHistory, 90, "quizHistory"},
    {kQuizSlots, 18, "quizSlots"},
    {kAnswerMasks, 9, "answerMasks"},
    {kClueWords, 9, "clueWords"},
    {kCollected, 48, "collected"},
    {kUnusedBA, 1, "unusedBA"},
    {kHidingPictures, 48, "hidingPictures"},
    {kVariablePictures, 9, "variablePictures"},
    {kHidingContents, 48, "hidingContents"},
    {kLevelPicWords, 12, "levelPicWords"},
    {kFoundMasks, 3, "foundMasks"},
    {kShelfBits, 12, "shelfBits"},
    {kQuizTier, 1, "quizTier"},
    {kTier, 2, "tier"},
    {kLevel, 2, "level"},
    {kCoins, 2, "coins"},
    {kNets, 2, "nets"},
    {kTreasuresFound, 2, "treasuresFound"},
    {kKeyState, 2, "keyState"},
    {kPromptIcon, 2, "promptIcon"},
    {kPlayerX, 2, "playerX"},
    {kPlayerY, 2, "playerY"},
    {kCameraX, 2, "cameraX"},
    {kCameraY, 2, "cameraY"},
    {kArea, 2, "area"},
    {kShopCoinsOwed, 2, "shopCoinsOwed"},
    {kBankedNets, 2, "bankedNets"},
    {kBankedCoins, 2, "bankedCoins"},
    {kMusicOn, 2, "musicOn"},
    {kSoundOn, 2, "soundOn"},
    {kGamesPlayed, 2, "gamesPlayed"},
    {kShownTreasure, 2, "shownTreasure"},
    {kCastleLayout, 2, "castleLayout"},
}};
}  // namespace urec

namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t o) { return static_cast<uint16_t>(b[o] | (b[o + 1] << 8)); }
int16_t rdS16(std::span<const uint8_t> b, size_t o) { return static_cast<int16_t>(rd16(b, o)); }
void wr16(std::span<uint8_t> b, size_t o, uint16_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o + 1] = static_cast<uint8_t>(v >> 8);
}

template <size_t N>
void rdBytes(std::span<const uint8_t> b, size_t o, std::array<uint8_t, N>& out) {
    std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(o), N, out.begin());
}
template <size_t N>
void wrBytes(std::span<uint8_t> b, size_t o, const std::array<uint8_t, N>& in) {
    std::copy(in.begin(), in.end(), b.begin() + static_cast<std::ptrdiff_t>(o));
}
template <size_t N>
void rdWords(std::span<const uint8_t> b, size_t o, std::array<uint16_t, N>& out) {
    for (size_t i = 0; i < N; ++i) out[i] = rd16(b, o + 2 * i);
}
template <size_t N>
void wrWords(std::span<uint8_t> b, size_t o, const std::array<uint16_t, N>& in) {
    for (size_t i = 0; i < N; ++i) wr16(b, o + 2 * i, in[i]);
}

char asciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

}  // namespace

// ------------------------------------------------------------------------------------ UserRecord

UserRecord UserRecord::fromBytes(std::span<const uint8_t, kUserRecordSize> bytes) {
    using namespace urec;
    const std::span<const uint8_t> b = bytes;
    UserRecord r;
    rdBytes(b, kName, r.name);
    r.savedGame = b[kSavedGame];
    r.totalTreasures = rdS16(b, kTotalTreasures);
    rdBytes(b, kQuizHistory, r.quizHistory);
    rdWords(b, kQuizSlots, r.quizSlots);
    rdBytes(b, kAnswerMasks, r.answerMasks);
    rdBytes(b, kClueWords, r.clueWords);
    rdBytes(b, kCollected, r.collected);
    r.unusedBA = b[kUnusedBA];
    rdBytes(b, kHidingPictures, r.hidingPictures);
    rdBytes(b, kVariablePictures, r.variablePictures);
    rdBytes(b, kHidingContents, r.hidingContents);
    rdWords(b, kLevelPicWords, r.levelPicWords);
    rdBytes(b, kFoundMasks, r.foundMasks);
    rdBytes(b, kShelfBits, r.shelfBits);
    r.quizTier = b[kQuizTier];
    r.tier = rd16(b, kTier);
    r.level = rd16(b, kLevel);
    r.coins = rd16(b, kCoins);
    r.nets = rd16(b, kNets);
    r.treasuresFound = rd16(b, kTreasuresFound);
    r.keyState = rd16(b, kKeyState);
    r.promptIcon = rd16(b, kPromptIcon);
    r.playerX = rdS16(b, kPlayerX);
    r.playerY = rdS16(b, kPlayerY);
    r.cameraX = rdS16(b, kCameraX);
    r.cameraY = rdS16(b, kCameraY);
    r.area = rd16(b, kArea);
    r.shopCoinsOwed = rdS16(b, kShopCoinsOwed);
    r.bankedNets = rd16(b, kBankedNets);
    r.bankedCoins = rd16(b, kBankedCoins);
    r.musicOn = rd16(b, kMusicOn);
    r.soundOn = rd16(b, kSoundOn);
    r.gamesPlayed = rd16(b, kGamesPlayed);
    r.shownTreasure = rdS16(b, kShownTreasure);
    r.castleLayout = rdS16(b, kCastleLayout);
    return r;
}

void UserRecord::toBytes(std::span<uint8_t, kUserRecordSize> out) const {
    using namespace urec;
    const std::span<uint8_t> b = out;
    wrBytes(b, kName, name);
    b[kSavedGame] = savedGame;
    wr16(b, kTotalTreasures, static_cast<uint16_t>(totalTreasures));
    wrBytes(b, kQuizHistory, quizHistory);
    wrWords(b, kQuizSlots, quizSlots);
    wrBytes(b, kAnswerMasks, answerMasks);
    wrBytes(b, kClueWords, clueWords);
    wrBytes(b, kCollected, collected);
    b[kUnusedBA] = unusedBA;
    wrBytes(b, kHidingPictures, hidingPictures);
    wrBytes(b, kVariablePictures, variablePictures);
    wrBytes(b, kHidingContents, hidingContents);
    wrWords(b, kLevelPicWords, levelPicWords);
    wrBytes(b, kFoundMasks, foundMasks);
    wrBytes(b, kShelfBits, shelfBits);
    b[kQuizTier] = quizTier;
    wr16(b, kTier, tier);
    wr16(b, kLevel, level);
    wr16(b, kCoins, coins);
    wr16(b, kNets, nets);
    wr16(b, kTreasuresFound, treasuresFound);
    wr16(b, kKeyState, keyState);
    wr16(b, kPromptIcon, promptIcon);
    wr16(b, kPlayerX, static_cast<uint16_t>(playerX));
    wr16(b, kPlayerY, static_cast<uint16_t>(playerY));
    wr16(b, kCameraX, static_cast<uint16_t>(cameraX));
    wr16(b, kCameraY, static_cast<uint16_t>(cameraY));
    wr16(b, kArea, area);
    wr16(b, kShopCoinsOwed, static_cast<uint16_t>(shopCoinsOwed));
    wr16(b, kBankedNets, bankedNets);
    wr16(b, kBankedCoins, bankedCoins);
    wr16(b, kMusicOn, musicOn);
    wr16(b, kSoundOn, soundOn);
    wr16(b, kGamesPlayed, gamesPlayed);
    wr16(b, kShownTreasure, static_cast<uint16_t>(shownTreasure));
    wr16(b, kCastleLayout, static_cast<uint16_t>(castleLayout));
}

std::string UserRecord::nameString() const {
    const auto end = std::find(name.begin(), name.end(), uint8_t{0});
    return std::string(name.begin(), end);
}

void UserRecord::setName(std::string_view s) {
    name.fill(0);
    const size_t n = std::min(s.size(), kUserMaxNameLength);
    std::memcpy(name.data(), s.data(), n);
}

uint8_t UserRecord::history(int question) const {
    if (question < 0 || question >= kQuizQuestions) return 0;
    return static_cast<uint8_t>((quizHistory[static_cast<size_t>(question >> 2)] >> ((question & 3) * 2)) & 3);
}

void UserRecord::setHistory(int question, uint8_t status) {
    if (question < 0 || question >= kQuizQuestions) return;
    uint8_t& b = quizHistory[static_cast<size_t>(question >> 2)];
    const int shift = (question & 3) * 2;
    b = static_cast<uint8_t>((b & ~(3 << shift)) | ((status & 3) << shift));
}

// 47c7:075d Users_ClearRecord (the record part; the original then writes the file).
void UserRecord::clear() {
    *this = UserRecord{};
    answerMasks.fill(7);
}

// -------------------------------------------------------------------------------------- UsersDat

std::optional<UsersDat> UsersDat::parse(std::span<const uint8_t> file) {
    if (file.size() < kUsersFileSize) return std::nullopt;
    return fromImage(file);
}

// 47c7:0514 Users_ReadFile: read 0xCAB bytes into the (zeroed) image DS:ba14.
UsersDat UsersDat::fromImage(std::span<const uint8_t> file, bool* complete) {
    std::array<uint8_t, kUsersFileSize> image{};
    const size_t n = std::min(file.size(), kUsersFileSize);
    std::copy_n(file.begin(), n, image.begin());
    if (complete) *complete = n == kUsersFileSize;
    UsersDat d;
    d.header0 = image[0];
    d.spriteHighWater = static_cast<uint16_t>(image[1] | (image[2] << 8));
    for (int i = 0; i < kUserRecordCount; ++i)
        d.records[static_cast<size_t>(i)] =
            UserRecord::fromBytes(std::span<const uint8_t, kUserRecordSize>(image.data() + recordOffset(i), kUserRecordSize));
    return d;
}

// 47c7:055a Users_WriteFile writes the whole image.
std::vector<uint8_t> UsersDat::bytes() const {
    std::vector<uint8_t> out(kUsersFileSize, 0);
    out[0] = header0;
    out[1] = static_cast<uint8_t>(spriteHighWater);
    out[2] = static_cast<uint8_t>(spriteHighWater >> 8);
    for (int i = 0; i < kUserRecordCount; ++i)
        records[static_cast<size_t>(i)].toBytes(std::span<uint8_t, kUserRecordSize>(out.data() + recordOffset(i), kUserRecordSize));
    return out;
}

bool UsersDat::anyPlayer() const { return playerCount() > 0; }

int UsersDat::playerCount() const {
    return static_cast<int>(std::count_if(records.begin(), records.end(), [](const UserRecord& r) { return !r.empty(); }));
}

int UsersDat::firstFreeRecord() const {
    for (int i = 0; i < kUserRecordCount; ++i)
        if (records[static_cast<size_t>(i)].empty()) return i;
    return -1;
}

// 47c7:04a4 Users_NameExists.
int UsersDat::findName(std::string_view typed) const {
    // The typed buffer is char[9]; trailing NULs and blanks are removed (for i = 8 down to 0).
    if (typed.size() > kUserNameSize) typed = typed.substr(0, kUserNameSize);
    while (!typed.empty() && (typed.back() == ' ' || typed.back() == '\0')) typed.remove_suffix(1);
    if (const size_t nul = typed.find('\0'); nul != std::string_view::npos) typed = typed.substr(0, nul);
    for (int i = 0; i < kUserRecordCount; ++i) {
        const UserRecord& r = records[static_cast<size_t>(i)];
        if (r.empty()) continue;
        const std::string n = r.nameString();
        if (n.size() != typed.size()) continue;
        bool same = true;
        for (size_t k = 0; k < n.size() && same; ++k) same = asciiLower(n[k]) == asciiLower(typed[k]);
        if (same) return i;
    }
    return -1;
}

}  // namespace tmport::dos
