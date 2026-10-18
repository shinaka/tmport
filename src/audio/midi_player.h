#pragma once
// MidiPlayer: the "MCI track device" the original drives (re/docs/fmt_sound.md §8.3): load a
// song (MCI_OPEN element), play from a position with a completion notification (MCI_SEEK +
// MCI_PLAY/MCI_NOTIFY), stop keeping the position (MCI_STOP), query the position
// (MCI_STATUS_POSITION), unload (MCI_CLOSE). Like the MCI sequencer it plays asynchronously: in
// threaded mode a worker thread advances the sequencer on the real-time clock, so music keeps
// time even while the game is busy. In manual mode (tests) service() advances it on the given
// clock. All methods are thread-safe.

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "audio/midi_backend.h"
#include "audio/midi_sequencer.h"
#include "audio/smf.h"

namespace tmport::audio {

class MidiPlayer {
public:
    // Opens the back end. threaded = true starts the worker thread (use the real clock then).
    MidiPlayer(std::unique_ptr<MidiBackend> backend, MicroClock clock, bool threaded);
    ~MidiPlayer();
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    // The back end opened (the original's midiOk, DS:0a4a).
    bool ok() const { return ok_; }
    std::string backendName() const;

    void load(std::shared_ptr<const MidiSong> song);  // stops; position 0
    void unload();
    bool loaded() const;
    std::shared_ptr<const MidiSong> song() const;

    void play(uint64_t fromUs);  // from a song position, with a completion notification
    void stop();                 // keeps the position
    bool playing() const;
    uint64_t positionUs() const;

    // True once after a play reached the end of the song (MM_MCINOTIFY with
    // MCI_NOTIFY_SUCCESSFUL). A stop, play, load or unload discards a pending notification.
    bool takeFinished();

    // Manual mode: sends the events due at the clock's current time. No-op when threaded.
    void service();

    // For tests: runs f with the back end while holding the player lock.
    template <class F>
    void withBackend(F&& f) {
        std::lock_guard lock(mu_);
        f(*backend_);
    }

private:
    void run();

    mutable std::mutex mu_;
    std::unique_ptr<MidiBackend> backend_;
    MicroClock clock_;
    MidiSequencer seq_;
    bool ok_ = false;
    bool threaded_ = false;
    bool finished_ = false;
    std::atomic<bool> quit_{false};
    std::thread worker_;
};

}  // namespace tmport::audio
