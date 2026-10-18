#pragma once
// Digitised sounds (SSND resources, re/docs/fmt_sound.md §3): decoded PCM, WAV export and the
// loader that fetches a sound id from the user's sound DLLs.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace tmport {
class ResourceManager;
}

namespace tmport::audio {

// One sound: 8-bit unsigned mono PCM (0x80 = silence) at sampleRate (22050 Hz for every shipped
// sound). Wave_Play (1088:0014) passes the header rate as nSamplesPerSec and the sample count as
// the buffer length.
struct Sound {
    int id = -1;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> pcm;

    // Play time in microseconds (0 for an empty sound or a zero rate).
    uint64_t durationUs() const {
        return sampleRate ? static_cast<uint64_t>(pcm.size()) * 1'000'000u / sampleRate : 0;
    }
    double seconds() const { return sampleRate ? static_cast<double>(pcm.size()) / sampleRate : 0.0; }
};

// Decodes an SSND resource body (NE padding allowed). nullopt if it is shorter than the 12-byte
// header or has a zero sample rate. A sample count larger than the body is clipped.
std::optional<Sound> soundFromSsnd(std::span<const uint8_t> resource, int id = -1);

// Samples over which the end of a sound is faded out for playback (about 1.5 ms at 22050 Hz).
inline constexpr size_t kFadeOutSamples = 32;
// Fades the last kFadeOutSamples (or fewer) samples of 8-bit unsigned PCM linearly to silence
// (0x80); the final sample becomes exactly 0x80. About a third of the shipped sounds end with one
// stray byte after their trailing silence (..., 0x80, 0x80, 0xCE) because the SSND sample count
// takes it in; played as is, that byte is a click at the end of the sound. Playback only: the
// decoded Sound and WAV exports keep the original bytes (PORTING_NOTES §6.1).
void fadeOutTail(std::span<uint8_t> pcm);

// RIFF/WAVE file: PCM, 1 channel, 8 bits, the sound's rate. WAV 8-bit data is unsigned like SSND,
// so the samples are copied unchanged. Byte-identical to Python's wave module as used by
// tools/assets/ssnd.py (odd data lengths get a zero pad byte that the RIFF size includes).
std::vector<uint8_t> makeWav(const Sound& s);

// Source of sounds by id (Res_LockInPlace 1038:0ba8 / Res_LoadCopy 1038:08c5).
class SoundLoader {
public:
    virtual ~SoundLoader() = default;
    // nullptr if the sound does not exist (the original then plays nothing).
    virtual std::shared_ptr<const Sound> load(int id) = 0;
};

// Loads SSND resources through the ResourceManager (id -> sound DLL routing, mod hook). No cache:
// the original reloads every sound, and a load is a single small read.
class ResourceSoundLoader final : public SoundLoader {
public:
    explicit ResourceSoundLoader(const ResourceManager& res) : res_(res) {}
    std::shared_ptr<const Sound> load(int id) override;

private:
    const ResourceManager& res_;
};

}  // namespace tmport::audio
