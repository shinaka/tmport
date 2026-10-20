#include "mods/wav_codec.h"

#include <format>

#include "mods/mod_common.h"
#include "res/ssnd.h"
#include "util/error.h"

namespace tmport::mods {

namespace {

void putU16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void putU32(std::vector<uint8_t>& v, uint32_t x) {
    putU16(v, x & 0xFFFF);
    putU16(v, x >> 16);
}

}  // namespace

std::vector<uint8_t> wavToSsnd(std::span<const uint8_t> wav, uint32_t targetRate, std::string* note) {
    const std::vector<uint8_t> pcm = wavToPcm8Mono(wav, targetRate, note);
    std::vector<uint8_t> out;
    out.reserve(roundUpNe(12 + pcm.size()));
    putU32(out, 0);
    putU32(out, targetRate);
    putU32(out, static_cast<uint32_t>(pcm.size()));
    out.insert(out.end(), pcm.begin(), pcm.end());
    padToNe(out);
    return out;
}

std::vector<uint8_t> ssndToWav(std::span<const uint8_t> ssnd) {
    const auto info = parseSsnd(ssnd);
    if (!info) throw FormatError("SSND: shorter than its 12-byte header");
    if (info->sampleRate < 1000 || info->sampleRate > 192000)
        throw FormatError(std::format("SSND: unusual sample rate {}", info->sampleRate));
    return makeWav(info->sampleRate, 1, 8, info->pcm(ssnd));
}

}  // namespace tmport::mods
