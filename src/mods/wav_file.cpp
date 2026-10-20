#include "mods/wav_file.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

#include "util/byte_reader.h"
#include "util/error.h"

namespace tmport::mods {

namespace {

struct WavData {
    WavFormat fmt;
    std::span<const uint8_t> data;  // the data chunk
};

WavData parseWav(std::span<const uint8_t> w) {
    if (w.size() < 12 || std::memcmp(w.data(), "RIFF", 4) != 0 || std::memcmp(w.data() + 8, "WAVE", 4) != 0)
        throw FormatError("not a WAV file (RIFF/WAVE header missing)");
    ByteReader r(w, "WAV");
    WavData out;
    bool haveFmt = false, haveData = false;
    size_t p = 12;
    while (p + 8 <= w.size()) {
        const uint32_t len = r.u32(p + 4);
        const size_t avail = w.size() - p - 8;
        const std::span<const uint8_t> body = w.subspan(p + 8, std::min<size_t>(len, avail));
        if (std::memcmp(w.data() + p, "fmt ", 4) == 0) {
            if (body.size() < 16) throw FormatError("WAV: fmt chunk too short");
            ByteReader f(body, "WAV fmt");
            out.fmt.formatTag = f.u16(0);
            out.fmt.channels = f.u16(2);
            out.fmt.sampleRate = f.u32(4);
            out.fmt.bitsPerSample = f.u16(14);
            if (out.fmt.formatTag == 0xFFFE) {
                if (body.size() < 40) throw FormatError("WAV: extensible fmt chunk too short");
                out.fmt.formatTag = f.u16(24);  // first two bytes of the subformat GUID
            }
            haveFmt = true;
        } else if (std::memcmp(w.data() + p, "data", 4) == 0) {
            out.data = body;  // a data chunk cut short by the end of the file is accepted
            haveData = true;
        }
        p += 8 + static_cast<size_t>(len) + (len & 1);  // chunks are word-aligned
    }
    if (!haveFmt) throw FormatError("WAV: no fmt chunk");
    if (!haveData) throw FormatError("WAV: no data chunk");
    const WavFormat& f = out.fmt;
    if (f.formatTag != 1 && f.formatTag != 3)
        throw FormatError(std::format("WAV: format {} is not supported (save as uncompressed PCM)", f.formatTag));
    if (f.formatTag == 1 && f.bitsPerSample != 8 && f.bitsPerSample != 16 && f.bitsPerSample != 24 && f.bitsPerSample != 32)
        throw FormatError(std::format("WAV: {}-bit PCM is not supported (use 8 or 16 bits)", f.bitsPerSample));
    if (f.formatTag == 3 && f.bitsPerSample != 32)
        throw FormatError(std::format("WAV: {}-bit float is not supported (use 8 or 16-bit PCM)", f.bitsPerSample));
    if (f.channels < 1 || f.channels > 8) throw FormatError(std::format("WAV: {} channels are not supported", f.channels));
    if (f.sampleRate < 1000 || f.sampleRate > 192000) throw FormatError(std::format("WAV: sample rate {} Hz is not supported", f.sampleRate));
    const size_t frameBytes = static_cast<size_t>(f.channels) * static_cast<size_t>(f.bitsPerSample / 8);
    out.fmt.frames = out.data.size() / frameBytes;
    return out;
}

void putU16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void putU32(std::vector<uint8_t>& v, uint32_t x) {
    putU16(v, x & 0xFFFF);
    putU16(v, x >> 16);
}

}  // namespace

WavFormat parseWavFormat(std::span<const uint8_t> wav) { return parseWav(wav).fmt; }

std::vector<uint8_t> wavToPcm8Mono(std::span<const uint8_t> wav, uint32_t targetRate, std::string* note) {
    const WavData w = parseWav(wav);
    const WavFormat& f = w.fmt;
    if (f.frames == 0) throw FormatError("WAV: the sound is empty");
    std::vector<uint8_t> pcm;
    if (f.channels == 1 && f.bitsPerSample == 8 && f.formatTag == 1 && f.sampleRate == targetRate) {
        pcm.assign(w.data.begin(), w.data.begin() + static_cast<std::ptrdiff_t>(f.frames));
        if (note) *note = std::format("{} Hz 8-bit mono, copied", f.sampleRate);
    } else {
        // Mix down to mono doubles in [-1, 1].
        const size_t bytes = static_cast<size_t>(f.bitsPerSample / 8);
        std::vector<double> mono(f.frames);
        for (size_t i = 0; i < f.frames; ++i) {
            double sum = 0;
            for (int c = 0; c < f.channels; ++c) {
                const uint8_t* s = w.data.data() + (i * static_cast<size_t>(f.channels) + static_cast<size_t>(c)) * bytes;
                double v = 0;
                if (f.formatTag == 3) {
                    float fl;
                    std::memcpy(&fl, s, 4);
                    v = std::isfinite(fl) ? std::clamp(static_cast<double>(fl), -1.0, 1.0) : 0.0;
                } else if (bytes == 1) {
                    v = (static_cast<int>(s[0]) - 128) / 128.0;
                } else if (bytes == 2) {
                    v = static_cast<int16_t>(s[0] | (s[1] << 8)) / 32768.0;
                } else if (bytes == 3) {
                    int32_t x = s[0] | (s[1] << 8) | (s[2] << 16);
                    if (x & 0x800000) x -= 0x1000000;
                    v = x / 8388608.0;
                } else {
                    v = static_cast<int32_t>(static_cast<uint32_t>(s[0]) | (static_cast<uint32_t>(s[1]) << 8) |
                                             (static_cast<uint32_t>(s[2]) << 16) | (static_cast<uint32_t>(s[3]) << 24)) /
                        2147483648.0;
                }
                sum += v;
            }
            mono[i] = sum / f.channels;
        }
        // Resample.
        std::vector<double> res;
        if (f.sampleRate == targetRate) {
            res = std::move(mono);
        } else {
            const double ratio = static_cast<double>(f.sampleRate) / targetRate;  // source samples per output sample
            const size_t n = std::max<size_t>(1, static_cast<size_t>(static_cast<double>(f.frames) / ratio));
            res.resize(n);
            for (size_t i = 0; i < n; ++i) {
                if (ratio > 1.0) {
                    // Area average over [i*ratio, (i+1)*ratio).
                    const double a = static_cast<double>(i) * ratio, b = std::min(a + ratio, static_cast<double>(f.frames));
                    double acc = 0, wsum = 0;
                    for (size_t k = static_cast<size_t>(a); static_cast<double>(k) < b && k < f.frames; ++k) {
                        const double lo = std::max(a, static_cast<double>(k)), hi = std::min(b, static_cast<double>(k + 1));
                        if (hi > lo) {
                            acc += mono[k] * (hi - lo);
                            wsum += hi - lo;
                        }
                    }
                    res[i] = wsum > 0 ? acc / wsum : 0;
                } else {
                    const double pos = static_cast<double>(i) * ratio;
                    const size_t k = static_cast<size_t>(pos);
                    const double t = pos - static_cast<double>(k);
                    const double s0 = mono[std::min(k, f.frames - 1)], s1 = mono[std::min(k + 1, f.frames - 1)];
                    res[i] = s0 + (s1 - s0) * t;
                }
            }
        }
        pcm.resize(res.size());
        for (size_t i = 0; i < res.size(); ++i)
            pcm[i] = static_cast<uint8_t>(std::clamp<long>(std::lround(res[i] * 128.0 + 128.0), 0, 255));
        if (note)
            *note = std::format("{} Hz {}-bit{} {} -> {} Hz 8-bit mono", f.sampleRate, f.bitsPerSample, f.formatTag == 3 ? " float" : "",
                                f.channels == 1 ? "mono" : f.channels == 2 ? "stereo" : std::format("{} channels", f.channels),
                                targetRate);
    }
    return pcm;
}

std::vector<uint8_t> makeWav(uint32_t rate, int channels, int bitsPerSample, std::span<const uint8_t> samples) {
    std::vector<uint8_t> out;
    const uint32_t align = static_cast<uint32_t>(channels * bitsPerSample / 8);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    putU32(out, static_cast<uint32_t>(36 + samples.size() + (samples.size() & 1)));
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    putU32(out, 16);
    putU16(out, 1);
    putU16(out, static_cast<uint32_t>(channels));
    putU32(out, rate);
    putU32(out, rate * align);
    putU16(out, align);
    putU16(out, static_cast<uint32_t>(bitsPerSample));
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    putU32(out, static_cast<uint32_t>(samples.size()));
    out.insert(out.end(), samples.begin(), samples.end());
    if (samples.size() & 1) out.push_back(0);
    return out;
}

}  // namespace tmport::mods
