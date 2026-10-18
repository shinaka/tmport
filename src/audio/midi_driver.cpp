#include "audio/midi_driver.h"

#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport::audio {

std::shared_ptr<const MidiSong> DataSourceTrackLoader::load(const std::string& relPath) {
    const std::string key = toUpperAscii(relPath);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    std::shared_ptr<const MidiSong> song;
    if (auto f = source_.open(relPath)) {
        try {
            song = std::make_shared<const MidiSong>(parseSmf(f->readAll()));
            for (const auto& w : song->warnings) log::debug("midi: {}: {}", relPath, w);
        } catch (const Error& e) {
            log::warn("midi: {}: {}", relPath, e.what());
        }
    } else {
        log::warn("midi: {} not found", relPath);
    }
    cache_[key] = song;  // failures are cached too (warn once)
    return song;
}

std::string MidiDriver::trackPath(int tuneId) const {
    const int i = tables_.tuneIndex(tuneId);
    if (i < 0) return {};
    std::string name = tables_.tuneFiles[static_cast<size_t>(i)];
    if (name.empty()) return {};
    name[0] = set_ == MusicSet::Extended ? 'E' : 'B';
    return tables_.soundsDir + name;
}

int MidiDriver::loopPos() const {
    // idx 10 ("not found") reads past the table in the original, never -1 there.
    if (idx_ < 0 || idx_ >= static_cast<int>(AudioTables::kTunes)) return 0;
    return tables_.loopPos[static_cast<size_t>(idx_)];
}

// 1090:02e9 Midi_OpenTrack
void MidiDriver::openTrack(int tuneId) {
    const int i = tables_.tuneIndex(tuneId);
    idx_ = i < 0 ? static_cast<int>(AudioTables::kTunes) : i;
    std::shared_ptr<const MidiSong> song;
    if (i >= 0) song = loader_.load(trackPath(tuneId));
    // The original opens a garbage path for an unknown id (e.g. Music_Resume before any zone);
    // here that simply opens nothing.
    trackOpen_ = song != nullptr;
    if (song) player_.load(std::move(song));
    savedPosMs_ = 0;
}

// 1090:0388 Midi_PlayTrack
void MidiDriver::playTrack(int tuneId) {
    if (trackOpen_) {  // MCI_CLOSE
        player_.unload();
        trackOpen_ = false;
    }
    openTrack(tuneId);
    if (trackOpen_ && appActive_) {
        curMusic_ = tuneId;
        player_.play(0);
    }
}

// 1090:0413 Midi_Stop
void MidiDriver::stop() {
    if (trackOpen_) {  // MCI_STOP + MCI_CLOSE
        player_.stop();
        player_.unload();
        trackOpen_ = false;
    }
    curMusic_ = -1;
}

// 1090:044b Midi_Pause
void MidiDriver::pause() {
    if (paused_) return;
    if (loopPos() == -1) {
        stop();  // a one-shot tune is abandoned, not paused
    } else if (trackOpen_) {
        savedPosMs_ = static_cast<uint32_t>(player_.positionUs() / 1000u);
        player_.stop();
        paused_ = true;
    }
}

// 1090:04bf Midi_Resume
void MidiDriver::resume() {
    if (paused_ && trackOpen_) {
        player_.play(static_cast<uint64_t>(savedPosMs_) * 1000u);  // MCI_SEEK + MCI_PLAY/MCI_NOTIFY
        paused_ = false;
    }
}

// 1090:03e6 Midi_OnPlayDone
void MidiDriver::onPlayDone() {
    const int loop = loopPos();
    if (loop == -1) {
        stop();
    } else {
        paused_ = true;
        savedPosMs_ = static_cast<uint32_t>(loop);
        resume();
    }
}

// 1090:0519 Midi_IsPlaying
bool MidiDriver::isPlaying() const { return curMusic_ != -1 && trackOpen_ && player_.ok(); }

void MidiDriver::service() {
    if (player_.takeFinished()) onPlayDone();
}

}  // namespace tmport::audio
