// Wave 2 preview, kept behind the hidden option --dos-preview (dos_entry.cpp).
#include "dos/app/dos_preview.h"

#include <optional>

#include "dos/data/dgroup_offsets.h"
#include "util/log.h"

namespace tmport::dos {

namespace {

// The BIOS key word (scan << 8 | ASCII) of Esc, and the command that the play context's key list
// (DS:80F3[0]) gives Ctrl+S (0x1F13), handled by Game_HandleHotkeys 43cb:03cb.
constexpr uint16_t kKeyEsc = 0x011B;
constexpr int16_t kCmdSoundToggle = 14;

std::optional<SoundCard> soundCardOf(SoundDevice d) {
    switch (d) {
        case SoundDevice::Speaker: return SoundCard::Speaker;
        case SoundDevice::Tandy: return SoundCard::Tandy;
        case SoundDevice::AdLib: return SoundCard::AdLib;
        case SoundDevice::Off: break;
    }
    return std::nullopt;
}

}  // namespace

std::unique_ptr<DosAudioDevice> makeDosAudioDevice(const DosFiles& files, SoundDevice device, std::function<int()> rand) {
    const auto card = soundCardOf(device);
    if (!card) return nullptr;
    auto tables = std::make_shared<const SoundTables>(SoundTables::fromExe(files.exe()));
    auto bank = std::make_shared<const SoundBank>(
        SoundBank::fromBytes(*card, files.bytes(sfxFileName(*card)), files.bytes(musicFileName(*card)), std::move(tables)));
    return std::make_unique<DosAudioDevice>(std::move(bank), DosAudioConfig{}, std::move(rand));
}

DosPreview::DosPreview(const DosGfxFiles& files, const DosOptions& options, DosRuntime& runtime, std::vector<uint8_t> roster,
                       DosAudioDevice* audio, bool offlineAudio)
    : files_(files),
      options_(options),
      runtime_(runtime),
      roster_(std::move(roster)),
      audio_(audio),
      offlineAudio_(offlineAudio),
      palette_(lookPalette(files.exe(), options.look)),
      inputTables_(InputTables::load(files.exe())),
      input_(inputTables_) {
    // Game_InitVars values (the EXE's initialised data): music and effects on.
    musicOn_ = files.exe().ds16(dg::kMusicOn) != 0;
    soundOn_ = files.exe().ds16(dg::kSoundOn) != 0;
    if (audio_) {
        audio_->setMusicOn(musicOn_);
        audio_->setSoundOn(soundOn_);
    }
    lastMouseX_ = runtime_.mouse().x();
    lastMouseY_ = runtime_.mouse().y();
}

void DosPreview::start() { show(0); }

// ------------------------------------------------------------------------------------ runtime

void DosPreview::soundTick() {
    if (!audio_ || !offlineAudio_) return;
    if (keepSamples_) {
        audio_->renderOfflineTicks(1, samples_);
    } else {
        scratch_.clear();
        audio_->renderOfflineTicks(1, scratch_);
    }
}

// The input half of Game_Tick: Input_Tick's Input_Poll (4a85:000E) on the play context, one BIOS
// key word per tick. The commands are applied by the frame body, as Game_HandleHotkeys does.
void DosPreview::gameTick(const TickInfo&) {
    input_.context = 0;
    input_.poll(runtime_.keyboard().bios(), MousePointer{}, 0);
    if (input_.lastKey != 0) keys_.emplace_back(input_.lastKey, input_.cmd);
    input_.cmd = -1;  // consumed (g_cmd = -1; DS:9238 = 1)
    input_.deliverNext = true;

    const MouseDriver& m = runtime_.mouse();
    if ((m.buttons() & MouseDriver::kLeft) && !(prevButtons_ & MouseDriver::kLeft)) clicked_ = true;
    prevButtons_ = m.buttons();
    if (m.x() != lastMouseX_ || m.y() != lastMouseY_) {
        lastMouseX_ = m.x();
        lastMouseY_ = m.y();
        pointerFollows_ = true;
        if (screen() == DosScreen::Title) dirty_ = true;
    }
}

// The main loop body (130f:0037): Sound_SyncState (not in the title loop), Game_HandleHotkeys and
// the preview's keys, then the picture when it changed.
void DosPreview::frameBody() {
    if (screen() != DosScreen::Title) syncSound();
    std::vector<std::pair<uint16_t, int16_t>> keys;
    keys.swap(keys_);
    for (const auto& [word, command] : keys) {
        handleKey(word, command);
        if (quit_) return;
    }
    if (clicked_) {
        clicked_ = false;
        show((index_ + 1) % std::size(kPreviewScreens));
    }
    if (dirty_) compose();
}

void DosPreview::handleKey(uint16_t keyWord, int16_t command) {
    if (command == kCmdSoundToggle) {
        toggleSound();
        return;
    }
    if (keyWord == kKeyEsc) {
        quit_ = true;
        return;
    }
    const char c = static_cast<char>(keyWord & 0xFF);
    if (c == '\r' || c == ' ') {
        show((index_ + 1) % std::size(kPreviewScreens));
    } else if (c >= '0' && c <= '9') {
        if (audio_) audio_->sfxPlay(c - '0');  // 491b:06d7 Sfx_Play
    } else if (c == '-' || c == '=') {
        if (audio_) audio_->sfxPlay(c == '-' ? 10 : 11);
    }
}

// ------------------------------------------------------------------------------------ screens

void DosPreview::show(size_t index) {
    const DosScreen from = screen();
    index_ = index;
    const DosScreen to = screen();
    if (shown_ > 0 && from == DosScreen::Title && to != DosScreen::Title) {
        // Title_Run's end (4ba4:0434): a slow Tandy or CGA machine turns the music off; the music
        // loop flag DS:63BA goes back to 0.
        if (options_.slowCpu && (options_.look == Look::Tandy || options_.look == Look::Cga)) {
            musicOn_ = false;
            if (audio_) audio_->setMusicOn(false);
            musicStop();
            sfxStop();
        }
        if (audio_) audio_->setMusicLoop(false);
    }
    enterScreen(to);
    dirty_ = true;
    compose();
}

void DosPreview::enterScreen(DosScreen s) {
    switch (s) {
        case DosScreen::Title:  // Title_Run 4ba4:0434
            uiMode_ = 0x80;
            area_ = 6;
            if (audio_) audio_->setMusicLoop(true);
            musicPlay(1);
            break;
        case DosScreen::SignIn:  // main 130f:0032: g_uiMode = 8; the sign-in is area 4
            uiMode_ = 8;
            area_ = 4;
            break;
        case DosScreen::Clubhouse:  // Game_Start 47c7:08b5 for a new game
            area_ = 5;
            musicStop();
            sfxStop();
            uiMode_ = static_cast<uint8_t>(uiMode_ & ~0x0A);
            musicPlay(tuneForArea(area_));
            soundChanged_ = true;
            break;
        case DosScreen::Quiz:  // out of the clubhouse (Cell_P12_StartRoom), then a catch (Elf_NetCatch)
            area_ = 0;
            musicStop();
            musicPlay(0);
            uiMode_ = 2;
            break;
        case DosScreen::Path:  // the answer closes the question (Quiz_Update)
            uiMode_ = 0;
            break;
        case DosScreen::Castle:  // through the level-2 gate (Cell_P42_GateL2 176a:03aa)
            area_ = 0x0D;
            musicPlay(2);
            uiMode_ = 0;
            break;
        default:
            break;
    }
}

void DosPreview::compose() {
    dirty_ = false;
    DosScreenSpec spec = defaultScreenSpec(screen(), options_.look);
    spec.usersImage = roster_;
    if (screen() == DosScreen::Title && pointerFollows_) spec.pointer = {{lastMouseX_, lastMouseY_}};
    const BackBuffer buf = composeScreen(files_, spec);
    const std::vector<uint32_t> rgba = toRgba32(buf, palette_);
    ++shown_;
    if (sink_) sink_(screen(), rgba);
}

// ------------------------------------------------------------------------------------- sound

int DosPreview::tuneForArea(uint16_t area) {
    if (area < 4) return 0;
    if (area < 7) return 1;
    if (area < 15) return 2;
    return -1;
}

void DosPreview::musicPlay(int tune) {
    if (audio_ && tune >= 0) audio_->musicPlay(tune);
}
void DosPreview::musicStop() {
    if (audio_) audio_->musicStop();
}
void DosPreview::sfxStop() {
    if (audio_) audio_->sfxStop();
}
void DosPreview::silenceAll() {
    if (audio_) audio_->silenceAll();
}

void DosPreview::toggleSound() {
    if (uiMode_ != 0 && area_ != 6) return;  // only in play or on the title
    const bool on = !musicOn_;
    musicOn_ = soundOn_ = on;
    if (audio_) {
        audio_->setMusicOn(on);
        audio_->setSoundOn(on);
    }
    if (!on) {
        musicStop();
        sfxStop();
        silenceAll();
    } else {
        musicPlay(tuneForArea(area_));
    }
    if (options_.verbose) log::info("dos preview: music and sound {}", on ? "on" : "off");
}

void DosPreview::syncSound() {
    if (lastUiMode_ != uiMode_) {
        lastUiMode_ = uiMode_;
        if (audio_) audio_->setMusicPaused(uiMode_ != 0);  // DS:37CE = 0xFFFF / 0
        if (uiMode_ != 0) {
            sfxStop();
            silenceAll();
        }
    }
    if (soundChanged_) {
        if (!musicOn_) {
            musicStop();
            sfxStop();
            silenceAll();
        } else {
            musicPlay(tuneForArea(area_));
        }
        soundChanged_ = false;
    }
}

}  // namespace tmport::dos
