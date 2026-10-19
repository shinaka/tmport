// Entry point of the 1990 DOS edition (dos_entry.h): the DOS folder, the look's palette, the sound
// device and the DOS runtime on the host, around the wave-2 preview.
// Wave 2 preview: replaced by the title/sign-in/game modules in wave 3.
#include "dos/app/dos_entry.h"

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <system_error>

#include "dos/app/dos_preview.h"
#include "dos/app/dos_runtime.h"
#include "dos/app/script_input.h"
#include "dos/save/users_store.h"
#include "mods/wav_codec.h"
#include "platform/host.h"
#include "rtl/borland_rand.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::dos {

namespace {

// The roster the screens show: the engine's DOS save, imported from the folder's USERS.DAT on the
// first run (dos/save/users_store.h). Empty (the composer then reads the folder's USERS.DAT, if
// any) without a preference folder or when the engine's file cannot be used.
std::vector<uint8_t> loadRoster(const DosOptions& o, const std::filesystem::path& folder) {
    if (o.prefDir.empty()) return {};
    DosUsersStore store(o.prefDir / std::filesystem::path(kDosUsersFileName));
    const DosUsersStatus status = store.loadWithImport(folder);
    if (status == DosUsersStatus::Ok || store.fromBackup()) return store.users().bytes();
    if (status != DosUsersStatus::Missing)
        log::warn("{}: {}; showing the DOS folder's roster", pathToUtf8(store.path()), describe(status));
    return {};
}

// The headless run's sound as a 16-bit mono WAV (the device's float output, full scale = 1.0).
bool writeSoundWav(const std::filesystem::path& path, const std::vector<float>& samples, int rate) {
    std::vector<uint8_t> bytes(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto v = static_cast<int16_t>(std::clamp(std::nearbyint(static_cast<double>(samples[i]) * 32767.0), -32768.0, 32767.0));
        bytes[2 * i] = static_cast<uint8_t>(v & 0xFF);
        bytes[2 * i + 1] = static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF);
    }
    return writeWholeFile(path, mods::makeWav(static_cast<uint32_t>(rate), 1, 16, bytes));
}

}  // namespace

int runDosEdition(platform::Host& host, const DosOptions& options, const std::filesystem::path& folder) {
    log::info("DOS edition in {} (look {}, sound {}{}, frame speed {}): wave 2 preview of the screens and sound",
              pathToUtf8(folder), lookName(options.look), iniValue(options.sound), options.slowCpu ? ", slow CPU" : "",
              iniValue(options.frameSpeed));
    const DosGfxFiles files = DosGfxFiles::open(folder);

    RuntimeOptions ro;
    ro.frameSpeed = options.frameSpeed;
    // The host keyboard is a PC keyboard, also with the Tandy look: KeyboardOptions::tandy stays off.
    DosRuntime runtime(ro);
    runtime.keyboard().isr().install();  // Kbd_Install 43a0:0002

    // One Turbo C rand() for the whole game, unseeded (PORTING_NOTES §2.4); the AdLib driver draws
    // from it in Music_Play.
    BorlandRand rand;
    const bool offline = options.headless || !host.hasWindow();
    std::unique_ptr<DosAudioDevice> audio = makeDosAudioDevice(files.files(), options.sound, [&rand] { return rand.rand(); });
    if (audio && !offline && !audio->open()) audio.reset();  // no device: no sound (open() logged why)

    DosPreview preview(files, options, runtime, loadRoster(options, folder), audio.get(), offline);
    runtime.setClient(&preview);

    const std::filesystem::path& dumpDir = options.frameDumpDir;
    if (!dumpDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dumpDir, ec);
        preview.keepOfflineSamples(offline && audio);
    }
    size_t dumped = 0;
    preview.setFrameSink([&](DosScreen s, std::span<const uint32_t> rgba) {
        host.presenter().presentFrame(rgba);
        if (dumpDir.empty()) return;
        const auto path = dumpDir / std::format("{:02}_{}_{}.png", dumped++, dosScreenName(s), lookName(options.look));
        if (!host.writeFrameDump(path)) log::error("cannot write {}", pathToUtf8(path));
    });
    preview.start();

    if (offline) {
        // Headless: the --keys script (game ticks) drives the run, which ends when it is used up and
        // the keys it left in the BIOS buffer are read (one per game tick).
        std::string error;
        auto script = ScriptedInput::parse(options.keys, error);
        if (!script) throw Error("--keys: " + error);
        runtime.setScript(std::move(*script));
        while (runtime.running() && !runtime.script().finished()) runtime.stepGameTicks(1);
        for (int i = 0; i < BiosKeyboard::kCapacity && runtime.running() && runtime.keyboard().bios().size() > 0; ++i)
            runtime.stepGameTicks(1);
        log::info("DOS preview: {} game ticks, {} screens shown", runtime.gameTicks(), preview.screensShown());
        if (!dumpDir.empty() && audio) {
            const auto wav = dumpDir / "sound.wav";
            if (!writeSoundWav(wav, preview.offlineSamples(), audio->sampleRate())) log::error("cannot write {}", pathToUtf8(wav));
        }
        return 0;
    }

    // Interactive: the host's events feed the PC keyboard and the mouse driver; host time drives
    // the virtual PIT (DosRuntime::advance, at most 4 game ticks per frame after a stall).
    bool resync = false;
    auto mouseAt = [&](float wx, float wy) {
        float cx = 0.0f, cy = 0.0f;
        if (host.windowToCanvas(wx, wy, cx, cy))
            runtime.mouse().setLogicalPosition(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)));
    };
    platform::HostHandlers h;
    h.input = [&](const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                runtime.hostKey(static_cast<uint32_t>(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
                break;
            case SDL_EVENT_MOUSE_MOTION: mouseAt(e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                mouseAt(e.button.x, e.button.y);
                const uint16_t b = e.button.button == SDL_BUTTON_LEFT    ? MouseDriver::kLeft
                                   : e.button.button == SDL_BUTTON_RIGHT ? MouseDriver::kRight
                                   : e.button.button == SDL_BUTTON_MIDDLE ? MouseDriver::kMiddle
                                                                          : uint16_t{0};
                if (b) runtime.mouse().setButton(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }
            default: break;
        }
    };
    h.window = [&](const SDL_Event& e) {
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) runtime.releaseAllKeys();
    };
    h.activation = [&](bool active) {
        // Frozen (focus loss, minimised): keys up, sound muted; the time spent frozen is skipped.
        if (!active) runtime.releaseAllKeys();
        if (audio) audio->setOutputEnabled(active);
        resync = true;
    };
    host.setHandlers(std::move(h));

    // A --keys script also drives a windowed run (on top of the player's keys), so the interactive
    // loop can be smoke-tested, e.g. on SDL's offscreen video driver.
    if (!options.keys.empty()) {
        std::string error;
        auto script = ScriptedInput::parse(options.keys, error);
        if (!script) throw Error("--keys: " + error);
        runtime.setScript(std::move(*script));
    }

    uint64_t last = host.nowNs();
    while (runtime.running() && !host.quitRequested()) {
        host.pump();
        if (host.quitRequested()) break;
        const uint64_t now = host.nowNs();
        if (resync) {
            last = now;
            resync = false;
        }
        runtime.advance(now - last);
        last = now;
        host.renderIfDue();
        host.clock().sleepMs(1);
    }
    host.setHandlers({});
    if (audio) audio->close();
    return 0;
}

}  // namespace tmport::dos
