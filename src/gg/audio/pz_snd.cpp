#include "gg/audio/pz_snd.h"

#include <format>

#include "gg/audio/snd_driver.h"
#include "gg/data/archive.h"
#include "util/error.h"

namespace tmport::gg {

// 3924:0000 PzSnd_LoadBank: (off[0] / 2) - 1 members (an unsigned compare), each registered.
void PzSnd::loadBank(uint16_t id) {
    bank_ = archive_.load(static_cast<uint16_t>(id + fmPresent_)).bytes;
    if (bank_.size() < 2) throw FormatError(std::format("puzzle sound bank {:#06x}: no offset table", id + fmPresent_));
    const unsigned n = (static_cast<unsigned>(bank_[0] | (bank_[1] << 8)) >> 1) - 1;
    if (n > static_cast<unsigned>(kMaxEffects))
        throw FormatError(std::format("puzzle sound bank {:#06x}: {} members (g_pzSndSlots holds {})", id + fmPresent_, n, kMaxEffects));
    unsigned k = 0;
    for (; k < n; ++k) {
        const size_t off = static_cast<size_t>(bank_[2 * k] | (bank_[2 * k + 1] << 8)) + 2;
        slots_[k] = off < bank_.size() ? driver_.registerSong(bank_, off) : SndDriver::kNotBmf;
    }
    count_ = static_cast<int>(k);
}

// 3924:0074 PzSnd_FreeBank
void PzSnd::freeBank() {
    for (int k = 0; k < count_; ++k) driver_.freeSlot(slots_[static_cast<size_t>(k)]);
    bank_.clear();
    count_ = 0;
}

void PzSnd::play(int i, int priority) { driver_.play(slot(i), 0, priority, 0); }

void PzSnd::playLoop(int i, int priority) { driver_.play(slot(i), 0, priority, 1); }

void PzSnd::stop(int i) { driver_.stop(slot(i)); }

// 3924:00fc: the member count read again from the bank copy.
void PzSnd::stopAll() {
    for (int k = 0; k < count_; ++k) driver_.stop(slots_[static_cast<size_t>(k)]);
}

void PzSnd::setTranspose(int i, int t) { driver_.setTranspose(slot(i), 0, t); }

int PzSnd::isActive(int i) const { return driver_.isActive(slot(i)); }

}  // namespace tmport::gg
