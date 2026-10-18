#include "audio/midi_sequencer.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace tmport::audio {

void MidiSequencer::setSong(std::shared_ptr<const MidiSong> song) {
    if (playing_) silence();
    playing_ = false;
    song_ = std::move(song);
    next_ = 0;
    startPosUs_ = stopPosUs_ = 0;
}

void MidiSequencer::start(uint64_t nowUs, uint64_t fromUs) {
    if (!song_) return;
    if (playing_) silence();
    fromUs = std::min(fromUs, song_->lengthUs);
    chase(fromUs);
    startNowUs_ = nowUs;
    startPosUs_ = fromUs;
    stopPosUs_ = fromUs;
    playing_ = true;
}

void MidiSequencer::stop(uint64_t nowUs) {
    if (!playing_) return;
    stopPosUs_ = positionUs(nowUs);
    playing_ = false;
    silence();
}

uint64_t MidiSequencer::positionUs(uint64_t nowUs) const {
    if (!playing_ || !song_) return stopPosUs_;
    const uint64_t pos = startPosUs_ + (nowUs > startNowUs_ ? nowUs - startNowUs_ : 0);
    return std::min(pos, song_->lengthUs);
}

bool MidiSequencer::advance(uint64_t nowUs) {
    if (!playing_ || !song_) return false;
    const uint64_t pos = startPosUs_ + (nowUs > startNowUs_ ? nowUs - startNowUs_ : 0);
    const auto& ev = song_->events;
    while (next_ < ev.size() && ev[next_].us <= pos) {
        const MidiEvent& e = ev[next_++];
        if (!e.isChannel()) continue;  // meta and SysEx are not sent
        uint8_t& n = sounding_[e.channel()][e.data1 & 0x7F];
        if (e.isNoteOn()) {
            if (n < 255) ++n;
        } else if (e.isNoteOff()) {
            if (n > 0) --n;
        }
        out_.send(e.status, e.data1, e.data2);
        ++eventsSent_;
    }
    if (next_ >= ev.size() && pos >= song_->lengthUs) {
        // Natural end (MCI_NOTIFY_SUCCESSFUL). Release anything a malformed file left sounding.
        playing_ = false;
        stopPosUs_ = song_->lengthUs;
        for (uint8_t ch = 0; ch < 16; ++ch)
            for (uint8_t note = 0; note < 128; ++note)
                for (; sounding_[ch][note] > 0; --sounding_[ch][note]) out_.send(static_cast<uint8_t>(0x80 | ch), note, 0);
        return true;
    }
    return false;
}

uint64_t MidiSequencer::nextWakeUs() const {
    if (!playing_ || !song_) return std::numeric_limits<uint64_t>::max();
    const auto& ev = song_->events;
    const uint64_t songTime = next_ < ev.size() ? ev[next_].us : song_->lengthUs;
    return songTime > startPosUs_ ? startNowUs_ + (songTime - startPosUs_) : startNowUs_;
}

void MidiSequencer::silence() {
    for (uint8_t ch = 0; ch < 16; ++ch)
        for (uint8_t note = 0; note < 128; ++note)
            for (; sounding_[ch][note] > 0; --sounding_[ch][note]) out_.send(static_cast<uint8_t>(0x80 | ch), note, 0);
    out_.allNotesOff();
}

void MidiSequencer::chase(uint64_t toUs) {
    const auto& ev = song_->events;
    next_ = static_cast<size_t>(
        std::lower_bound(ev.begin(), ev.end(), toUs, [](const MidiEvent& e, uint64_t t) { return e.us < t; }) - ev.begin());
    if (next_ == 0) return;
    // Latest value per channel of: program, channel pressure, pitch bend, controllers 0..119
    // (120..127 are channel-mode messages and are not chased). Re-sent in their original order.
    constexpr size_t kPerChannel = 3 + 120;
    struct Slot {
        size_t order = std::numeric_limits<size_t>::max();
        uint8_t status = 0, d1 = 0, d2 = 0;
    };
    std::vector<Slot> slots(16 * kPerChannel);
    for (size_t i = 0; i < next_; ++i) {
        const MidiEvent& e = ev[i];
        if (!e.isChannel()) continue;
        size_t k;
        switch (e.command()) {
            case 0xC0: k = 0; break;
            case 0xD0: k = 1; break;
            case 0xE0: k = 2; break;
            case 0xB0:
                if (e.data1 >= 120) continue;
                k = 3 + e.data1;
                break;
            default: continue;
        }
        slots[e.channel() * kPerChannel + k] = Slot{i, e.status, e.data1, e.data2};
    }
    std::vector<const Slot*> used;
    for (const auto& s : slots)
        if (s.order != std::numeric_limits<size_t>::max()) used.push_back(&s);
    std::sort(used.begin(), used.end(), [](const Slot* a, const Slot* b) { return a->order < b->order; });
    for (const Slot* s : used) out_.send(s->status, s->d1, s->d2);
}

}  // namespace tmport::audio
