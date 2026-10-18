#include "audio/midi_player.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <limits>

namespace tmport::audio {

MidiPlayer::MidiPlayer(std::unique_ptr<MidiBackend> backend, MicroClock clock, bool threaded)
    : backend_(backend ? std::move(backend) : std::make_unique<NullMidiBackend>()),
      clock_(clock ? std::move(clock) : MicroClock(realClockUs)),
      seq_(*backend_),
      threaded_(threaded) {
    ok_ = backend_->open();
    if (threaded_) worker_ = std::thread([this] { run(); });
}

MidiPlayer::~MidiPlayer() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mu_);
    seq_.stop(clock_());
    seq_.setSong(nullptr);
    backend_->close();
}

std::string MidiPlayer::backendName() const {
    std::lock_guard lock(mu_);
    return backend_->name();
}

void MidiPlayer::load(std::shared_ptr<const MidiSong> song) {
    std::lock_guard lock(mu_);
    seq_.setSong(std::move(song));
    finished_ = false;
}

void MidiPlayer::unload() { load(nullptr); }

bool MidiPlayer::loaded() const {
    std::lock_guard lock(mu_);
    return seq_.song() != nullptr;
}

std::shared_ptr<const MidiSong> MidiPlayer::song() const {
    std::lock_guard lock(mu_);
    return seq_.song();
}

void MidiPlayer::play(uint64_t fromUs) {
    std::lock_guard lock(mu_);
    finished_ = false;
    if (!ok_) return;  // no output: nothing plays, so nothing ever completes either
    seq_.start(clock_(), fromUs);
}

void MidiPlayer::stop() {
    std::lock_guard lock(mu_);
    seq_.stop(clock_());
    finished_ = false;
}

bool MidiPlayer::playing() const {
    std::lock_guard lock(mu_);
    return seq_.playing();
}

uint64_t MidiPlayer::positionUs() const {
    std::lock_guard lock(mu_);
    return seq_.positionUs(clock_());
}

bool MidiPlayer::takeFinished() {
    std::lock_guard lock(mu_);
    const bool f = finished_;
    finished_ = false;
    return f;
}

void MidiPlayer::service() {
    if (threaded_) return;
    std::lock_guard lock(mu_);
    if (seq_.advance(clock_())) finished_ = true;
}

void MidiPlayer::run() {
    constexpr uint64_t kMaxSleepUs = 2000;   // re-check at least every 2 ms while playing
    constexpr uint64_t kIdleSleepUs = 5000;
    while (!quit_) {
        uint64_t sleepUs = kIdleSleepUs;
        {
            std::lock_guard lock(mu_);
            if (seq_.advance(clock_())) finished_ = true;
            const uint64_t wake = seq_.nextWakeUs();
            if (wake != std::numeric_limits<uint64_t>::max()) {
                const uint64_t now = clock_();
                sleepUs = wake > now ? std::min(wake - now, kMaxSleepUs) : 0;
            }
        }
        if (sleepUs) SDL_DelayNS(sleepUs * 1000u);
    }
}

}  // namespace tmport::audio
