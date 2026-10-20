#include "gg/audio/music_cues.h"

#include <algorithm>

#include "gg/audio/snd_res.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"

namespace tmport::gg {

MusicCues::MusicCues(SndRes& res, const GgExe& exe) : res_(res) {
    for (size_t b = 0; b < roomCueTicks_.size(); ++b) roomCueTicks_[b] = exe.ds16(static_cast<uint16_t>(dg::kRoomMusicCueTicks + 2 * b));
    const auto t = exe.dsBytes(dg::kRoomMusicTransposes, transposes_.size());
    std::copy(t.begin(), t.end(), transposes_.begin());
    SndDriver& d = res_.driver();
    roomCueId_ = d.registerCue([this] { roomCue(); });
    workshopCueId_ = d.registerCue([this] { workshopCue(); });
    movieCueId_ = d.registerCue([this] { movieCue(); });
}

// 1622:03b6 RoomMusic_SelectLayers
void MusicCues::roomMusicSelectLayers(int n, bool roomHasChimps, int level) {
    res_.selectArrangement(roomMusicBank_, n);
    res_.muteChannel(roomMusicBank_, 4);
    res_.muteChannel(roomMusicBank_, 5);
    if (roomHasChimps) res_.unmuteChannel(roomMusicBank_, level < 3 ? 4 : 5);
}

// 1622:0346 RoomMusic_Start
void MusicCues::roomMusicStart() {
    if (res_.isPlaying(roomMusicBank_) != 0) return;
    res_.playMusic(roomMusicBank_, 0, 1);
    roomMusicStep_ = 0;
    SndDriver& d = res_.driver();
    d.setTranspose(res_.slot(roomMusicBank_), 0, transposeByte(6 * building_));  // stride 6, not 12
    d.setCue(res_.slot(roomMusicBank_), roomCueTick(building_), roomCueId_);
}

// 12e0:007f Music_RoomCue: the next of the building's 6 transposes for the next pass.
void MusicCues::roomCue() {
    roomMusicStep_ = (roomMusicStep_ + 1) % 6;
    res_.driver().setTranspose(res_.slot(roomMusicBank_), 0, transposeByte(12 * building_ + 2 * roomMusicStep_));
}

// State_06_Workshop 2dee:1a90 (after loading the banks)
void MusicCues::workshopMusicStart(int musicHandle) {
    musicBank_ = musicHandle;
    workshopArrangement_ = 1;
    workshopCues_ = 0;
    res_.selectArrangement(musicBank_, 1);
    res_.playMusic(musicBank_, 0, 1);
    res_.driver().setCue(res_.slot(musicBank_), res_.bankCueTick(), workshopCueId_);
}

// 2dee:17eb / 2dee:185d
void MusicCues::workshopBlueprint(bool open) {
    blueprintOpen_ = open;
    res_.selectArrangement(musicBank_, open ? 3 : workshopArrangement_);
}

// 12e0:00bd Music_WorkshopCue: arrangement 2 at the second call (never reached, §14.2).
void MusicCues::workshopCue() {
    ++workshopCues_;
    if (workshopCues_ == 2) {
        workshopArrangement_ = 2;
        if (!blueprintOpen_) res_.selectArrangement(musicBank_, 2);
    }
}

// State_15_FinalMovie 2fcb:0640 (after loading the music bank)
void MusicCues::movieMusicStart(int musicHandle) {
    movieMusicBank_ = musicHandle;
    movieStep_ = 1;
    res_.driver().setCue(res_.slot(movieMusicBank_), kMovieCueTick, movieCueId_);
    res_.playMusic(movieMusicBank_, 0, 1);
    res_.selectArrangement(movieMusicBank_, 1);
}

// 12e0:00ea Music_MovieCue: arrangements 1, 2, 3, 4, all, all, all, then again from 1.
void MusicCues::movieCue() {
    ++movieStep_;
    if (movieStep_ < 5) {
        res_.selectArrangement(movieMusicBank_, movieStep_);
    } else {
        if (movieStep_ == 5) res_.selectArrangement(movieMusicBank_, 0);
        if (movieStep_ == 7) movieStep_ = 0;
    }
}

}  // namespace tmport::gg
