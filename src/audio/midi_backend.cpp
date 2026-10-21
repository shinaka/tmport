#include "audio/midi_backend.h"

#include <SDL3/SDL_timer.h>

#include <mutex>

#include "audio/midi_backend_soundfont.h"
#include "mischief/util/log.h"

namespace tmport::audio {

uint64_t realClockUs() { return SDL_GetTicksNS() / 1000u; }

void MidiBackend::allNotesOff() {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        send(static_cast<uint8_t>(0xB0 | ch), 64, 0);   // sustain off
        send(static_cast<uint8_t>(0xB0 | ch), 123, 0);  // all notes off
    }
}

MidiBackendKind resolveMidiBackendKind(MidiBackendKind kind, bool soundFontConfigured) {
    if (kind != MidiBackendKind::Auto) return kind;
#if defined(_WIN32)
    (void)soundFontConfigured;
    return MidiBackendKind::WinMM;
#else
    return soundFontConfigured ? MidiBackendKind::SoundFont : MidiBackendKind::Null;
#endif
}

std::unique_ptr<MidiBackend> createMidiBackend(const MidiBackendConfig& config) {
    const bool haveSf2 = !config.soundFont.empty();
    switch (resolveMidiBackendKind(config.kind, haveSf2)) {
        case MidiBackendKind::WinMM: return createWinMmMidiBackend();
        case MidiBackendKind::SoundFont:
            if (!haveSf2) {
                mischief::log::warn("midi: the SoundFont MIDI output needs a SoundFont: set soundfont = <file.sf2> in [audio] of "
                          "tmport.ini or pass --soundfont <file.sf2>; music is disabled");
                return std::make_unique<SoundFontMidiBackend>(SoundFontMidiBackend::Config{});  // cannot open
            }
            return createSoundFontMidiBackend(config.soundFont, config.sdlDevice, config.musicVolume);
        case MidiBackendKind::Null:
            if (config.kind == MidiBackendKind::Auto) {
                static std::once_flag once;
                std::call_once(once, [] {
                    mischief::log::info("midi: no MIDI synthesizer on this platform and no SoundFont configured, so the music is "
                              "silent. Set soundfont = <General MIDI .sf2 file> in [audio] of tmport.ini (or pass "
                              "--soundfont <file.sf2>) to hear it; see docs/BUILDING.md.");
                });
            }
            return std::make_unique<NullMidiBackend>();
        case MidiBackendKind::Auto: break;
    }
    return std::make_unique<NullMidiBackend>();
}

std::unique_ptr<MidiBackend> createMidiBackend(MidiBackendKind kind) {
    MidiBackendConfig c;
    c.kind = kind;
    return createMidiBackend(c);
}

}  // namespace tmport::audio
