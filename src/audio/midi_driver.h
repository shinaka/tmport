#pragma once
// MidiDriver: the original's MIDI module (segment 1090, re/docs/fmt_sound.md §8.2-8.3) on top of
// our own sequencer. It keeps the original's state variables and their exact update rules
// (curMusic, paused, savedPos, the tune index and the track "device"), so pause/resume, looping
// and one-shot tunes behave like the MCI version, quirks included:
//   * Midi_Stop does not clear `paused`; Midi_PlayTrack does not either;
//   * Midi_Pause abandons a one-shot tune (loopPos == -1) instead of pausing it;
//   * Midi_PlayTrack while the app is inactive opens the track but does not start it.
// Positions are milliseconds (the MCI default time format is not set by the game; ms assumed).
// The B/E file set replaces the original's midimap.cfg probe (§8.2): Extended (E*.MID) by
// default, Base (B*.MID) selectable.

#include <map>
#include <memory>
#include <string>

#include "audio/audio_tables.h"
#include "audio/midi_player.h"
#include "audio/smf.h"

namespace mischief::vfs {
class DataSource;
}

namespace tmport::audio {

// Source of parsed MIDI files by path relative to the game folder.
class TrackLoader {
public:
    virtual ~TrackLoader() = default;
    // nullptr if the file is missing or not a usable SMF.
    virtual std::shared_ptr<const MidiSong> load(const std::string& relPath) = 0;
};

// Reads tunes from the user's data source (directory or CD image); parsed songs are cached.
// TODO(mods): consult mods/MIDI/<9000..9009>.mid first (PORTING_NOTES.md §7.4).
class DataSourceTrackLoader final : public TrackLoader {
public:
    explicit DataSourceTrackLoader(const mischief::vfs::DataSource& source) : source_(source) {}
    std::shared_ptr<const MidiSong> load(const std::string& relPath) override;

private:
    const mischief::vfs::DataSource& source_;
    std::map<std::string, std::shared_ptr<const MidiSong>> cache_;  // key: upper-case path
};

enum class MusicSet {
    Base,      // B*.MID: base-level synth layout (channels 13-16)
    Extended,  // E*.MID: General MIDI layout (channels 1-10); the default
};

class MidiDriver {
public:
    MidiDriver(const AudioTables& tables, MidiPlayer& player, TrackLoader& loader, MusicSet set = MusicSet::Extended)
        : tables_(tables), player_(player), loader_(loader), set_(set) {}

    MusicSet musicSet() const { return set_; }
    void setMusicSet(MusicSet s) { set_ = s; }
    // soundsDir + the tune's file template with its placeholder byte replaced by 'B' or 'E'
    // (1090:02e9), e.g. "SOUNDS\" + "Ename.mid". Empty for a tune id not in DS:09fa.
    std::string trackPath(int tuneId) const;

    void openTrack(int tuneId);  // 1090:02e9 Midi_OpenTrack
    void playTrack(int tuneId);  // 1090:0388 Midi_PlayTrack
    void stop();                 // 1090:0413 Midi_Stop
    void pause();                // 1090:044b Midi_Pause
    void resume();               // 1090:04bf Midi_Resume
    void onPlayDone();           // 1090:03e6 Midi_OnPlayDone (MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL)
    bool isPlaying() const;      // 1090:0519 Midi_IsPlaying
    // Delivers a pending end-of-tune notification (the window procedure's MM_MCINOTIFY case).
    void service();

    // g_appActive (DS:049a).
    bool appActive() const { return appActive_; }
    void setAppActive(bool a) { appActive_ = a; }

    // Original state, for tests and diagnostics.
    int curMusic() const { return curMusic_; }         // DS:0a4c, -1 = none
    bool paused() const { return paused_; }            // DS:0a4e
    uint32_t savedPosMs() const { return savedPosMs_; } // DS:0a54
    int tuneIndex() const { return idx_; }             // DS:4b78 (10 = not found)
    bool trackOpen() const { return trackOpen_; }      // trackDev (DS:0a52) != 0
    bool midiOk() const { return player_.ok(); }       // DS:0a4a
    MidiPlayer& player() { return player_; }

private:
    int loopPos() const;

    const AudioTables& tables_;
    MidiPlayer& player_;
    TrackLoader& loader_;
    MusicSet set_;
    bool appActive_ = true;
    int curMusic_ = -1;
    bool paused_ = false;
    uint32_t savedPosMs_ = 0;
    int idx_ = 0;
    bool trackOpen_ = false;
};

}  // namespace tmport::audio
