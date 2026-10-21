// WinMM midiOut back end (Windows only): MIDI_MAPPER, short messages, midiOutReset on stop.
// Raises the system timer resolution to 1 ms while open so the sequencer thread's sleeps are
// precise (the original's MCI sequencer used its own millisecond timer).

#include "audio/midi_backend.h"

#if defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>

#include "mischief/util/log.h"

namespace tmport::audio {

namespace {

class WinMmMidiBackend final : public MidiBackend {
public:
    ~WinMmMidiBackend() override { close(); }
    std::string name() const override { return "winmm"; }

    bool open() override {
        if (handle_) return true;
        HMIDIOUT h = nullptr;
        const MMRESULT r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
        if (r != MMSYSERR_NOERROR) {
            mischief::log::warn("midi: midiOutOpen(MIDI_MAPPER) failed ({}); music is disabled", static_cast<unsigned>(r));
            return false;
        }
        handle_ = h;
        timerRaised_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        return true;
    }

    void close() override {
        if (!handle_) return;
        midiOutReset(handle_);
        midiOutClose(handle_);
        handle_ = nullptr;
        if (timerRaised_) timeEndPeriod(1);
        timerRaised_ = false;
    }

    bool isOpen() const override { return handle_ != nullptr; }

    void send(uint8_t status, uint8_t d1, uint8_t d2) override {
        ++messages_;
        if (!handle_) return;
        const DWORD msg = static_cast<DWORD>(status) | (static_cast<DWORD>(d1 & 0x7F) << 8) | (static_cast<DWORD>(d2 & 0x7F) << 16);
        midiOutShortMsg(handle_, msg);
    }

    void allNotesOff() override {
        MidiBackend::allNotesOff();
        if (handle_) midiOutReset(handle_);
    }

private:
    HMIDIOUT handle_ = nullptr;
    bool timerRaised_ = false;
};

}  // namespace

std::unique_ptr<MidiBackend> createWinMmMidiBackend() { return std::make_unique<WinMmMidiBackend>(); }

}  // namespace tmport::audio

#else

namespace tmport::audio {
std::unique_ptr<MidiBackend> createWinMmMidiBackend() { return nullptr; }
}  // namespace tmport::audio

#endif
