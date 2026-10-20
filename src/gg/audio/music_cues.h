#pragma once
// The music cue callbacks of ssg.exe's game-flow segment 12e0 (re/gg/docs/code_sound.md §14) and
// the sound calls the game makes around them: the room music (RoomMusic_SelectLayers 1622:03b6,
// RoomMusic_Start 1622:0346, Music_RoomCue 12e0:007f), the workshop music (State_06_Workshop
// 2dee:1a90, the blueprint sub-screen 2dee:17eb / 185d, Music_WorkshopCue 12e0:00bd) and the final
// movie's music (State_15_FinalMovie 2fcb:0640, Music_MovieCue 12e0:00ea).
//
// The callbacks run inside the sound tick (the original's timer interrupt; here the render side,
// under GgAudio's lock) and touch only sound state and the counters below, which the game reads
// through this class. The original's data bugs are kept (§14.1: the start transpose read with a
// stride of 6; the speaker Aircraft music is shorter than its cue tick, so it never changes key;
// §14.2: the workshop cue never fires; §13.4: the movie's speaker list 4).

#include <array>
#include <cstdint>

#include "gg/audio/snd_driver.h"

namespace tmport::gg {

class GgExe;
class SndRes;

class MusicCues {
public:
    static constexpr uint16_t kRoomMusicBank = 0x035F;  // + 2 * building (Rooms_Load 1622:0096)
    static constexpr uint16_t kWorkshopMusicBank = 0x0027;
    static constexpr uint16_t kWorkshopEffectBank = 0x0029;
    static constexpr uint16_t kMovieMusicBank = 0x30CA;
    static constexpr uint16_t kMovieEffectBank = 0x108D;
    static constexpr uint16_t kMovieCueTick = 0x250;  // 592 = the movie song's length

    // Reads the cue ticks DS:0a06 and the transposes DS:010e from ssg.exe and registers the three
    // callbacks with the driver (their CueIds stand for the far pointers 12e0:007f / 00bd / 00ea).
    MusicCues(SndRes& res, const GgExe& exe);

    // ---- rooms
    void setRoomMusicBank(int h) { roomMusicBank_ = h; }  // g_roomMusicBank DS:09e6
    int roomMusicBank() const { return roomMusicBank_; }
    void setBuilding(int building) { building_ = building; }  // g_building (0 automotive, 1 aircraft, 2 alt. energy)
    // 1622:03b6: arrangement n (the room 1..3, else 4), channels 4 and 5 muted, then the chimp voice:
    // channel 4 (level < 3) or 5 back when the room has chimps (g_roomChimpCount[room] != 0).
    void roomMusicSelectLayers(int n, bool roomHasChimps, int level);
    // 1622:0346: unless it plays: the music from section 0, looping, step 0, the start transpose
    // (byte DS:010e[6 * building]: the stride bug) and the cue Music_RoomCue at DS:0a06[building].
    void roomMusicStart();
    int roomMusicStep() const { return roomMusicStep_; }  // g_roomMusicStep DS:08be

    // ---- workshop: g_musicBank and g_effectBank are loaded by the caller (banks 0x27, 0x29).
    // 2dee:1a90..: g_workshopArrangement = 1, cue count 0, arrangement 1, the music looping, the cue
    // Music_WorkshopCue at g_sndBankCueTick (the misread L record: it never fires).
    void workshopMusicStart(int musicHandle);
    // 2dee:17eb (open: arrangement 3) / 2dee:185d (closed: back to g_workshopArrangement).
    void workshopBlueprint(bool open);
    int workshopArrangement() const { return workshopArrangement_; }  // DS:4558
    int workshopCueCount() const { return workshopCues_; }            // DS:4556

    // ---- final movie: the bank is loaded by the caller (0x30CA).
    // 2fcb:0640: step 1, the cue Music_MovieCue at 250h, the music looping, arrangement 1.
    void movieMusicStart(int musicHandle);
    int movieMusicStep() const { return movieStep_; }  // g_movieMusicStep DS:4554

    // The callbacks (for tests and the sound tool; the driver calls them through their ids).
    void roomCue();      // Music_RoomCue 12e0:007f
    void workshopCue();  // Music_WorkshopCue 12e0:00bd
    void movieCue();     // Music_MovieCue 12e0:00ea
    uint16_t roomCueTick(int building) const { return roomCueTicks_.at(static_cast<size_t>(building)); }
    // The transpose byte DS:010e + offset (offset = 12 building + 2 step for a cue).
    uint8_t transposeByte(int offset) const { return transposes_.at(static_cast<size_t>(offset)); }

private:
    SndRes& res_;
    SndDriver::CueId roomCueId_ = 0, workshopCueId_ = 0, movieCueId_ = 0;
    std::array<uint16_t, 3> roomCueTicks_{};  // DS:0a06
    std::array<uint8_t, 36> transposes_{};    // DS:010e: 3 rows of 6 words, the low byte used

    int roomMusicBank_ = -1;
    int building_ = 0;
    int roomMusicStep_ = 0;
    int musicBank_ = -1;          // g_musicBank DS:9dc4 (the workshop's)
    int workshopArrangement_ = 0;
    int workshopCues_ = 0;
    bool blueprintOpen_ = false;  // g_blueprintOpen DS:2198
    int movieMusicBank_ = -1;     // g_movieMusicBank DS:4552
    int movieStep_ = 0;
};

}  // namespace tmport::gg
