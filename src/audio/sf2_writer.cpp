#include "audio/sf2_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace tmport::audio {

namespace {

// SoundFont 2.01 generator operators used here.
enum Gen : uint16_t {
    kGenReleaseVolEnv = 38,
    kGenInstrument = 41,
    kGenSampleId = 53,
    kGenSampleModes = 54,
    kGenScaleTuning = 56,
};

constexpr int kCycles = 20;
constexpr int kLoopFirstCycle = 2;
constexpr int kLoopEndCycle = 18;  // exclusive
constexpr int kGuard = 46;         // zero samples after each sample (SF2 2.01 §6.1)
constexpr int16_t kReleaseTimecents = -5186;  // 1200 * log2(0.05 s)

class Bytes {
public:
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    }
    void id(std::string_view four) { b.insert(b.end(), four.begin(), four.begin() + 4); }
    void name20(std::string_view s) {
        char n[20] = {};
        std::memcpy(n, s.data(), std::min<size_t>(s.size(), 19));
        b.insert(b.end(), n, n + 20);
    }
    void append(const std::vector<uint8_t>& o) { b.insert(b.end(), o.begin(), o.end()); }
};

std::vector<uint8_t> chunk(std::string_view id, const std::vector<uint8_t>& payload) {
    Bytes o;
    o.id(id);
    o.u32(static_cast<uint32_t>(payload.size()));
    o.append(payload);
    if (payload.size() & 1) o.u8(0);
    return o.b;
}

std::vector<uint8_t> list(std::string_view type, std::initializer_list<std::vector<uint8_t>> chunks) {
    Bytes p;
    p.id(type);
    for (const auto& c : chunks) p.append(c);
    return chunk("LIST", p.b);
}

std::vector<uint8_t> zstr(std::string_view s) {
    std::vector<uint8_t> v(s.begin(), s.end());
    v.push_back(0);
    if (v.size() & 1) v.push_back(0);
    return v;
}

struct SampleDef {
    const char* name;
    bool square;
};
constexpr SampleDef kSamples[] = {{"sine", false}, {"square", true}};

struct InstDef {
    const char* name;
    uint16_t sample;
    bool fixedPitch;
};
constexpr InstDef kInsts[] = {{"Test Sine", 0, false}, {"Test Square", 1, false}, {"Test Drum", 1, true}};

struct PresetDef {
    const char* name;
    uint16_t program, bank, inst;
};
constexpr PresetDef kPresets[] = {
    {"Test Sine", testsf2::kSineProgram, 0, 0},
    {"Test Square", testsf2::kSquareProgram, 0, 1},
    {"Test Drums", 0, testsf2::kDrumBank, 2},
};

}  // namespace

std::vector<uint8_t> makeTestSoundFont() {
    using namespace testsf2;
    constexpr uint32_t len = kCycles * kCycle;
    constexpr double kPi = 3.14159265358979323846;

    // sdta: the samples, each followed by its zero guard.
    Bytes smpl;
    for (const auto& s : kSamples) {
        for (uint32_t i = 0; i < len; ++i) {
            const int phase = static_cast<int>(i % kCycle);
            const double v = s.square ? (phase < kCycle / 2 ? 0.5 : -0.5) : 0.5 * std::sin(2.0 * kPi * phase / kCycle);
            smpl.i16(static_cast<int16_t>(std::lround(v * 32767.0)));
        }
        for (int i = 0; i < kGuard; ++i) smpl.i16(0);
    }

    // pdta: presets -> instruments -> samples.
    Bytes phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    uint16_t bagIndex = 0;
    for (const auto& p : kPresets) {
        phdr.name20(p.name);
        phdr.u16(p.program);
        phdr.u16(p.bank);
        phdr.u16(bagIndex);
        phdr.u32(0);
        phdr.u32(0);
        phdr.u32(0);
        pbag.u16(bagIndex);  // one zone per preset, one generator per zone
        pbag.u16(0);
        pgen.u16(kGenInstrument);
        pgen.u16(p.inst);
        ++bagIndex;
    }
    phdr.name20("EOP");
    phdr.u16(0);
    phdr.u16(0);
    phdr.u16(bagIndex);
    phdr.u32(0);
    phdr.u32(0);
    phdr.u32(0);
    pbag.u16(bagIndex);
    pbag.u16(0);
    pgen.u32(0);
    for (int i = 0; i < 10; ++i) pmod.u8(0);  // terminal modulator only

    uint16_t genIndex = 0, instBag = 0;
    for (const auto& in : kInsts) {
        inst.name20(in.name);
        inst.u16(instBag);
        ibag.u16(genIndex);
        ibag.u16(0);
        igen.u16(kGenReleaseVolEnv);
        igen.i16(kReleaseTimecents);
        igen.u16(kGenSampleModes);
        igen.u16(1);  // loop continuously
        genIndex += 2;
        if (in.fixedPitch) {
            igen.u16(kGenScaleTuning);
            igen.u16(0);
            ++genIndex;
        }
        igen.u16(kGenSampleId);  // must be the zone's last generator
        igen.u16(in.sample);
        ++genIndex;
        ++instBag;
    }
    inst.name20("EOI");
    inst.u16(instBag);
    ibag.u16(genIndex);
    ibag.u16(0);
    igen.u32(0);
    for (int i = 0; i < 10; ++i) imod.u8(0);

    uint32_t start = 0;
    for (const auto& s : kSamples) {
        shdr.name20(s.name);
        shdr.u32(start);
        shdr.u32(start + len);
        shdr.u32(start + kLoopFirstCycle * kCycle);
        shdr.u32(start + kLoopEndCycle * kCycle);
        shdr.u32(kSampleRate);
        shdr.u8(kRootKey);
        shdr.u8(0);   // pitch correction
        shdr.u16(0);  // sample link
        shdr.u16(1);  // mono sample
        start += len + kGuard;
    }
    shdr.name20("EOS");
    for (int i = 0; i < 26; ++i) shdr.u8(0);

    Bytes ifil;
    ifil.u16(2);
    ifil.u16(1);
    Bytes body;
    body.id("sfbk");
    body.append(list("INFO", {chunk("ifil", ifil.b), chunk("isng", zstr("EMU8000")), chunk("INAM", zstr("TM Port test tones"))}));
    body.append(list("sdta", {chunk("smpl", smpl.b)}));
    body.append(list("pdta", {chunk("phdr", phdr.b), chunk("pbag", pbag.b), chunk("pmod", pmod.b), chunk("pgen", pgen.b),
                              chunk("inst", inst.b), chunk("ibag", ibag.b), chunk("imod", imod.b), chunk("igen", igen.b),
                              chunk("shdr", shdr.b)}));
    return chunk("RIFF", body.b);
}

}  // namespace tmport::audio
