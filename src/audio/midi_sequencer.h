#pragma once
// MidiSequencer: real-time playback of a parsed MidiSong to a MidiBackend, driven by an explicit
// clock (advance(nowUs)), so it is deterministic and testable on a fake clock. Not thread-safe;
// MidiPlayer wraps it for threaded use.
//
// Positions are song time in microseconds. start(now, from) plays from any position: before
// starting mid-song it "chases" the channel state (programs, controllers, pitch bend, channel
// pressure) the song set before that position, so a resumed tune sounds right. stop() sends
// note-offs for every sounding note and the back end's all-notes-off, and keeps the position.
// SysEx and meta events are never sent.

#include <array>
#include <cstdint>
#include <memory>

#include "audio/midi_backend.h"
#include "audio/smf.h"

namespace tmport::audio {

class MidiSequencer {
public:
    explicit MidiSequencer(MidiBackend& out) : out_(out) {}

    // Replaces the song (stops the current one); position 0.
    void setSong(std::shared_ptr<const MidiSong> song);
    const std::shared_ptr<const MidiSong>& song() const { return song_; }

    // Starts (or restarts) playback at song time fromUs (clamped to the song length).
    void start(uint64_t nowUs, uint64_t fromUs);
    // Stops playback, silencing sounding notes; the position is kept.
    void stop(uint64_t nowUs);
    bool playing() const { return playing_; }
    // Song time at nowUs (while stopped: where it stopped).
    uint64_t positionUs(uint64_t nowUs) const;

    // Sends every event due by nowUs. Returns true exactly once when playback reaches the end of
    // the song (the sequencer is then stopped at the end).
    bool advance(uint64_t nowUs);
    // Wall-clock time at which advance() next has work (an event or the end); UINT64_MAX when
    // not playing.
    uint64_t nextWakeUs() const;

    // Number of channel messages sent for song events (excluding chase and silence).
    uint64_t eventsSent() const { return eventsSent_; }

private:
    void silence();
    void chase(uint64_t toUs);

    MidiBackend& out_;
    std::shared_ptr<const MidiSong> song_;
    size_t next_ = 0;
    bool playing_ = false;
    uint64_t startNowUs_ = 0;  // wall time at which song time startPosUs_ was (re)started
    uint64_t startPosUs_ = 0;
    uint64_t stopPosUs_ = 0;
    uint64_t eventsSent_ = 0;
    std::array<std::array<uint8_t, 128>, 16> sounding_{};
};

}  // namespace tmport::audio
