#pragma once
// MusicManager: the scene music controller (segment 1128, re/docs/fmt_sound.md §8.4-8.5).
// Maps a music zone code to a tune, remembers it even while sound is off (so turning sound on
// starts it), and offers the halt / restart-from-the-beginning pair used around hints and elf
// questions. Pause/resume at the saved position (dialogs, menus, focus loss) is MidiDriver's
// pause()/resume(), reached through AudioSystem::pauseNested()/appDeactivate() and friends.

#include <cstdint>
#include <optional>

#include "audio/midi_driver.h"

namespace tmport::audio {

// "Music and Sound" (DS:0b96) and "Voice" (DS:0b97), saved per player (record +0x0E / +0x0F).
// Bytes, as in the original (sign-in copies the record byte unchanged).
struct AudioSettings {
    uint8_t soundOn = 1;
    uint8_t voiceOn = 1;
};

// Music zone codes: the argument of Music_PlayZone and RoomDef.music (DS:23d8 + 0x1C*room + 0x1B).
namespace zone {
inline constexpr int kHall = 0;      // rooms 0-8, CLUBHSE
inline constexpr int kLevel0 = 1;    // rooms 9-22, LEVEL1 (2 maps to the same tune, unused)
inline constexpr int kLevel1 = 3;    // rooms 23-35, LEVEL2 (4 unused)
inline constexpr int kLevel2 = 5;    // rooms 36-48, LEVEL3 (6 unused)
inline constexpr int kCastle = 7;    // room 49, CASTLE
inline constexpr int kFinale = -98;  // celebration, new champion
inline constexpr int kTitle = -97;   // never requested by the game
inline constexpr int kThrone = -96;  // celebration
inline constexpr int kOpen1 = -95;   // intro, plays once
inline constexpr int kOpen2 = -94;   // intro after OPEN1, sign-in
}  // namespace zone

class MusicManager {
public:
    MusicManager(MidiDriver& driver, const AudioSettings& settings, int16_t initialTuneId)
        : driver_(driver), settings_(settings), tuneId_(initialTuneId) {}

    // The zone -> tune switch of Music_PlayZone (1128:001a, 13 cases); nullopt for other codes.
    static std::optional<int> tuneForZone(int code);

    void playZone(int code);              // 1128:0000 Music_PlayZone
    void halt();                          // 1128:00d6 Music_Halt (zone and tune kept)
    void resume();                        // 1128:00dc Music_Resume: restarts the tune from its beginning
    void stopScene();                     // 1128:00ef Music_StopScene (stop and forget the zone)
    bool isZonePlaying(int code) const;   // 1128:010c Music_IsZonePlaying

    // The room-change rule of the main loop (1118:0274..035d), for the game module: call with the
    // music codes of the room left and the room entered. Equal codes: nothing happens, the tune
    // keeps playing (returns false). Otherwise Music_StopScene and, unless deferStart (rooms 0
    // and 9 start their tune after the room transition), Music_PlayZone(enteredCode); with
    // deferStart the caller calls playZone(enteredCode) after presenting the room.
    bool onRoomChange(int leftCode, int enteredCode, bool deferStart);

    int zone() const { return zone_; }              // DS:5efe
    int tuneId() const { return tuneId_; }          // DS:13ca
    bool sceneActive() const { return sceneActive_; }  // DS:13c6

private:
    MidiDriver& driver_;
    const AudioSettings& settings_;
    int16_t tuneId_;
    int16_t zone_ = 0;
    bool sceneActive_ = false;
};

}  // namespace tmport::audio
