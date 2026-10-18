#include "audio/audio_tables.h"

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"

namespace tmport::audio {

namespace {

std::vector<uint16_t> readStopList(const ExeTables& exe, uint16_t off) {
    std::vector<uint16_t> codes;
    for (uint32_t p = off; codes.size() < 64; p += 2) {
        const uint16_t c = exe.u16(p);
        if (c == 0) break;
        codes.push_back(c);
    }
    return codes;
}

std::string asString(std::span<const uint8_t> b) { return std::string(b.begin(), b.end()); }

// g_musicId (i16), the scene tune Music_PlayZone (1128:002d..) stores and Music_Resume replays.
// Not in data/dgroup_offsets.h (owned by another area); TODO(integration): move it there.
constexpr uint16_t kMusicIdInitial = 0x13CA;

}  // namespace

int AudioTables::tuneIndex(int tuneId) const {
    for (size_t i = 0; i < kTunes; ++i)
        if (tuneIds[i] == tuneId) return static_cast<int>(i);
    return -1;
}

AudioTables AudioTables::fromExe(const ExeTables& exe) {
    AudioTables t;
    t.tuneIds = exe.array<int16_t, kTunes>(dg::kMusicIds);
    t.loopPos = exe.array<int16_t, kTunes>(dg::kMusicLoopPos);
    for (size_t i = 0; i < kTunes; ++i) t.tuneFiles[i] = asString(exe.farCstr(dg::kMusicFiles + static_cast<uint32_t>(4 * i)));
    t.soundsDir = asString(exe.cstr(dg::kStrSoundsDir));
    t.soundOn = exe.u8(dg::kSoundOn);
    t.voiceOn = exe.u8(dg::kVoiceOn);
    t.initialTuneId = exe.i16(kMusicIdInitial);
    static constexpr uint16_t kLists[kStopListCount] = {dg::kClueAbortKeys,     dg::kHintVoiceStopKeys,
                                                        dg::kQuizResultStopKeys, dg::kQuizSorryStopKeys,
                                                        dg::kQuizLineStopKeys,   dg::kQuizAnswerStopKeys};
    for (size_t i = 0; i < kStopListCount; ++i) t.stopLists[i] = readStopList(exe, kLists[i]);
    return t;
}

}  // namespace tmport::audio
