#pragma once
// Scripted sound schedules for the sound tool and the tests: the game's sound calls at given sound
// ticks, to reproduce what a DOSBox-X capture recorded (re/gg/docs/fmt_sound.md §5 lists the
// captures and the schedules derived from them).
//
// Script: "TICK:CMD[:ARG...],..." with absolute sound ticks (the command runs right before that
// tick, as a call from the game between two timer interrupts); "A/BxN:CMD..." repeats a step N
// times at ticks A, A + B, ... Ids are hex, other arguments decimal (may be negative). Commands:
//   m:ID  load music bank ID (SndRes_LoadMusicBank; + g_fmPresent)    p[:LOOP]  play it (loop 1)
//   a:N   its arrangement N    x  stop it    M  free it    t:T  transpose of its section 0
//   e:ID  load effect bank ID (also g_effectBank)   f:K[:LOOP[:P]]  play effect K (loop 0, P 4 -> 43h)
//   s:K   stop effect K    E  free the effect bank    S  SndRes_StopAllEffects
//   g:K[:LOOP[:P]]  effect K of the global bank 0x105B    q  Ctrl+S    z  pause    r  resume
//   R:B:N[:CHIMPS[:LEVEL]]  room music of building B (bank 0x35F + 2B, loaded when B changes),
//                           RoomMusic_SelectLayers(N) and RoomMusic_Start
//   W     workshop: effects 0x29, music 0x27 and Music_WorkshopCue's start (2dee:1a90)
//   V     final movie: music 0x30CA with Music_MovieCue (2fcb:0640), effects 0x108D
//   P:ID  puzzle bank (PzSnd_LoadBank)   y:K[:P] / Y:K[:P]  PzSnd_Play / PlayLoop (raw priority P, 3)
//   u:K   PzSnd_Stop    U  PzSnd_StopAll    T:K:N  PzSnd_SetTranspose
//   L     the tick is lost: it falls while a call holds the driver's lock (g_sndBusy)
//   h:ID[:P]  a single song (SndRes_LoadSong) played as an effect (priority P, 32h: the hint chime)
//   H     free it
//   end   the end of the run

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::gg {

class Archive;
class GgAudio;
class GgExe;
struct DroComparison;

struct SoundTestStep {
    uint64_t tick = 0;
    std::string cmd;
    std::vector<int> args;
};

struct SoundTestSchedule {
    std::string name;
    std::string description;
    std::vector<SoundTestStep> steps;  // in tick order
    // A named schedule's capture (a .dro of the captures' audio folder) and how to compare with it
    // (dro_compare.h): the write filter from the capture's dump, the driver's B0h shadows from it.
    std::string capture;
    bool compareWithDump = false;
    bool seedFromCapture = false;

    // Throws std::invalid_argument for a bad script.
    static SoundTestSchedule parse(std::string_view script);
    // The schedules of the captures (fmt_sound.md §5); nullopt for an unknown name.
    static std::optional<SoundTestSchedule> named(std::string_view name);
    static std::vector<std::string> names();
};

class SoundTest {
public:
    explicit SoundTest(GgAudio& audio) : audio_(audio) {}
    // Runs the steps (each before its tick), then up to the `end` step, or to `endTick` when given
    // (which wins). Samples go to pcm (nullptr: not synthesised).
    void run(const SoundTestSchedule& schedule, std::vector<float>* pcm, std::optional<uint64_t> endTick = std::nullopt);

    int music() const { return music_; }      // the last music handle
    int effects() const { return effects_; }  // the last effect bank handle

private:
    void apply(const SoundTestStep& s);

    GgAudio& audio_;
    int music_ = -1;
    int effects_ = -1;
    int roomBuilding_ = -1;
    int song_ = -1;
};

// Runs a named schedule on a fresh offline GgAudio (fm, nothing synthesised) and compares its OPL
// writes with the schedule's capture file, as the schedule says (base, seed). Throws on a missing
// or broken file.
DroComparison checkSchedule(const GgExe& exe, const Archive& archive, const SoundTestSchedule& schedule,
                            const std::filesystem::path& captureFile);

}  // namespace tmport::gg
