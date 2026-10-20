// Wave 2 preview: replaced by the game modules in wave 3.
#include "gg/app/preview.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gg/app/display.h"
#include "gg/audio/gg_audio.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/building.h"
#include "gg/game/hub.h"
#include "gg/game/hud.h"
#include "gg/game/pic_cache.h"
#include "gg/game/puzzles.h"
#include "gg/game/screen_anim.h"
#include "gg/gfx/video.h"
#include "gg/save/players_dat.h"
#include "gg/save/players_store.h"
#include "gg/text/font.h"
#include "gg/ui/attract.h"
#include "gg/ui/attract_anims.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/signin.h"
#include "gg/ui/ui_canvas.h"
#include "mods/wav_file.h"
#include "platform/host.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// Input codes (code_runtime.md §9: BIOS keys as ASCII, extended keys as 0x100 + scan code).
constexpr uint16_t kKeyEnter = 0x000D;
constexpr uint16_t kKeyEsc = 0x001B;
constexpr uint16_t kKeySpace = 0x0020;
constexpr uint16_t kKeyLeft = 0x014B;
constexpr uint16_t kKeyRight = 0x014D;
constexpr uint16_t kKeyDel = 0x0153;

constexpr uint16_t kTitleMusic = 0x1020;       // DS:0098 / g_musicBank: the title music
constexpr uint16_t kWarehouseEffects = 0x1039; // State_07_Warehouse 12e0:0634
constexpr int kAttractSteps = 11;              // the sub-sequence DS:20a8 (step 12 is NULL)
constexpr int kTicksPerSecond = 146;           // Attract_PlayAnim's Timer_Set(1, n * 146)

// Where the preview goes next.
enum class Page : uint8_t { Attract, SignIn, Hub, Warehouse, Puzzles, Quit };
struct Next {
    Page page = Page::Quit;
    int arg = 0;  // the building of the warehouse
};

// The esc key (or a closed window) ends the preview from inside any loop.
struct PreviewQuit {};

// ---------------------------------------------------------------------------------------- sound

// The sound calls the preview makes (the game's SndRes_* calls), logged with their sound tick in
// the schedule syntax of gg/audio/soundtest.h ("TICK:CMD:ARGS"), so a run can be replayed by
// `ggport-cli --gg-sound fm soundtest "SCRIPT" OUT.wav`.
class Sound {
public:
    explicit Sound(GgAudio& audio) : a_(audio) {}

    GgAudio& audio() { return a_; }
    int effectBank() const { return effects_; }

    int loadMusic(uint16_t id) {
        note(std::format("m:{:x}", id));
        return a_.loadMusicBank(id);
    }
    void freeMusic(int h) {
        note("M");
        a_.freeMusicBank(h);
    }
    void playMusic(int h, int loop = 1) {
        note(std::format("p:{}", loop));
        a_.playMusic(h, 0, loop);
    }
    void stopMusic(int h) {
        note("x");
        a_.stopMusic(h);
    }
    void arrangement(int h, int n) {
        note(std::format("a:{}", n));
        a_.selectArrangement(h, n);
    }
    // g_effectBank DS:7a44 = SndRes_LoadEffectBank(id)
    int loadEffects(uint16_t id) {
        note(std::format("e:{:x}", id));
        effects_ = a_.loadEffectBank(id);
        a_.setEffectBank(effects_);
        return effects_;
    }
    void freeEffects() {
        if (effects_ < 0) return;
        note("E");
        a_.freeEffectBank(effects_);
        effects_ = -1;
        a_.setEffectBank(-1);
    }
    // SndRes_PlayEffect(g_effectBank + k, loop, p)
    void effect(int k, int loop, int p) {
        if (effects_ < 0) return;
        note(std::format("f:{}:{}:{}", k, loop, p));
        a_.playEffect(effects_ + k, loop, p);
    }
    void stopEffect(int k) {
        if (effects_ < 0) return;
        note(std::format("s:{}", k));
        a_.stopEffect(effects_ + k);
    }
    bool effectPlaying(int k) { return effects_ >= 0 && a_.isPlaying(effects_ + k) == 1; }
    void stopAllEffects() {
        note("S");
        a_.stopAllEffects();
    }
    // Effect k of the global bank 0x105B (DS:9dc0).
    void globalEffect(int k, int loop, int p) {
        const int g = a_.call([](SndRes& r) { return r.globalEffects(); });
        if (g < 0) return;
        note(std::format("g:{}:{}:{}", k, loop, p));
        a_.playEffect(g + k, loop, p);
    }
    // Room_Enter's music (1622:03b6 RoomMusic_SelectLayers, 1622:0346 RoomMusic_Start).
    void roomMusic(int building, int n, bool chimps, int level) {
        note(std::format("R:{}:{}:{}:{}", building, n, chimps ? 1 : 0, level));
        a_.call([&](SndRes&) {
            a_.cues().roomMusicSelectLayers(n, chimps, level);
            a_.cues().roomMusicStart();
        });
    }
    // The puzzles' PzSnd bank (3924).
    void puzzleBank(uint16_t id) {
        note(std::format("P:{:x}", id));
        a_.call([&](SndRes&) { a_.puzzles().loadBank(id); });
    }
    void puzzleFreeBank() {
        a_.call([&](SndRes&) { a_.puzzles().freeBank(); });
    }
    void puzzleEffect(int k, int priority) {
        note(std::format("y:{}:{}", k, priority));
        a_.call([&](SndRes&) { a_.puzzles().play(k, priority); });
    }
    // Ctrl+S: Snd_ToggleOption 1cdd:05c8.
    void toggle() {
        note("q");
        a_.toggleSound();
    }
    // The effects a number key may play: the members of the current effect bank, of the global
    // bank (hub) or of the PzSnd bank (puzzles).
    int effectCount() {
        if (effects_ < 0) return 0;
        return a_.call([&](SndRes& r) { return static_cast<int>(r.entry(effects_ - 1)); });
    }
    int globalCount() {
        return a_.call([](SndRes& r) { return r.globalEffects() > 0 ? static_cast<int>(r.entry(r.globalEffects() - 1)) : 0; });
    }
    int puzzleCount() {
        return a_.call([&](SndRes&) { return a_.puzzles().count(); });
    }

private:
    void note(const std::string& cmd) {
        if (!a_.isOpen()) log::debug("sound call: {}:{}", a_.tickIndex(), cmd);
        else log::debug("sound call: {}", cmd);
    }

    GgAudio& a_;
    int effects_ = -1;
};

// ---------------------------------------------------------------------------------------- roster

// The roster the sign-in shows: the engine's players.dat, else the game folder's PLAYERS.DAT, else
// a new one (Roster_LoadOrCreate's creation). Read only: the preview saves nothing.
Roster previewRoster(const GgContext& ctx) {
    std::vector<std::filesystem::path> candidates;
    if (!ctx.launch.prefDir.empty()) candidates.push_back(playersPathIn(ctx.launch.prefDir));
    if (const auto p = ctx.files.playersDatPath()) candidates.push_back(*p);
    for (const auto& path : candidates) {
        const auto bytes = readWholeFile(path);
        if (!bytes || bytes->size() < kRosterSize) continue;
        const Roster r = Roster::fromBytes(std::span<const uint8_t, kRosterSize>(bytes->data(), kRosterSize));
        if (r.magicOk() && r.version == kPlayersVersion) return r;
    }
    return PlayersDat::create().roster;
}

// --------------------------------------------------------------------------------------- preview

class Preview {
public:
    explicit Preview(GgContext& ctx);
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    void run();
    // The end of the run: the sound shut down (Game_Shutdown's Snd_Off), --wav-dump, --opl-log.
    void finish();

private:
    // Screens.
    Next attract();
    Next signIn();
    Next hub();
    Next warehouse(int building);
    Next puzzles();

    // State_01's sub-sequence (2c11) and Attract_PlayAnim.
    int16_t titleAnim();
    int16_t afterTitle();
    int16_t storyPage();
    int16_t afterStory();
    int16_t beforePrompt();
    int16_t signInPrompt();
    int playAnim(const AttractAnim& anim, int fade, int flags, int arg);
    int16_t animResult(int r);

    // An input poll: Input_GetCode, Esc quits, number keys play effects.
    uint16_t poll();
    void numberKey(uint16_t code);
    // A frame of 4 ticks: Timer_Set(0, 4) before the drawing, Timer_Wait(0) after.
    void frameStart() { rt_.timers().set(0, 4); }
    void frameEnd() { rt_.timerWait(0); }
    UiCanvas ui() { return UiCanvas{video_.screen(), fonts_, [this] { display_.flush(); }}; }
    void drawMenuBar();

    GgContext& ctx_;
    const GgExe& exe_;
    const Archive& arc_;
    GgRuntime& rt_;
    Video video_;
    Display display_;
    Fonts fonts_;
    PicCache hudCache_;  // Game_Init's loads, kept for the whole run (the HUD bundle)
    Hud hud_;
    std::unique_ptr<GgAudio> audio_;
    std::unique_ptr<Sound> sound_;
    bool pitSound_ = false;    // the sound ticks come from the virtual PIT
    std::vector<float> pcm_;   // --wav-dump
    bool keepPcm_ = false;
    AttractAnims anims_;
    HudCounters counters_;     // the last warehouse's (the puzzles' HUD shows its score)
    int titleMusic_ = -1;      // DS:0098
    int attractMusic_ = -1;    // g_musicBank DS:9dc4 in the attract
    int16_t leaveRequest_ = 2; // g_leaveRequest DS:455a
    uint16_t storyText_ = kStoryFirstText;  // g_storyTextId DS:a04e
    // The puzzle screens use the PzSnd bank for the number keys, the hub the global bank.
    enum class Keys : uint8_t { Effects, Global, Puzzle } keys_ = Keys::Effects;
    bool finished_ = false;
};

Preview::Preview(GgContext& ctx)
    : ctx_(ctx),
      exe_(ctx.files.exe()),
      arc_(ctx.files.archive()),
      rt_(ctx.runtime),
      display_(ctx.runtime, video_),
      fonts_(Fonts::load(ctx.files.archive())),
      hudCache_(ctx.files.archive(), &video_.palette(), [this](PaletteLoad l) { display_.runLoad(std::move(l)); }),
      anims_(AttractAnims::fromExe(ctx.files.exe())) {
    const GgOptions& o = ctx.launch.options;
    // Init_CommandLine's sound start-up (303a:005c step 7) and SndRes_Init (Game_Init).
    GgAudioConfig cfg;
    cfg.mode = ctx.launch.sound;
    cfg.logOpl = !o.oplLog.empty();
    audio_ = std::make_unique<GgAudio>(exe_, arc_, cfg);
    sound_ = std::make_unique<Sound>(*audio_);
    if (!ctx.launch.headless) {
        if (!audio_->open()) log::warn("no audio device: the sound runs silently");
    } else if (!o.wavDump.empty()) {
        keepPcm_ = true;
    }
    if (!audio_->isOpen()) {
        // Timer_ISR: Snd_Tick on every even g_ticks (code_runtime.md §1).
        pitSound_ = true;
        rt_.clock().setSoundTick([this](uint32_t) { audio_->step(keepPcm_ ? &pcm_ : nullptr); });
    }
    // Input_GetCode's global keys: Ctrl+S.
    rt_.input().hooks().toggleSound = [this] { sound_->toggle(); };
    // Game_Init: Gfx_Init with Mouse_Init and Pal_Blackout; Hud_Init (the HUD bundle's palette
    // range goes into the shadow while faded).
    display_.init(exe_, arc_);
    hud_.init(exe_, arc_, hudCache_);
}

Preview::~Preview() {
    if (pitSound_) rt_.clock().setSoundTick({});
    rt_.input().hooks().toggleSound = {};
}

void Preview::finish() {
    if (finished_) return;
    finished_ = true;
    audio_->shutdown();  // Game_Shutdown 12e0:0b19: Snd_Off
    const GgOptions& o = ctx_.launch.options;
    if (!o.wavDump.empty()) {
        if (!keepPcm_) {
            log::warn("--wav-dump: only headless runs render their sound offline");
        } else {
            std::vector<uint8_t> bytes;
            bytes.reserve(pcm_.size() * 2);
            for (const float f : pcm_) {
                const auto v = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
                bytes.push_back(static_cast<uint8_t>(v));
                bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
            }
            const auto wav = mods::makeWav(static_cast<uint32_t>(audio_->sampleRate()), 1, 16, bytes);
            if (writeWholeFile(pathFromUtf8(o.wavDump), wav))
                log::info("--wav-dump: {} samples ({} sound ticks) -> {}", pcm_.size(), audio_->tickIndex(), o.wavDump);
            else
                log::error("cannot write {}", o.wavDump);
        }
    }
    if (!o.oplLog.empty()) {
        if (audio_->isOpen()) audio_->close();  // the log is read while no stream runs
        if (audio_->oplLog().save(o.oplLog))
            log::info("--opl-log: {} OPL writes -> {}", audio_->oplLog().entries().size(), o.oplLog);
        else
            log::error("cannot write {}", o.oplLog);
    }
}

void Preview::run() {
    Next next{Page::Attract, 0};
    try {
        while (next.page != Page::Quit) {
            switch (next.page) {
                case Page::Attract: next = attract(); break;
                case Page::SignIn: next = signIn(); break;
                case Page::Hub: next = hub(); break;
                case Page::Warehouse: next = warehouse(next.arg); break;
                case Page::Puzzles: next = puzzles(); break;
                case Page::Quit: break;
            }
        }
    } catch (const PreviewQuit&) {
        log::info("preview: Esc");
    }
}

// ------------------------------------------------------------------------------------------ input

uint16_t Preview::poll() {
    const uint16_t c = rt_.getInputCode();
    if (c == kKeyEsc) throw PreviewQuit{};
    numberKey(c);
    return c;
}

void Preview::numberKey(uint16_t code) {
    if (code < '0' || code > '9') return;
    const int k = code == '0' ? 9 : code - '1';
    switch (keys_) {
        case Keys::Effects:
            if (k < sound_->effectCount()) sound_->effect(k, 0, 4);
            break;
        case Keys::Global:
            if (k < sound_->globalCount()) sound_->globalEffect(k, 0, 5);
            break;
        case Keys::Puzzle:
            if (k < sound_->puzzleCount()) sound_->puzzleEffect(k, 3);
            break;
    }
}

void Preview::drawMenuBar() {
    // 40a4:0000 Menu_SetMenuBar(DS:4053): states 5 and 9 draw the bar (rows 0..8).
    menuDrawBar(ui(), MenuBarDef::fromExe(exe_), MenuBarPictures::load(arc_), -1);
}

// ---------------------------------------------------------------------------------------- attract

// 2c11:0913 State_01_Attract
Next Preview::attract() {
    keys_ = Keys::Effects;
    video_.cursor().hide();  // Mouse_Hide
    sound_->loadEffects(kAttractEffects);
    attractMusic_ = sound_->loadMusic(kAttractMusic);
    sound_->playMusic(attractMusic_);
    Zone click = Zone::fromRecord(exe_.dsBytes(dg::kZoneClickEnter, 22));  // a click = Enter
    rt_.zones().addLocal(click);
    leaveRequest_ = 2;
    const std::array<StateFn, kAttractSteps + 2> table = {
        StateFn{},
        [this] { return titleAnim(); },
        [this] { return afterTitle(); },
        [this] { return storyPage(); },
        [this] { return storyPage(); },
        [this] { return storyPage(); },
        [this] { return storyPage(); },
        [this] { return storyPage(); },
        [this] { return storyPage(); },
        [this] { return afterStory(); },
        [this] { return beforePrompt(); },
        [this] { return signInPrompt(); },
        StateFn{},
    };
    int16_t state = 1;
    try {
        seqRun(table, state);
    } catch (...) {
        rt_.zones().clearLocal();
        throw;
    }
    display_.fadeOut();
    rt_.zones().clearLocal();
    sound_->freeMusic(attractMusic_);
    attractMusic_ = -1;
    sound_->freeEffects();
    video_.cursor().show();  // Mouse_Show
    if (leaveRequest_ < 0) throw PreviewQuit{};
    return {Page::SignIn, 0};
}

int16_t Preview::animResult(int r) {
    // The steps' answer to Attract_PlayAnim's result: Enter -> next step, Esc -> stop and quit,
    // Space -> stop (to the sign-in), anything else (the time-out) -> next.
    if (r == kKeyEsc) {
        leaveRequest_ = -1;
        return -1;
    }
    return r == kKeySpace ? int16_t{-1} : int16_t{0};
}

// 2c11:0000 Attract_PlayAnim(anim, fade, flags, vararg)
int Preview::playAnim(const AttractAnim& anim, int fade, int flags, int arg) {
    PicCache cache(arc_, &video_.palette(), [this](PaletteLoad l) { display_.runLoad(std::move(l)); });
    ScreenAnimSound fx;
    fx.play = [this](int member, int loop, int priority) { sound_->effect(member, loop, priority); };
    fx.stop = [this](int member) { sound_->stopEffect(member); };
    fx.isPlaying = [this](int member) {
        rt_.idle();  // the busy-wait of op -45 lets the sound run
        return sound_->effectPlaying(member);
    };
    AttractAnimHost host(video_, cache, ctx_.rand, fx);
    AttractAnimPlayer player(exe_, anim, host);  // ScreenAnim_Load of every id, Sprites_Reset
    if (flags & kAnimTimed) rt_.timers().set(1, static_cast<uint16_t>(arg * kTicksPerSecond));
    const int watch = (flags & kAnimWatch) ? arg : -1;
    for (bool first = true;; first = false) {
        frameStart();
        player.startChannels();
        host.frame();  // ScreenAnim_Frame
        display_.flush();
        frameEnd();
        if (first) {
            if (fade == kAnimFadeIn) display_.fadeIn();
            else if (fade == kAnimFadeFromSaved) display_.fadeFromSaved();
        }
        player.advance();
        bool leave = false;
        int result = 0;
        if ((flags & kAnimTimed) && rt_.timerExpired(1)) leave = true;
        if (flags & kAnimInput) {
            result = poll();
            if (result == kKeyEnter || result == kKeySpace) leave = true;
        }
        if (watch >= 0 && player.channelAtEnd(watch)) leave = true;
        if (leave) return result;
    }
}

// 2c11:043f Attract_TitleAnim
int16_t Preview::titleAnim() {
    display_.runLoad(attractTitleBackground(video_, arc_));
    const Bundle art = Bundle::load(arc_, kTitleArt);  // Res_LoadCopy
    int r = playAnim(anims_.title, kAnimFadeIn, kAnimTimed | kAnimInput, kTitlePhaseSeconds[0]);
    if (r == kKeyEnter) return 0;
    if (r == kKeyEsc || r == kKeySpace) return animResult(r);
    sound_->effect(kTitleEffect[0], 0, 4);
    attractDrawTitleArt(video_.screen(), art, 2);
    display_.flush();
    r = playAnim(anims_.title, 0, kAnimTimed | kAnimInput, kTitlePhaseSeconds[1]);
    if (r == kKeyEnter) return 0;
    if (r == kKeyEsc || r == kKeySpace) return animResult(r);
    attractDrawTitleArt(video_.screen(), art, 3);
    sound_->effect(kTitleEffect[1], 0, 4);
    display_.flush();
    r = playAnim(anims_.title, 0, kAnimTimed | kAnimInput, kTitlePhaseSeconds[2]);
    return animResult(r);
}

// 2c11:05cf Attract_AfterTitle
int16_t Preview::afterTitle() {
    attractAfterTitle(video_, arc_);
    const int16_t r = animResult(playAnim(anims_.afterTitle, kAnimFadeFromSaved, kAnimWatch | kAnimInput, 0));
    display_.fadeOut();
    sound_->stopAllEffects();
    storyText_ = kStoryFirstText;
    return r;
}

// 2c11:0630 Attract_StoryPage
int16_t Preview::storyPage() {
    const bool first = storyText_ == kStoryFirstText;
    display_.runLoad(attractStoryPage(video_, fonts_, arc_, storyText_));
    if (first) display_.fadeOutIn();
    else display_.flush();
    rt_.timers().set(1, static_cast<uint16_t>(kAttractPageTicks));
    int16_t result = 0;
    for (bool done = false; !done;) {
        const uint16_t c = poll();
        if (c == kKeyEnter || c == kKeySpace || c == kKeyEsc) {
            result = animResult(c);
            done = true;
        }
        if (rt_.timerExpired(1)) {
            result = 0;  // the time-out also overrides a key of the same poll
            done = true;
        }
    }
    ++storyText_;
    return result;
}

// 2c11:077f Attract_AfterStory
int16_t Preview::afterStory() {
    attractAfterStory(video_.screen());
    const int16_t r = animResult(playAnim(anims_.afterStory, kAnimFadeIn, kAnimWatch | kAnimInput, 2));
    display_.fadeOut();
    return r;
}

// 2c11:07e5 Attract_BeforePrompt
int16_t Preview::beforePrompt() {
    display_.runLoad(attractBeforePrompt(video_, arc_));
    const int16_t r = animResult(playAnim(anims_.beforePrompt, kAnimFadeIn, kAnimWatch | kAnimInput, 0));
    display_.fadeOut();
    return r;
}

// 2c11:084c Attract_SignInPrompt
int16_t Preview::signInPrompt() {
    display_.runLoad(attractSignInPrompt(video_, fonts_, arc_));
    display_.fadeOutIn();
    rt_.timers().set(1, static_cast<uint16_t>(kAttractPageTicks));
    int16_t result = 0;
    for (bool done = false; !done;) {
        const uint16_t c = poll();
        if (c == kKeyEnter || c == kKeySpace) {
            result = 0;
            done = true;
        }
        if (rt_.timerExpired(1)) {
            result = 1;  // restart at the title (also over a key of the same poll)
            done = true;
        }
    }
    display_.fadeOut();
    return result;
}

// ---------------------------------------------------------------------------------------- sign-in

// The sign-in's dialogs, sounds and file writes for the preview: no boxes, nothing saved.
class PreviewSignInHost final : public SignInHost {
public:
    PreviewSignInHost(GgRuntime& rt, Video& video, Sound& sound) : rt_(rt), video_(video), sound_(sound) {}
    int yesNoRes(uint16_t id) override {
        log::info("preview: Dlg_YesNoRes({:04x}) answered No (dialogs are wave 3's)", id);
        return 0;
    }
    void messageRes(uint16_t id) override { log::info("preview: Dlg_MessageRes({:04x}) (dialogs are wave 3's)", id); }
    void playTypeEffect() override { sound_.effect(0, 0, 1); }
    void saveRoster(const Roster&) override { log::info("preview: the roster is not saved"); }
    uint32_t ticks() override { return rt_.ticks(); }
    int mouseX() override { return video_.cursor().x(); }
    int mouseY() override { return video_.cursor().y(); }

private:
    GgRuntime& rt_;
    Video& video_;
    Sound& sound_;
};

// 2caa:1240 State_02_SignIn up to the sign-in (SignIn_Run 2caa:0eb3 as the loop)
Next Preview::signIn() {
    keys_ = Keys::Effects;
    rt_.keyboard().setPlayMode(false);
    const Roster roster = previewRoster(ctx_);
    if (titleMusic_ < 0) titleMusic_ = sound_->loadMusic(kTitleMusic);
    auto signin = std::make_unique<SignIn>(exe_, arc_);  // SignIn_LoadButtons
    sound_->loadEffects(kSignInEffects);
    sound_->playMusic(titleMusic_);
    sound_->arrangement(titleMusic_, 1);
    fonts_.select(1);  // SignIn_ShowScreen
    display_.runLoad(signInDrawScreen(video_, arc_));
    display_.fadeOutIn();
    signin->begin(roster, rt_.ticks());
    PreviewSignInHost host(rt_, video_, *sound_);
    const UiCanvas canvas = ui();
    rt_.flushInput();  // SignIn_Run: Input_Flush, then Input_GetCode until it returns 0
    while (poll() != 0) {
    }
    signin->enter(canvas, host, &rt_.zones());
    // SignIn_Run's table CS:1210: the picture buttons' pressed pictures, their releases, Enter.
    PicButton* pressed = nullptr;
    auto release = [&] {
        if (pressed) signin->drawButtonNormal(canvas, *pressed);
        pressed = nullptr;
    };
    auto press = [&](PicButton& b) {
        if (!b.enabled) return;
        if (pressed && pressed != &b) signin->drawButtonNormal(canvas, *pressed);
        if (pressed != &b) signin->drawButtonPressed(canvas, b);
        pressed = &b;
    };
    Next next{Page::Hub, 0};
    try {
        for (bool done = false; !done;) {
            uint16_t c = poll();
            switch (c) {
                case 0: break;
                case 0x4706: case 0x8706: press(signin->cancelButton()); break;
                case 0x4707: case 0x8707: press(signin->newPlayerButton()); break;
                case 0x4708: case 0x8708: press(signin->startGameButton()); break;
                case kCodeCancel:
                case kCodeNewPlayer:
                    release();
                    signin->handleCode(canvas, host, c);
                    break;
                case kCodeStartGame:
                    release();
                    if (signin->handleCode(canvas, host, c) == kKeyEnter) done = true;
                    break;
                case kKeyEnter:
                case kKeySpace:  // the preview's way on (a typed name keeps no space)
                    release();
                    if (c == kKeyEnter) signin->handleCode(canvas, host, c);
                    done = true;
                    break;
                default:
                    release();
                    if ((c & 0xFFF) == kCodeRosterBar) break;  // the scroll bar's drag is wave 3's
                    if (c == kKeyDel) c = 8;
                    signin->handleCode(canvas, host, c);
                    break;
            }
        }
    } catch (...) {
        signin->leave(rt_.zones());
        throw;
    }
    signin->leave(rt_.zones());
    display_.fadeOut();
    sound_->freeEffects();  // SignIn_Free; the title music plays on into the hub
    return next;
}

// ------------------------------------------------------------------------------------------- hub

// 12e0:0148 State_03_Hub
Next Preview::hub() {
    keys_ = Keys::Global;
    rt_.keyboard().setPlayMode(false);
    if (titleMusic_ < 0) {
        titleMusic_ = sound_->loadMusic(kTitleMusic);
        sound_->playMusic(titleMusic_);
    }
    sound_->arrangement(titleMusic_, 2);
    PicCache cache(arc_, &video_.palette(), [this](PaletteLoad l) { display_.runLoad(std::move(l)); });
    HubScreen hub;
    hub.setup(video_, exe_, arc_, cache, HubProgress{});
    // Zones_AddLocalArray(8, DS:0132)
    std::array<Zone, 8> zones{};
    for (size_t i = 0; i < zones.size(); ++i)
        zones[i] = Zone::fromRecord(exe_.dsBytes(static_cast<uint16_t>(dg::kHubZones + 0x16 * i), 0x16));
    rt_.zones().addLocalArray(zones);
    Next next{Page::Warehouse, 0};
    try {
        for (bool first = true, done = false; !done; first = false) {
            frameStart();
            hub.frame(video_, ctx_.rand);
            if (first) display_.fadeOutIn();
            else display_.flush();
            frameEnd();
            const uint16_t c = poll();
            if (c == HubScreen::kSignIn) {
                next = {Page::SignIn, 0};
                done = true;
            } else if (c > HubScreen::kSignIn && c <= HubScreen::kSignIn + 3) {
                next = {Page::Warehouse, c - HubScreen::kSignIn - 1};
                done = true;
            } else if (c == kKeyEnter || c == kKeySpace) {
                done = true;
            }
        }
    } catch (...) {
        rt_.zones().clearLocal();
        hub.leave(video_);
        throw;
    }
    rt_.zones().clearLocal();
    hub.leave(video_);
    display_.fadeOut();
    return next;
}

// ------------------------------------------------------------------------------------- warehouse

// States 4, 5 and 7 (12e0:0820, 12e0:096a, 12e0:0619): the first race of the building, its rooms
Next Preview::warehouse(int building) {
    keys_ = Keys::Effects;
    rt_.keyboard().setPlayMode(false);  // the preview keeps the arrows and Space as keys
    // State_04: the title music freed, the building set up (Rooms_Load loads the room music).
    if (titleMusic_ >= 0) {
        sound_->freeMusic(titleMusic_);
        titleMusic_ = -1;
    }
    const int level = 0;
    PicCache cache(arc_, &video_.palette(), [this](PaletteLoad l) { display_.runLoad(std::move(l)); });
    Building b(exe_, arc_, cache);
    b.setup(building, level, ctx_.rand, hud_, video_);
    // A new visit's counters (12e0:08e3..0905: g_partsNeeded = Parts_NeededCount(), 10 bananas at
    // level 0); the actors are wave 3's (Building_NewVisit, Chimps_Spawn, the brains): none is placed.
    b.counters.partsNeeded = b.parts().neededCount;
    if (level == 0) b.counters.projectiles = 10;
    counters_ = b.counters;
    for (Actor& a : b.actors()) {
        a.disabled = 1;
        a.sprite.flags |= Actor::kHidden;
    }
    const int roomMusic = audio_->loadMusicBank(static_cast<uint16_t>(MusicCues::kRoomMusicBank + 2 * building));
    audio_->call([&](SndRes&) {
        audio_->cues().setRoomMusicBank(roomMusic);
        audio_->cues().setBuilding(building);
    });
    sound_->loadEffects(kWarehouseEffects);  // State_07_Warehouse
    const RoomMusicHook music = [&](int room) {
        if (room == 0) sound_->stopMusic(roomMusic);
        else sound_->roomMusic(building, room <= 3 ? room : 4, false, level);
    };
    const auto wipeSound = [this](int k, bool start) {
        if (start) sound_->effect(k, 1, 10);
        else sound_->stopEffect(k);
    };
    // The rooms with a picture (rooms 2 and 3 of a hall set have no objects of their own).
    std::vector<int> rooms;
    for (int r = 1; r < b.rooms().count(); ++r) {
        const auto& layers = b.rooms().room(r).layers;
        if (std::any_of(layers.begin(), layers.end(), [](const auto& l) { return !l.empty(); })) rooms.push_back(r);
    }
    if (rooms.empty()) rooms.push_back(std::min(1, b.rooms().count() - 1));
    size_t at = 0;
    auto enter = [&](int r) {
        drawMenuBar();  // State_05_EnterRoom
        b.enterRoom(r, video_, hud_, music);
        b.drawEntryFrame(video_, hud_);
        display_.show(arc_, ctx_.rand, wipeSound);  // Screen_Show: the fade-in, or the wipe
    };
    enter(rooms[at]);
    Next next{Page::Puzzles, 0};
    for (bool done = false; !done;) {
        frameStart();
        b.drawFrame(video_, hud_);
        display_.flush();
        frameEnd();
        const uint16_t c = poll();
        if (c == kKeyEnter || c == kKeySpace) {
            done = true;
        } else if ((c == kKeyLeft || c == kKeyRight) && rooms.size() > 1) {
            at = c == kKeyRight ? (at + 1) % rooms.size() : (at + rooms.size() - 1) % rooms.size();
            enter(rooms[at]);
        }
    }
    sound_->stopMusic(roomMusic);  // State_08_LeaveRoom
    display_.fadeOut();
    sound_->freeEffects();
    audio_->freeMusicBank(roomMusic);
    return next;
}

// ----------------------------------------------------------------------------------------- puzzles

// State 9's screens (38e2 Puzzle_Dispatch and the eight set-ups), variant 0 of each kind
Next Preview::puzzles() {
    keys_ = Keys::Puzzle;
    for (int kind = 0; kind < kPuzzleKinds; ++kind) {
        // The screen a puzzle draws over: the warehouse's HUD (State_08 only fades out), then the
        // menu bar (State_09_Puzzle).
        hud_.drawBar(video_.screen());
        hud_.drawLabels(video_.screen(), 7);
        hud_.drawCounters(video_.screen(), counters_);
        drawMenuBar();
        sound_->puzzleBank(puzzleKindInfo(kind).effectBank);
        auto pz = drawPuzzleScreen(video_, exe_, arc_, fonts_, kind, 0);
        // The fade-in's flush: the whole screen (the preview does not come from the warehouse,
        // whose picture the original's VRAM still holds where nothing new is drawn).
        video_.screen().dirty().mark(0, 0, Screen::kWidth, Screen::kHeight);
        display_.flush();
        display_.fadeIn();
        log::info("preview: puzzle {}", pz->describe());
        bool done = false;
        while (!done) {
            const uint16_t c = poll();
            done = c == kKeyEnter || c == kKeySpace;
        }
        display_.fadeOut();
        video_.cursor().clearRegions();
        video_.cursor().setCursorPicture(video_.cursor().arrow());
        sound_->puzzleFreeBank();
    }
    return {Page::Attract, 0};
}

}  // namespace

int previewMain(GgContext& ctx) {
    log::info("Wave 2 preview: the attract, then Enter / Space through the screens; Esc quits");
    Preview preview(ctx);
    try {
        preview.run();
    } catch (...) {
        preview.finish();
        throw;
    }
    preview.finish();
    return exit_code::kNormal;
}

}  // namespace tmport::gg
