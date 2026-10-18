#include "audio/sf2_synth.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>

// TinySoundFont, compiled here and only here. Its include directory is a SYSTEM include of tmcore;
// the pragmas keep its own warnings out of the build log on every compiler.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define TSF_NO_STDIO  // files are read through std::ifstream (Unicode paths on Windows)
#define TSF_IMPLEMENTATION
#include "tsf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace tmport::audio {

namespace {

constexpr int kChannels = 16;
constexpr int kDrumChannel = 9;  // MIDI channel 10

int clampRate(int rate) { return std::clamp(rate, 8000, 192000); }

// tsf_stream over a binary file.
int streamRead(void* data, void* ptr, unsigned int size) {
    auto* in = static_cast<std::ifstream*>(data);
    in->read(static_cast<char*>(ptr), static_cast<std::streamsize>(size));
    return static_cast<int>(in->gcount());
}
int streamSkip(void* data, unsigned int count) {
    auto* in = static_cast<std::ifstream*>(data);
    in->seekg(static_cast<std::streamoff>(count), std::ios::cur);
    return in->good() ? 1 : 0;
}

}  // namespace

std::unique_ptr<Sf2Synth> Sf2Synth::loadFile(const std::filesystem::path& path, int sampleRate, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open the file";
        return nullptr;
    }
    char head[12] = {};
    in.read(head, sizeof head);
    if (in.gcount() != 12 || std::string_view(head, 4) != "RIFF" || std::string_view(head + 8, 4) != "sfbk") {
        error = "not a SoundFont 2 file (no RIFF 'sfbk' header)";
        return nullptr;
    }
    in.seekg(0);
    tsf_stream s{&in, &streamRead, &streamSkip};
    ::tsf* f = tsf_load(&s);
    if (!f) {
        error = "not a usable SoundFont 2 file (damaged, incomplete, or compressed .sf3 samples)";
        return nullptr;
    }
    if (tsf_get_presetcount(f) <= 0) {
        tsf_close(f);
        error = "the SoundFont has no presets";
        return nullptr;
    }
    return std::unique_ptr<Sf2Synth>(new Sf2Synth(f, clampRate(sampleRate)));
}

std::unique_ptr<Sf2Synth> Sf2Synth::loadMemory(std::span<const uint8_t> sf2, int sampleRate, std::string& error) {
    if (sf2.size() < 12 || std::memcmp(sf2.data(), "RIFF", 4) != 0 || std::memcmp(sf2.data() + 8, "sfbk", 4) != 0) {
        error = "not a SoundFont 2 file (no RIFF 'sfbk' header)";
        return nullptr;
    }
    ::tsf* f = tsf_load_memory(sf2.data(), static_cast<int>(sf2.size()));
    if (!f) {
        error = "not a usable SoundFont 2 file";
        return nullptr;
    }
    if (tsf_get_presetcount(f) <= 0) {
        tsf_close(f);
        error = "the SoundFont has no presets";
        return nullptr;
    }
    return std::unique_ptr<Sf2Synth>(new Sf2Synth(f, clampRate(sampleRate)));
}

Sf2Synth::Sf2Synth(::tsf* f, int rate) : f_(f), rate_(rate) {
    tsf_set_output(f_, TSF_STEREO_INTERLEAVED, rate_, kDefaultGainDb);
    std::lock_guard lock(mu_);
    initChannelsLocked();
}

Sf2Synth::~Sf2Synth() {
    if (f_) tsf_close(f_);
}

int Sf2Synth::presetCount() const {
    std::lock_guard lock(mu_);
    return tsf_get_presetcount(f_);
}

std::string Sf2Synth::presetName(int bank, int program) const {
    std::lock_guard lock(mu_);
    const char* n = tsf_bank_get_presetname(f_, bank, program);
    return n ? std::string(n) : std::string();
}

void Sf2Synth::initChannelsLocked() {
    tsf_channel_set_volume(f_, kChannels - 1, 1.0f);  // allocates all 16 channels at once
    for (int ch = 0; ch < kChannels; ++ch) {
        tsf_channel_set_bank(f_, ch, 0);
        programLocked(ch, 0);
        volume_[ch] = 100;
        expression_[ch] = 127;
        applyVolumeLocked(ch);
        tsf_channel_set_pan(f_, ch, 0.5f);
        tsf_channel_set_pitchrange(f_, ch, 2.0f);
        tsf_channel_set_tuning(f_, ch, 0.0f);
        tsf_channel_set_pitchwheel(f_, ch, 8192);
        tsf_channel_set_sustain(f_, ch, 0);
    }
}

void Sf2Synth::applyVolumeLocked(int ch) {
    // GM / DLS: 40 log10(v/127) dB for volume and for expression.
    const float v = volume_[ch] / 127.0f, e = expression_[ch] / 127.0f;
    tsf_channel_set_volume(f_, ch, v * v * e * e);
}

void Sf2Synth::programLocked(int ch, int program) {
    const int drums = ch == kDrumChannel ? 1 : 0;
    // tsf falls back from the selected bank to bank 0 (and, for drums, to kit 0 of bank 128).
    // A program missing even there plays program 0 rather than the channel's previous sound.
    if (!tsf_channel_set_presetnumber(f_, ch, program, drums)) tsf_channel_set_presetnumber(f_, ch, 0, drums);
}

void Sf2Synth::send(uint8_t status, uint8_t d1, uint8_t d2) {
    if (status < 0x80 || status >= 0xF0) return;
    const int ch = status & 0x0F;
    d1 &= 0x7F;
    d2 &= 0x7F;
    std::lock_guard lock(mu_);
    switch (status & 0xF0) {
        case 0x80: tsf_channel_note_off(f_, ch, d1); break;
        case 0x90:
            if (d2 == 0)
                tsf_channel_note_off(f_, ch, d1);
            else
                tsf_channel_note_on(f_, ch, d1, d2 / 127.0f);
            break;
        case 0xC0: programLocked(ch, d1); break;
        case 0xE0: tsf_channel_set_pitchwheel(f_, ch, d1 | (d2 << 7)); break;
        case 0xB0:
            switch (d1) {
                case 7:
                    volume_[ch] = d2;
                    applyVolumeLocked(ch);
                    break;
                case 11:
                    expression_[ch] = d2;
                    applyVolumeLocked(ch);
                    break;
                case 39: case 43: break;  // 14-bit volume / expression LSB: not used by GM tunes
                case 121:  // reset all controllers (RP-015)
                    expression_[ch] = 127;
                    applyVolumeLocked(ch);
                    tsf_channel_set_sustain(f_, ch, 0);
                    tsf_channel_set_pitchwheel(f_, ch, 8192);
                    tsf_channel_midi_control(f_, ch, 101, 127);
                    tsf_channel_midi_control(f_, ch, 100, 127);
                    break;
                case 124: case 125: case 126: case 127:  // omni / mono / poly: imply all notes off
                    tsf_channel_note_off_all(f_, ch);
                    break;
                default:
                    // 0/32 bank select, 6/38 data entry, 10/42 pan, 64 sustain, 98-101 (N)RPN,
                    // 120 all sound off, 123 all notes off; anything else is ignored by tsf.
                    tsf_channel_midi_control(f_, ch, d1, d2);
                    break;
            }
            break;
        default: break;  // 0xA0 key pressure, 0xD0 channel pressure: not supported
    }
}

void Sf2Synth::reset() {
    std::lock_guard lock(mu_);
    tsf_reset(f_);  // frees the channel table (and starts a quick release of every voice)
    for (int i = 0; i < f_->voiceNum; ++i) tsf_voice_kill(&f_->voices[i]);  // silent at once
    initChannelsLocked();
}

void Sf2Synth::render(float* out, int frames) {
    if (frames <= 0) return;
    std::lock_guard lock(mu_);
    tsf_render_float(f_, out, frames, 0);
}

int Sf2Synth::activeVoices() const {
    std::lock_guard lock(mu_);
    return tsf_active_voice_count(f_);
}

void Sf2Synth::setGainDb(float db) {
    std::lock_guard lock(mu_);
    tsf_set_output(f_, TSF_STEREO_INTERLEAVED, rate_, db);
}

Sf2Synth::ChannelInfo Sf2Synth::channel(int ch) const {
    ChannelInfo c;
    if (ch < 0 || ch >= kChannels) return c;
    std::lock_guard lock(mu_);
    // The preset actually selected (for channel 10 that is a bank 128 kit).
    const int idx = tsf_channel_get_preset_index(f_, ch);
    if (idx >= 0 && idx < f_->presetNum) {
        c.bank = f_->presets[idx].bank;
        c.program = f_->presets[idx].preset;
    }
    c.volume = volume_[ch];
    c.expression = expression_[ch];
    c.pitchWheel = tsf_channel_get_pitchwheel(f_, ch);
    return c;
}

}  // namespace tmport::audio
