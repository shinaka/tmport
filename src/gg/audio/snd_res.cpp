#include "gg/audio/snd_res.h"

#include <format>
#include <stdexcept>

#include "gg/data/archive.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

SndRes::SndRes(SndDriver& driver, const Archive& archive) : driver_(driver), archive_(archive) { table_.fill(-1); }

int16_t& SndRes::at(int i) {
    if (i < 0 || i >= kTableSize)
        throw std::out_of_range(std::format("sound handle table entry {} is outside g_sndTable[60]", i));
    return table_[static_cast<size_t>(i)];
}

int SndRes::firstFree(int from) const {
    int i = from;
    while (i < kTableSize && table_[static_cast<size_t>(i)] != -1) ++i;
    return i;
}

std::vector<uint8_t> SndRes::loadCopy(uint16_t id) const {
    return archive_.load(static_cast<uint16_t>(id + fmPresent_)).bytes;
}

// bank + off[i] + 2: the payload of member i (the walks of 1cdd:0024 / 031a / 0423 read the
// offset table without a count).
size_t SndRes::memberPayload(const std::vector<uint8_t>& bank, int i) {
    const auto at = static_cast<size_t>(2 * i);
    if (at + 2 > bank.size()) throw FormatError(std::format("sound bank: member {} is past the offset table", i));
    const size_t off = static_cast<size_t>(bank[at] | (bank[at + 1] << 8)) + 2;
    if (off >= bank.size()) throw FormatError(std::format("sound bank: member {} is past the end", i));
    return off;
}

// 1cdd:0004 SndRes_Init
void SndRes::init() {
    table_.fill(-1);
    for (auto& m : mem_) m.clear();
    globalEffects_ = loadEffectBank(kGlobalEffectBank);
}

// 1cdd:0024 SndRes_LoadMusicBank: members up to the first payload starting with 0 (the `I 0`
// member; one starting with 4 gives g_sndBankCueTick the word at payload + 2), then the song.
int SndRes::loadMusicBank(uint16_t id) {
    const int h = firstFree(0);
    at(h);  // a full table would write past g_sndTable
    auto& bank = mem_[static_cast<size_t>(h)];
    bank = loadCopy(id);
    int i = 0;
    size_t p = memberPayload(bank, 0);
    while (bank[p] != 0) {
        if (bank[p] == 4 && p + 3 < bank.size()) bankCueTick_ = static_cast<uint16_t>(bank[p + 2] | (bank[p + 3] << 8));
        p = memberPayload(bank, ++i);
    }
    const int slot = driver_.registerSong(bank, memberPayload(bank, i + 1));
    at(h) = static_cast<int16_t>(slot);
    if (slot < 0) {
        log::warn("gg sound: music bank {:#06x}: the song did not register ({})", id + fmPresent_, slot);
        bank.clear();
        at(h) = -1;
    }
    return h;
}

// 1cdd:012d SndRes_LoadEffectBank: n = off[0] / 2 - 1 members, all registered in order (a `C FF`
// member leaves its error code); the handle is the entry + 1.
int SndRes::loadEffectBank(uint16_t id) {
    const int e = firstFree(kFirstEffectHandle);
    at(e);
    auto& bank = mem_[static_cast<size_t>(e)];
    bank = loadCopy(id);
    if (bank.size() < 2) throw FormatError(std::format("sound bank {:#06x}: no offset table", id + fmPresent_));
    const int n = (static_cast<uint16_t>(bank[0] | (bank[1] << 8)) >> 1) - 1;
    for (int k = 0; k < n; ++k) at(e + 1 + k) = static_cast<int16_t>(driver_.registerSong(bank, memberPayload(bank, k)));
    at(e) = static_cast<int16_t>(n);
    return e + 1;
}

// 1cdd:01e0 SndRes_LoadSong: a BMF entry (the hint chime 0x1056 / 0x1057).
int SndRes::loadSong(uint16_t id) {
    const int e = firstFree(kFirstEffectHandle);
    at(e);
    auto& bank = mem_[static_cast<size_t>(e)];
    bank = loadCopy(id);
    at(e) = static_cast<int16_t>(driver_.registerSong(bank, 0));
    return e;
}

// 1cdd:0237
void SndRes::freeMusicBank(int h) {
    if (at(h) >= 0) {
        driver_.freeSlot(at(h));
        mem_[static_cast<size_t>(h)].clear();
    }
    at(h) = -1;
    lastMusicSlot_ = -1;
}

// 1cdd:027d: h = the effect handle (entry + 1).
void SndRes::freeEffectBank(int h) {
    const int n = at(h - 1);
    at(h - 1) = -1;
    lastEffectSlot_ = -1;
    for (int k = 0; k < n; ++k) {
        driver_.freeSlot(at(h + k));
        at(h + k) = -1;
    }
    mem_[static_cast<size_t>(h - 1)].clear();
}

// 1cdd:02e3
void SndRes::freeSong(int h) {
    driver_.freeSlot(at(h));
    at(h) = -1;
    mem_[static_cast<size_t>(h)].clear();
}

// 1cdd:031a SndRes_PlayMusic
void SndRes::playMusic(int h, int section, int loop) {
    const auto& bank = mem_.at(static_cast<size_t>(h));
    int priority = 0;
    if (!bank.empty()) {
        size_t p = memberPayload(bank, 0);
        for (int i = 1; bank[p] != 0; ++i) {
            if (bank[p] == 3) {
                priority = p + 1 < bank.size() ? static_cast<int8_t>(bank[p + 1]) : 0;
                break;
            }
            p = memberPayload(bank, i);
        }
    }
    if (driver_.isActive(at(h)) == 0) {
        lastMusicSlot_ = at(h);
        driver_.play(at(h), section, priority, loop);
    }
}

// 1cdd:03c9 SndRes_PlayEffect
void SndRes::playEffect(int h, int loop, int p) {
    lastEffectSlot_ = at(h);
    driver_.play(at(h), 0, p + kEffectPriority, loop);
}

void SndRes::stopMusic(int h) {
    driver_.stop(at(h));
    lastMusicSlot_ = -1;
}

void SndRes::stopEffect(int h) {
    driver_.stop(at(h));
    lastEffectSlot_ = -1;
}

void SndRes::stopAllEffects() {
    if (effectBank_ < 1) {
        log::debug("gg sound: SndRes_StopAllEffects without an effect bank");
        return;
    }
    const int n = at(effectBank_ - 1);
    for (int k = 0; k < n; ++k) driver_.stop(at(effectBank_ + k));
}

int SndRes::isPlaying(int h) const {
    const int16_t s = table_.at(static_cast<size_t>(h));
    return s < 0 ? 0 : driver_.isActive(s);
}

int SndRes::slot(int h) const { return table_.at(static_cast<size_t>(h)); }

// 1cdd:0423 SndRes_SelectArrangement: the n-th member whose payload starts with 1 (or the first
// starting with 0 when there are fewer lists), then a mute for each byte after the first up to an
// FFh: no end check, signed channel numbers (code_sound.md §13.4).
void SndRes::selectArrangement(int h, int n) {
    const int s = at(h);
    for (int c = 0; c < 16; ++c) driver_.unmuteChannel(s, c);
    if (n == 0) return;
    const auto& bank = mem_.at(static_cast<size_t>(h));
    if (bank.empty()) return;
    size_t p = memberPayload(bank, 0);
    int count = 1;
    for (int i = 1; bank[p] != 0 && (bank[p] != 1 || count++ != n); ++i) p = memberPayload(bank, i);
    while (++p < bank.size() && bank[p] != 0xFF) driver_.muteChannel(s, static_cast<int8_t>(bank[p]));
    if (p >= bank.size()) log::warn("gg sound: arrangement {} of handle {} ran off the bank copy", n, h);
}

void SndRes::muteChannel(int h, int channel) { driver_.muteChannel(at(h), channel); }

void SndRes::unmuteChannel(int h, int channel) { driver_.unmuteChannel(at(h), channel); }

// 1cdd:0592 SndRes_ApplySoundOption
void SndRes::applySoundOption() {
    if (globalEffects_ < 0) return;
    if (soundMuted_ == 0) driver_.stop(at(globalEffects_));
    else driver_.play(at(globalEffects_), 0, 0x7F, 1);
}

// 1cdd:05c8 Snd_ToggleOption
void SndRes::toggleOption() {
    soundMuted_ ^= 1;
    applySoundOption();
}

}  // namespace tmport::gg
