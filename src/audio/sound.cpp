#include "audio/sound.h"

#include <algorithm>

#include "res/resource_manager.h"
#include "res/ssnd.h"
#include "util/log.h"

namespace tmport::audio {

std::optional<Sound> soundFromSsnd(std::span<const uint8_t> resource, int id) {
    const auto info = parseSsnd(resource);
    if (!info || info->sampleRate == 0) return std::nullopt;
    Sound s;
    s.id = id;
    s.sampleRate = info->sampleRate;
    const auto pcm = info->pcm(resource);
    s.pcm.assign(pcm.begin(), pcm.end());
    return s;
}

void fadeOutTail(std::span<uint8_t> pcm) {
    const size_t n = std::min(pcm.size(), kFadeOutSamples);
    uint8_t* tail = pcm.data() + (pcm.size() - n);
    for (size_t i = 0; i < n; ++i) {
        const int gain = static_cast<int>(n - 1 - i);  // n-1 .. 0, out of n
        const int v = (static_cast<int>(tail[i]) - 0x80) * gain / static_cast<int>(n);
        tail[i] = static_cast<uint8_t>(0x80 + v);
    }
}

namespace {

void putTag(std::vector<uint8_t>& out, const char (&tag)[5]) { out.insert(out.end(), tag, tag + 4); }
void putU16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    putU16(out, v & 0xFFFF);
    putU16(out, v >> 16);
}

}  // namespace

std::vector<uint8_t> makeWav(const Sound& s) {
    const uint32_t dataLen = static_cast<uint32_t>(s.pcm.size());
    const uint32_t pad = dataLen & 1u;
    std::vector<uint8_t> out;
    out.reserve(44 + dataLen + pad);
    putTag(out, "RIFF");
    putU32(out, 36 + dataLen + pad);
    putTag(out, "WAVE");
    putTag(out, "fmt ");
    putU32(out, 16);            // fmt chunk size
    putU16(out, 1);             // WAVE_FORMAT_PCM
    putU16(out, 1);             // channels
    putU32(out, s.sampleRate);  // samples per second
    putU32(out, s.sampleRate);  // bytes per second (1 channel x 1 byte)
    putU16(out, 1);             // block align
    putU16(out, 8);             // bits per sample
    putTag(out, "data");
    putU32(out, dataLen);
    out.insert(out.end(), s.pcm.begin(), s.pcm.end());
    if (pad) out.push_back(0);
    return out;
}

std::shared_ptr<const Sound> ResourceSoundLoader::load(int id) {
    auto raw = res_.loadRaw(ResType::SSND, id);
    if (!raw) {
        log::warn("sound {} does not exist", id);
        return nullptr;
    }
    auto s = soundFromSsnd(*raw, id);
    if (!s) {
        log::warn("sound {} is not a valid SSND resource", id);
        return nullptr;
    }
    return std::make_shared<const Sound>(std::move(*s));
}

}  // namespace tmport::audio
