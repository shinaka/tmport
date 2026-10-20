#include "gg/audio/soundtest.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <stdexcept>

#include "gg/audio/dro_compare.h"
#include "gg/audio/gg_audio.h"
#include "util/error.h"
#include "util/file_util.h"

namespace tmport::gg {

namespace {

struct Named {
    std::string_view name;
    std::string_view capture;  // the .dro under the captures' audio folder
    bool dumpBase;             // CompareBase::CaptureDump
    bool seed;                 // the B0h shadows from the capture's dump
    std::string_view description;
    std::string_view script;
};

// The schedules of the DOSBox-X captures (fmt_sound.md §5), found by matching the captures write
// for write: what the game called, at which sound tick of an engine run that starts with it. The
// comparison (dro_compare.h) finds where in the run each capture opens. Effect priorities that no
// channel conflict reveals are the call sites' (code_sound.md §15) or 4.
constexpr Named kNamed[] = {
    {"title", "w0_title_attract.dro", false, false,
     "attract (State_01_Attract 2c11:0913): effects 0x1022, title music 0x1020; the title animation's effects 12 and 13 "
     "(2c11:043f) and a screen animation's effect 0 (stopped by it)",
     "0:e:1022,0:m:1020,0:p,80:f:12:0:4,301:f:13:0:4,789:f:0:1:1,885:s:0,5000:end"},
    {"title_replay", "w0_replay_title_attract.dro", false, false, "the same, second boot (effect 13 one tick earlier)",
     "0:e:1022,0:m:1020,0:p,80:f:12:0:4,300:f:13:0:4,789:f:0:1:1,885:s:0,4000:end"},
    {"hub", "r2_hub_idle.dro", true, false, "hub (State_03_Hub 12e0:0167): title music, arrangement 2",
     "0:e:102f,0:m:1020,0:p,0:a:2,9000:end"},
    {"postrace", "r2_postrace_to_hub.dro", true, false, "post-race menu to the hub: title music, arrangement 2",
     "0:m:1020,0:p,0:a:2,1000:end"},
    {"warehouse", "r2_warehouse_walk.dro", true, false,
     "Aircraft warehouse room 1 (room music 0x361 arrangement 1 with the chimp layer, effects 0x1039): effect 15 started "
     "while effect 17 holds channel 0 at the same priority gets the channel (and its patch) when 17 ends",
     "0:R:1:1:1:1,0:e:1039,976:f:15:0:4,1138:f:17:0:4,1140:f:15:0:4,2000:end"},
    {"workshop", "r2_workshop_idle.dro", true, false,
     "workshop (State_06_Workshop 2dee:1a90): music 0x27 arrangement 1, belt and race-door effects (the door animation "
     "plays 13 and 14 at priority 3, Workshop_UpdateRaceDoor 14 again at 4)",
     "0:W,0:f:10:0:5,786:f:3:0:1,786:f:13:0:3,798:f:14:0:3,818:f:14:0:4,1102:f:4:0:1,1126:f:5:0:2,1150:s:5,"
     "1150:f:6:0:3,1152:f:6:0:3,1276:f:4:0:1,1300:f:5:0:2,1324:f:6:0:3,1326:s:5,1326:f:6:0:3,1800:end"},
    {"puzzle_k4", "r2_puzzle_k4_success.dro", true, false,
     "balance puzzle solved (PzSnd 3924, bank 0x11c6): a weight taken and hung, five swing steps of effect 8 transposed "
     "0..4, the solved effects",
     "0:R:1:1,0:x,0:P:11c6,100:y:0:3,213:y:1:3,215:y:8:3,240:T:8:1,240:y:8:3,265:T:8:2,265:y:8:3,290:T:8:3,"
     "290:y:8:3,315:T:8:4,315:y:8:3,340:y:5:3,340/6x4:y:13:3,700:end"},
    {"puzzle_k5", "r2_puzzle_k5_miss.dro", true, false, "launch puzzle, a miss (PzSnd 3924, bank 0x11ce)",
     "0:R:1:1,0:x,0:P:11ce,100:y:0:3,260:y:1:3,371:y:4:3,382:y:7:3,406:y:2:4,700:end"},
    {"race_automotive", "r2_automotive_race1_won.dro", true, true,
     "workshop music freed, land race (music 0xF7 arrangements 1, 2), results (effects 0x108F, music 0x1089 once, the "
     "count-up's global effect 1 every 2 ticks, effect 0 under it, effect 1 at the rank-up)",
     "0:W,0:f:10:0:5,514:M,514:E,514:m:f7,514:a:1,514:p,514:e:f4,826:a:2,1050:x,1054:e:108f,1054:m:1089,1057:g:1:0:5,"
     "1057:p:0,1076/2x49:g:1:0:5,1178:f:0:0:4,1438:f:1:0:4,3000:end"},
    {"race_altenergy", "r2_altenergy_race1_won.dro", true, true,
     "the same for the Alternative Energy race (music 0xF9, arrangements 1, 2, 3, 2; the count-up on odd PIT ticks)",
     "0:W,0:f:10:0:5,367:M,367:E,367:m:f9,367:a:1,367:p,367:e:f4,574:a:2,1199:a:3,1307:a:2,1682:x,1682:M,1682:E,"
     "1682:e:108f,1682:m:1089,1685:p:0,1685:g:1:0:5,1705:g:1:0:5,1706/2x48:g:1:0:5,1806:f:0:0:4,2066:f:1:0:4,3300:end"},
    {"race_aircraft_won", "r2_aircraft_race1_won.dro", true, true,
     "aircraft race (music 0x262, effects 0x260 at priority 3: 8, 7, 9; arrangements 1, 2, 3, 2), results",
     "0:W,0:f:10:0:5,821:M,821:E,829:m:262,829:e:260,829:a:1,829:p,921:a:2,934:f:8:0:3,938:a:3,1278:s:8,1282:f:7:0:3,"
     "1453:a:2,1937:f:7:0:3,2135:s:7,2138:f:9:0:3,2302:x,2302:S,2305:M,2305:E,2305:e:108f,2305:m:1089,2308:g:1:0:5,"
     "2308:p:0,2327/2x49:g:1:0:5,2429:f:0:0:4,2689:f:1:0:4,3300:end"},
    {"race_aircraft_lost", "r2_aircraft_race1_lost.dro", true, true,
     "aircraft race lost (the engine effect 7 looping, then 9), results of a loss (music 0x108B once, no count-up)",
     "0:W,0:f:10:0:5,542:M,542:E,551:m:262,551:e:260,551:a:1,551:p,643:a:2,656:f:8:0:3,660:a:3,1000:s:8,1004:f:7:1:3,"
     "2064:s:7,2064:f:9:0:3,2268:x,2268:S,2270:M,2270:E,2270:e:108f,2270:m:108b,2273:p:0,2548:f:1:0:4,3300:end"},
    {"movie", "r2_movie_race15.dro", true, true,
     "the 15th race won, results and the final movie (State_15_FinalMovie 2fcb:0640: music 0x30CA with Music_MovieCue "
     "switching the arrangement every 592-tick pass; the cue table's effects of 0x108D: 0, 1 looping, 9 every 4 frames, "
     "5, 2). Matched up to the movie's end (7050 of 8320 writes); the rest is not reconstructed",
     "0:W,0:f:10:0:5,359:M,359:E,367:m:262,367:e:260,367:a:1,367:p,459:a:2,471:f:8:0:3,475:a:3,816:s:8,816:f:7:0:3,"
     "991:a:2,1471:f:7:0:3,1675:s:7,1675:f:9:0:3,1840:x,1840:S,1843:M,1843:E,1843:e:108f,1843:m:1089,1846:g:1:0:5,"
     "1846:p:0,1866/2x49:g:1:0:5,1968:f:0:0:4,2228:f:1:0:4,3295:V,5686:f:0:0:0,5812:f:1:1:1,5929:f:9:0:2,5965:f:9:0:2,"
     "6001:f:9:0:2,6036:f:9:0:2,6073:f:9:0:2,6108:f:9:0:2,6145:f:9:0:2,6181:f:9:0:2,6217:f:9:0:2,6253:f:9:0:2,"
     "7361:f:5:0:2,7388:f:2:0:1,7398:s:1,7406:f:6:0:2,9500:end"},
};

bool isHexCommand(std::string_view cmd) { return cmd == "m" || cmd == "e" || cmd == "P" || cmd == "h"; }

int toInt(std::string_view s, int base) {
    int v = 0;
    if (base == 16 && (s.starts_with("0x") || s.starts_with("0X"))) s.remove_prefix(2);
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (s.empty() || r.ec != std::errc() || r.ptr != s.data() + s.size()) throw std::invalid_argument(std::format("bad number '{}'", s));
    return v;
}

int arg(const SoundTestStep& s, size_t i, int dflt) { return i < s.args.size() ? s.args[i] : dflt; }

}  // namespace

SoundTestSchedule SoundTestSchedule::parse(std::string_view script) {
    SoundTestSchedule sch;
    while (!script.empty()) {
        const size_t comma = script.find(',');
        std::string_view item = script.substr(0, comma);
        script = comma == std::string_view::npos ? std::string_view{} : script.substr(comma + 1);
        std::vector<std::string_view> f;
        while (true) {
            const size_t c = item.find(':');
            f.push_back(item.substr(0, c));
            if (c == std::string_view::npos) break;
            item = item.substr(c + 1);
        }
        if (f.size() < 2) throw std::invalid_argument(std::format("soundtest step '{}': expected TICK:CMD[:ARG...]", f[0]));
        SoundTestStep s;
        // TICK, or A/BxN: N steps at ticks A, A + B, ...
        uint64_t first = 0, stride = 0, count = 1;
        const size_t slash = f[0].find('/'), x = f[0].find('x');
        if (slash != std::string_view::npos && x != std::string_view::npos && x > slash) {
            first = static_cast<uint64_t>(toInt(f[0].substr(0, slash), 10));
            stride = static_cast<uint64_t>(toInt(f[0].substr(slash + 1, x - slash - 1), 10));
            count = static_cast<uint64_t>(toInt(f[0].substr(x + 1), 10));
        } else {
            first = static_cast<uint64_t>(toInt(f[0], 10));
        }
        s.cmd = std::string(f[1]);
        static constexpr std::string_view kCmds[] = {"m", "p", "a", "x", "M", "e", "f", "s", "E", "S", "g", "t", "q", "z", "r", "R", "W", "V", "P", "y", "Y", "u", "U", "T", "L", "h", "H", "end"};
        if (std::find(std::begin(kCmds), std::end(kCmds), s.cmd) == std::end(kCmds))
            throw std::invalid_argument(std::format("soundtest: unknown command '{}'", s.cmd));
        for (size_t i = 2; i < f.size(); ++i) s.args.push_back(toInt(f[i], isHexCommand(s.cmd) ? 16 : 10));
        for (uint64_t k = 0; k < count; ++k) {
            s.tick = first + k * stride;
            sch.steps.push_back(s);
        }
    }
    std::stable_sort(sch.steps.begin(), sch.steps.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    return sch;
}

std::optional<SoundTestSchedule> SoundTestSchedule::named(std::string_view name) {
    for (const Named& n : kNamed)
        if (n.name == name) {
            SoundTestSchedule s = parse(n.script);
            s.name = std::string(n.name);
            s.description = std::string(n.description);
            s.capture = std::string(n.capture);
            s.compareWithDump = n.dumpBase;
            s.seedFromCapture = n.seed;
            return s;
        }
    return std::nullopt;
}

std::vector<std::string> SoundTestSchedule::names() {
    std::vector<std::string> v;
    for (const Named& n : kNamed) v.emplace_back(n.name);
    return v;
}

void SoundTest::apply(const SoundTestStep& s) {
    const std::string& c = s.cmd;
    if (c == "m") {
        music_ = audio_.loadMusicBank(static_cast<uint16_t>(arg(s, 0, 0)));
    } else if (c == "p") {
        audio_.playMusic(music_, 0, arg(s, 0, 1));
    } else if (c == "a") {
        audio_.selectArrangement(music_, arg(s, 0, 0));
    } else if (c == "x") {
        audio_.stopMusic(music_);
    } else if (c == "M") {
        audio_.freeMusicBank(music_);
        music_ = -1;
    } else if (c == "e") {
        effects_ = audio_.loadEffectBank(static_cast<uint16_t>(arg(s, 0, 0)));
        audio_.setEffectBank(effects_);
    } else if (c == "f") {
        audio_.playEffect(effects_ + arg(s, 0, 0), arg(s, 1, 0), arg(s, 2, 4));
    } else if (c == "s") {
        audio_.stopEffect(effects_ + arg(s, 0, 0));
    } else if (c == "E") {
        audio_.freeEffectBank(effects_);
        effects_ = -1;
    } else if (c == "S") {
        audio_.stopAllEffects();
    } else if (c == "g") {
        audio_.call([&](SndRes& r) { r.playEffect(r.globalEffects() + arg(s, 0, 0), arg(s, 1, 0), arg(s, 2, 4)); });
    } else if (c == "t") {
        audio_.call([&](SndRes& r) { return r.driver().setTranspose(r.slot(music_), 0, arg(s, 0, 0)); });
    } else if (c == "q") {
        audio_.toggleSound();
    } else if (c == "z") {
        audio_.pause();
    } else if (c == "r") {
        audio_.resume();
    } else if (c == "R") {
        const int b = arg(s, 0, 0);
        audio_.call([&](SndRes& r) {
            MusicCues& cues = audio_.cues();
            if (b != roomBuilding_) {  // Rooms_Load 1622:0096
                if (cues.roomMusicBank() >= 0) r.freeMusicBank(cues.roomMusicBank());
                cues.setRoomMusicBank(r.loadMusicBank(static_cast<uint16_t>(MusicCues::kRoomMusicBank + 2 * b)));
                cues.setBuilding(b);
                roomBuilding_ = b;
            }
            cues.roomMusicSelectLayers(arg(s, 1, 1), arg(s, 2, 0) != 0, arg(s, 3, 1));
            cues.roomMusicStart();
        });
        music_ = audio_.cues().roomMusicBank();
    } else if (c == "W") {
        effects_ = audio_.loadEffectBank(MusicCues::kWorkshopEffectBank);
        audio_.setEffectBank(effects_);
        music_ = audio_.loadMusicBank(MusicCues::kWorkshopMusicBank);
        audio_.call([&](SndRes&) { audio_.cues().workshopMusicStart(music_); });
    } else if (c == "h") {  // Hint_Show 3060:013b: the chime as a single song, SndRes_PlayEffect(h, 0, 32h)
        song_ = audio_.loadSong(static_cast<uint16_t>(arg(s, 0, 0)));
        audio_.playEffect(song_, 0, arg(s, 1, 0x32));
    } else if (c == "H") {
        audio_.freeSong(song_);
        song_ = -1;
    } else if (c == "P") {
        audio_.call([&](SndRes&) { audio_.puzzles().loadBank(static_cast<uint16_t>(arg(s, 0, 0))); });
    } else if (c == "y" || c == "Y") {
        audio_.call([&](SndRes&) {
            if (c == "y") audio_.puzzles().play(arg(s, 0, 0), arg(s, 1, 3));
            else audio_.puzzles().playLoop(arg(s, 0, 0), arg(s, 1, 3));
        });
    } else if (c == "u") {
        audio_.call([&](SndRes&) { audio_.puzzles().stop(arg(s, 0, 0)); });
    } else if (c == "U") {
        audio_.call([&](SndRes&) { audio_.puzzles().stopAll(); });
    } else if (c == "T") {
        audio_.call([&](SndRes&) { audio_.puzzles().setTranspose(arg(s, 0, 0), arg(s, 1, 0)); });
    } else if (c == "V") {
        music_ = audio_.loadMusicBank(MusicCues::kMovieMusicBank);
        audio_.call([&](SndRes&) { audio_.cues().movieMusicStart(music_); });
        effects_ = audio_.loadEffectBank(MusicCues::kMovieEffectBank);
    }
}

void SoundTest::run(const SoundTestSchedule& schedule, std::vector<float>* pcm, std::optional<uint64_t> endTick) {
    for (const SoundTestStep& s : schedule.steps) {
        if (endTick && s.tick >= *endTick) break;
        audio_.advanceTo(std::max(s.tick, audio_.tickIndex()), pcm);
        if (s.cmd == "L") {  // the tick is lost: the driver is busy while it falls (Snd_Tick 1bb4:0db3)
            audio_.call([](SndRes& r) { r.driver().setBusy(true); });
            audio_.advanceTo(audio_.tickIndex() + 1, pcm);
            audio_.call([](SndRes& r) { r.driver().setBusy(false); });
            continue;
        }
        if (s.cmd == "end") {
            if (!endTick) return;
            break;
        }
        apply(s);
    }
    if (endTick) audio_.advanceTo(std::max(*endTick, audio_.tickIndex()), pcm);
}

DroComparison checkSchedule(const GgExe& exe, const Archive& archive, const SoundTestSchedule& schedule,
                            const std::filesystem::path& captureFile) {
    const auto bytes = readWholeFile(captureFile);
    if (!bytes) throw Error("cannot read " + pathToUtf8(captureFile));
    const DroFile dro = parseDro(*bytes);
    GgAudioConfig cfg;
    cfg.mode = SoundMode::Fm;
    cfg.logOpl = true;
    GgAudio audio(exe, archive, cfg);
    if (schedule.seedFromCapture) audio.call([&](SndRes& r) { seedB0Shadows(r.driver(), dro); });
    SoundTest(audio).run(schedule, nullptr);
    return compareWithDro(dro, audio.oplLog().entries(), 0, GgAudio::kOplWriteSeconds * 1000.0,
                          schedule.compareWithDump ? CompareBase::CaptureDump : CompareBase::Engine);
}

}  // namespace tmport::gg
