#include "gg/ui/ui_cli.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "gg/data/archive.h"
#include "gg/data/cli_args.h"
#include "gg/data/gg_exe.h"
#include "gg/data/gg_files.h"
#include "gg/data/resources.h"
#include "gg/game/pic_cache.h"
#include "gg/gfx/video.h"
#include "gg/save/players_dat.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "gg/ui/attract.h"
#include "gg/ui/attract_anims.h"
#include "gg/ui/dialog.h"
#include "gg/ui/help.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/signin.h"
#include "gg/ui/text_viewer.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;
using cli::insideFolder;
using cli::openFolder;
using cli::optionValue;
using cli::optionValues;

constexpr std::string_view kCommands[] = {"--gg-screen", "--gg-ui-info"};

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void err(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stderr); }

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::vector<std::string> operands(const std::vector<std::string>& args, std::string_view cmd, size_t n) {
    const auto it = std::find(args.begin(), args.end(), cmd);
    if (static_cast<size_t>(args.end() - it) < n + 1) throw UsageError(std::format("{} needs {} arguments", cmd, n));
    return {it + 1, it + 1 + static_cast<ptrdiff_t>(n)};
}

int intOption(const std::vector<std::string>& args, std::string_view name, int def) {
    const auto v = optionValue(args, name);
    try {
        return v ? std::stoi(*v) : def;
    } catch (const std::exception&) {
        throw UsageError(std::format("{} needs a number", name));
    }
}

uint16_t parseId(std::string_view s) {
    if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
    if (s.empty() || s.size() > 4) throw UsageError(std::format("bad resource id '{}'", s));
    unsigned v = 0;
    for (const char c : s) {
        const int d = (c >= '0' && c <= '9')   ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                               : -1;
        if (d < 0) throw UsageError(std::format("bad resource id '{}'", s));
        v = v * 16 + static_cast<unsigned>(d);
    }
    return static_cast<uint16_t>(v);
}

int number(const std::string& s) {
    try {
        size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used == s.size()) return v;
    } catch (const std::exception&) {
    }
    throw UsageError("not a number: '" + s + "'");
}

// "a:b:c" -> {"a", "b", "c"}
std::vector<std::string> splitColon(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t c = s.find(':', start);
        parts.push_back(s.substr(start, c == std::string::npos ? std::string::npos : c - start));
        if (c == std::string::npos) return parts;
        start = c + 1;
    }
}

std::optional<std::pair<int, int>> pointOption(const std::vector<std::string>& args, std::string_view name) {
    const auto v = optionValue(args, name);
    if (!v) return std::nullopt;
    const auto comma = v->find(',');
    if (comma == std::string::npos) throw UsageError(std::format("{} needs X,Y", name));
    try {
        return std::pair{std::stoi(v->substr(0, comma)), std::stoi(v->substr(comma + 1))};
    } catch (const std::exception&) {
        throw UsageError(std::format("{} needs X,Y", name));
    }
}

// --- composing ---------------------------------------------------------------------------------------

struct Composer {
    const GgFiles& files;
    const GgExe& exe;
    const Archive& arc;
    Video video;
    Fonts fonts;
    DialogStrings strings;
    std::vector<std::string> rects;
    bool forced = false;  // Mouse_ForceDefault(1) (dialogs, menus): the forced region's arrow
    std::optional<std::filesystem::path> frameDir;  // --frame-dir: every frame of the animated step
    int scale = 1;
    // The picture cache the screen animations load their frames through (palettes into the shadow).
    PicCache cache{arc, &video.palette()};
    BorlandRand rand;  // the screen animations' rand() (the attract's scripts have no random step)

    explicit Composer(const GgFiles& f)
        : files(f), exe(f.exe()), arc(f.archive()), fonts(Fonts::load(f.archive())), strings(DialogStrings::fromExe(f.exe())) {
        video.init(exe, arc);  // Gfx_Init, Mouse_Init, Pal_Blackout (the tool ends with the shadow in the DAC)
        video.cursor().hide();
        // Game_Init's Hud_Init (1a04:025b): PicCache_LoadBundle(0x1058) applies the bundle's palette
        // member (colours 16..63, the menu bar's among them) to the shadow before the attract.
        const Bundle hud = Bundle::load(arc, kHudBundle);
        for (const BundleMember& m : hud.members())
            if (m.resType() == ResType::P && isPaletteRange(m.payload)) video.palette().store(parsePaletteRange(m.payload));
    }
    static constexpr uint16_t kHudBundle = 0x1058;
    UiCanvas ui() { return UiCanvas::of(video, fonts); }
    void rect(std::string_view name, int x, int y, int w, int h) {
        rects.push_back(std::format("rect {} {} {} {} {}\n", name, x, y, w, h));
    }
    void storePalette(uint16_t id) { video.palette().store(parsePaletteRange(arc.read(id))); }

    // The settled frame: the cursor at the pointer (when asked) drawn by a last Dirty_Flush, the shadow
    // palette in the DAC (every fade done).
    std::vector<uint8_t> finish(std::optional<std::pair<int, int>> cursor) {
        if (cursor) {
            Cursor& c = video.cursor();
            c.forceDefault(forced ? 1 : 0);
            c.show();
            c.restoreUnder();
            c.setPosition(cursor->first, cursor->second);
            video.screen().dirty().mark(cursor->first, cursor->second, 1, 1);
        }
        video.flush();
        return picture();
    }
    // VRAM through the shadow palette (a settled frame) as RGBA bytes.
    std::vector<uint8_t> picture() {
        video.dac().set(video.palette().shadow());
        const auto rgba = video.frameRgba();
        std::vector<uint8_t> bytes(rgba.size() * 4);
        for (size_t i = 0; i < rgba.size(); ++i)
            for (int k = 0; k < 4; ++k) bytes[i * 4 + static_cast<size_t>(k)] = static_cast<uint8_t>(rgba[i] >> (8 * k));
        return bytes;
    }
};

void writePng(const std::filesystem::path& path, const std::vector<uint8_t>& rgba, int scale);

void writePng(const std::filesystem::path& path, const std::vector<uint8_t>& rgba, int scale) {
    if (scale <= 1) {
        if (!writeWholeFile(path, mods::encodePngRgba(320, 200, rgba))) throw std::runtime_error("cannot write " + pathToUtf8(path));
        return;
    }
    std::vector<uint8_t> big(static_cast<size_t>(320 * scale) * 200 * scale * 4);
    for (int y = 0; y < 200 * scale; ++y)
        for (int x = 0; x < 320 * scale; ++x)
            std::copy_n(rgba.data() + (static_cast<size_t>(y / scale) * 320 + x / scale) * 4, 4,
                        big.data() + (static_cast<size_t>(y) * 320 * scale + x) * 4);
    if (!writeWholeFile(path, mods::encodePngRgba(320 * scale, 200 * scale, big)))
        throw std::runtime_error("cannot write " + pathToUtf8(path));
}

// The attract's frames: Attract_PlayAnim's loop without timers or input (the screen animations draw
// into the RAM screen, the flush follows).
// `watch` >= 0: the original leaves when that channel's script reaches -1 (flags 4). With
// `target`, --frame-dir receives every frame.
void playFrames(Composer& c, AttractAnimHost& host, const AttractAnim& anim, int frames, int fade, int watch = -1,
                bool target = false) {
    AttractAnimPlayer player(c.exe, anim, host);
    if (target && c.frameDir) std::filesystem::create_directories(*c.frameDir);
    for (int f = 0; f < frames; ++f) {
        player.startChannels();
        host.frame();
        c.video.flush();
        if (f == 0 && fade == kAnimFadeIn) c.video.palette().setFaded(false);  // Pal_FadeIn's end state
        if (target && c.frameDir) writePng(*c.frameDir / std::format("frame_{:04d}.png", f + 1), c.picture(), c.scale);
        player.advance();
        if (watch >= 0 && player.channelAtEnd(watch)) {
            out(std::format("note the watched channel ended after frame {}\n", f + 1));
            break;
        }
    }
}

// The attract up to (and including) the drawing of `step` (1..11), as Seq_Run runs it from the boot;
// the animations of earlier steps are skipped (each later step redraws the screen). `frames` and
// `phase` select the moment of an animated step.
void composeAttract(Composer& c, int step, int phase, int frames) {
    AttractAnimHost host(c.video, c.cache, c.rand);  // no sound in the tool
    const AttractAnims anims = AttractAnims::fromExe(c.exe);
    Screen& s = c.video.screen();
    // step 1: the title
    attractTitleBackground(c.video, c.arc);
    if (step == 1) {
        // Each phase is its own Attract_PlayAnim (the animations restart). An earlier phase runs until
        // Timer_Expired(1): the first frame after tick > n * 146, i.e. frame floor(n * 146 / 4) + 1 of
        // exactly 4 ticks (the fade-in's time on the first frame is not modelled).
        constexpr int kPhaseFrames[3] = {1 * 146 / 4 + 1, 3 * 146 / 4 + 1, 5 * 146 / 4 + 1};
        const Bundle art = Bundle::load(c.arc, kTitleArt);  // Res_LoadCopy
        for (int p = 1; p <= phase; ++p) {
            if (p >= 2) {
                attractDrawTitleArt(s, art, p);
                c.video.flush();
            }
            playFrames(c, host, anims.title, p == phase ? frames : kPhaseFrames[p - 1], p == 1 ? kAnimFadeIn : 0, -1,
                       p == phase);
        }
        return;
    }
    c.video.palette().setFaded(false);  // the first PlayAnim's Pal_FadeIn
    // step 2
    attractAfterTitle(c.video, c.arc);
    if (step == 2) {
        playFrames(c, host, anims.afterTitle, frames, kAnimFadeFromSaved, 0, true);
        return;
    }
    c.video.palette().setFaded(true);  // Pal_FadeOut
    // steps 3..8: the story pages
    for (int page = 0; page < kStoryPages && step >= 3; ++page) {
        attractStoryPage(c.video, c.fonts, c.arc, static_cast<uint16_t>(kStoryFirstText + page));
        if (page == 0) c.video.palette().setFaded(false);  // Screen_FadeOutIn
        c.video.flush();
        if (step == 3 + page) return;
    }
    // step 9
    attractAfterStory(s);
    if (step == 9) {
        playFrames(c, host, anims.afterStory, frames, kAnimFadeIn, 2, true);
        return;
    }
    c.video.palette().setFaded(true);
    // step 10
    attractBeforePrompt(c.video, c.arc);
    if (step == 10) {
        playFrames(c, host, anims.beforePrompt, frames, kAnimFadeIn, 0, true);
        return;
    }
    c.video.palette().setFaded(true);
    // step 11
    attractSignInPrompt(c.video, c.fonts, c.arc);
    c.video.palette().setFaded(false);  // Screen_FadeOutIn
    c.video.flush();
}

// SignInHost for the tool: no modal boxes (they restore what they cover), no sound, no file writes.
class ToolSignInHost final : public SignInHost {
public:
    explicit ToolSignInHost(std::pair<int, int> mouse) : mouse_(mouse) {}
    int yesNoRes(uint16_t id) override {
        out(std::format("note Dlg_YesNoRes({:04x}) answered No\n", id));
        return 0;
    }
    void messageRes(uint16_t id) override { out(std::format("note Dlg_MessageRes({:04x})\n", id)); }
    void playTypeEffect() override {}
    void saveRoster(const Roster&) override {}
    uint32_t ticks() override { return 0; }
    int mouseX() override { return mouse_.first; }
    int mouseY() override { return mouse_.second; }

private:
    std::pair<int, int> mouse_;
};

Roster loadRoster(const GgFiles& files, const std::vector<std::string>& args) {
    std::optional<std::filesystem::path> path;
    if (const auto p = optionValue(args, "--players")) path = pathFromUtf8(*p);
    else if (!hasFlag(args, "--new-file")) path = files.playersDatPath();
    if (path) {
        const auto bytes = readWholeFile(*path);  // read only
        if (!bytes) throw std::runtime_error("cannot read " + pathToUtf8(*path));
        if (bytes->size() >= kRosterSize) {
            const Roster r = Roster::fromBytes(std::span<const uint8_t, kRosterSize>(bytes->data(), kRosterSize));
            if (r.magicOk() && r.version == kPlayersVersion) return r;
        }
        out(std::format("note {} is not a version-12 PLAYERS.DAT: a new roster is used\n", pathToUtf8(*path)));
    }
    return PlayersDat::create().roster;  // 2caa:004f Roster_LoadOrCreate's creation
}

// State_02_SignIn up to the name entry (and, for `chimps`, the With/Without Chimps question).
void composeSignIn(Composer& c, const std::vector<std::string>& args, bool chimps, std::pair<int, int> mouse) {
    // The palette history of the w0 tour: the boot's attract up to story page 1 (Space there).
    c.storePalette(kTitlePalette);
    c.storePalette(kAfterTitlePalette);
    c.storePalette(kStoryPalette);
    c.video.palette().setFaded(true);  // Pal_FadeOut of the attract
    SignIn signin(c.exe, c.arc);       // SignIn_LoadButtons
    UiCanvas ui = c.ui();
    c.fonts.select(1);                 // SignIn_ShowScreen
    signInDrawScreen(c.video, c.arc);
    c.video.flush();                   // Screen_FadeOutIn
    c.video.palette().setFaded(false);
    signin.begin(loadRoster(c.files, args), 0);
    ToolSignInHost host(mouse);
    signin.enter(ui, host);
    if (hasFlag(args, "--new")) signin.handleCode(ui, host, kCodeNewPlayer);
    if (const auto t = optionValue(args, "--type"))
        for (const char ch : *t) signin.handleCode(ui, host, static_cast<uint8_t>(ch));
    if (const auto codes = optionValue(args, "--codes")) {  // input codes in hex, e.g. 700 (a list click), 150
        size_t start = 0;
        for (;;) {
            const size_t comma = codes->find(',', start);
            signin.handleCode(ui, host, parseId(codes->substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    c.rect("list", 62, 46, 180, 88);
    c.rect("bar", signin.scrollBar().x, signin.scrollBar().y, signin.scrollBar().w, signin.scrollBar().h);
    for (PicButton* b : {&signin.cancelButton(), &signin.newPlayerButton(), &signin.startGameButton()})
        c.rect("button", b->zone.x, b->zone.y, b->zone.w, b->zone.h);
    if (!chimps) return;
    // SignIn_Run: Start Game pressed and released over its button
    signin.drawButtonPressed(ui, signin.startGameButton());
    signin.drawButtonNormal(ui, signin.startGameButton());
    if (signin.handleCode(ui, host, kCodeStartGame) != 0x0D) throw std::runtime_error("Start Game did not sign in");
    if (SignIn::points(signin.roster().entries[static_cast<size_t>(signin.index())]) >= 0) {
        out("note an existing player signs in: no chimps question\n");
        return;
    }
    // State_02_SignIn: Player_InitRecord; Dlg_AskChimps -> Dlg_RunModal(desc, 1, 1)
    const OwnedDialog d = askChimpsDesc(c.strings, c.arc);
    c.fonts.select(0);
    c.forced = true;
    SavedImage under;
    const DialogLayout L = dlgDraw(ui, d.desc, &under);
    c.rect("box", L.x, L.y, L.w, L.h);
}

void drawBar(Composer& c) {
    const MenuBarDef bar = MenuBarDef::fromExe(c.exe);
    const MenuBarPictures pics = MenuBarPictures::load(c.arc);
    menuDrawBar(c.ui(), bar, pics, -1);
    c.rect("bar", 0, 0, 320, 9);
}

DialogLayout drawModal(Composer& c, const DialogDesc& d, std::string_view name = "box") {
    c.fonts.select(0);  // Dlg_RunModal 3f56:0663
    c.forced = true;    // Mouse_ForceDefault(1)
    SavedImage under;
    const DialogLayout L = dlgDraw(c.ui(), d, &under);
    c.rect(name, L.x, L.y, L.w, L.h);
    return L;
}

int menuIndex(const std::string& name) {
    if (name == "help") return kMenuHelp;
    if (name == "file") return kMenuFile;
    if (name == "options") return kMenuOptions;
    throw UsageError("menu: help, file or options");
}

int cmdScreen(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-screen", 2);
    const auto outPath = pathFromUtf8(ops[1]);
    if (insideFolder(outPath, files.folder())) throw UsageError("refusing to write into the game folder: " + pathToUtf8(outPath));
    const std::vector<std::string> name = splitColon(ops[0]);
    const auto cursor = pointOption(args, "--cursor");
    const int frames = intOption(args, "--frame", 1);
    Composer c(files);
    c.scale = intOption(args, "--scale", 1);
    if (optionValue(args, "--fill")) {  // a background of one colour (and its back page)
        c.video.screen().setDrawColor(static_cast<uint8_t>(intOption(args, "--fill", 0)));
        c.video.screen().fillRect(0, 0, 320, 200);
        c.video.screen().saveAll();
        c.video.screen().setDrawColor(0);
    }
    if (const auto d = optionValue(args, "--frame-dir")) {
        c.frameDir = pathFromUtf8(*d);
        if (insideFolder(*c.frameDir, files.folder())) throw UsageError("refusing to write into the game folder");
    }
    for (const auto& p : optionValues(args, "--pal")) {  // ID or ID/member
        const auto slash = p.find('/');
        const uint16_t id = parseId(std::string_view(p).substr(0, slash));
        if (slash == std::string::npos) c.storePalette(id);
        else c.video.palette().store(parsePaletteRange(Bundle::load(c.arc, id).member(static_cast<size_t>(number(p.substr(slash + 1)))).payload));
    }
    const std::string& kind = name[0];
    auto arg = [&](size_t i) -> const std::string& {
        if (name.size() <= i) throw UsageError(std::format("{} needs :{}", kind, i == 1 ? "ID" : "N"));
        return name[i];
    };
    if (kind == "title") {
        const int phase = intOption(args, "--phase", 3);
        if (phase < 1 || phase > 3) throw UsageError("--phase 1..3");
        composeAttract(c, 1, phase, frames);
    } else if (kind == "after-title") {
        composeAttract(c, 2, 0, frames);
    } else if (kind.starts_with("story") && kind.size() == 6 && kind[5] >= '1' && kind[5] <= '6') {
        composeAttract(c, 3 + (kind[5] - '1'), 0, 0);
        c.rect("box", 57, 38, 177, 53);
    } else if (kind == "after-story") {
        composeAttract(c, 9, 0, frames);
    } else if (kind == "before-prompt") {
        composeAttract(c, 10, 0, frames);
    } else if (kind == "prompt") {
        composeAttract(c, 11, 0, 0);
    } else if (kind == "signin" || kind == "name" || kind == "chimps") {
        composeSignIn(c, args, kind == "chimps", cursor.value_or(std::pair{160, 100}));
    } else if (kind == "hint") {
        const HintBox box = HintBox::load(c.strings, c.arc, parseId(arg(1)));
        drawModal(c, box.desc);  // Hint_Show: Font_Select(0), Mouse_ForceDefault(1), Dlg_MessageBox
    } else if (kind == "yesno") {
        if (!hasFlag(args, "--no-bar")) drawBar(c);
        const OwnedDialog d = yesNoResDesc(c.strings, c.arc, parseId(arg(1)));
        drawModal(c, d.desc);
    } else if (kind == "message") {
        const OwnedDialog d = messageResDesc(c.strings, c.arc, parseId(arg(1)));
        drawModal(c, d.desc);
    } else if (kind == "menubar") {
        drawBar(c);
    } else if (kind == "menu") {
        const int i = menuIndex(arg(1));
        const int row = name.size() > 2 ? number(name[2]) : 0;
        const MenuBarDef bar = MenuBarDef::fromExe(c.exe);
        const MenuBarPictures pics = MenuBarPictures::load(c.arc);
        UiCanvas ui = c.ui();
        menuDrawBar(ui, bar, pics, -1);  // Menu_SetMenuBar
        c.forced = true;                 // Menu_RunDropDown: Mouse_ForceDefault(1)
        menuDrawBar(ui, bar, pics, i);   // open(i)
        SavedImage under;
        const DialogLayout L = menuDrawDropDown(ui, c.strings, bar.menus[static_cast<size_t>(i)], &under);
        menuToggleRow(ui, L, 0);
        if (row != 0) {
            menuToggleRow(ui, L, 0);
            menuToggleRow(ui, L, row);
        }
        c.rect("bar", 0, 0, 320, 9);
        c.rect("box", L.x, L.y, L.w, L.h);
    } else if (kind == "help") {
        const HelpTopic topic = HelpTopic::load(c.arc, parseId(arg(1)));
        const int page = name.size() > 2 ? number(name[2]) : 0;
        if (page < 0 || page >= topic.pageCount()) throw UsageError(std::format("the topic has {} pages", topic.pageCount()));
        if (!hasFlag(args, "--no-bar")) drawBar(c);  // the Help menu's choose: Menu_DrawBar(-1)
        for (int p = 0; p <= page; ++p) drawModal(c, topic.pageDesc(c.strings, p), p == page ? "box" : "under");
    } else if (kind == "textview") {
        const auto bytes = c.arc.read(parseId(arg(1)));
        TextViewer view(c.exe, c.arc, text::untilNul(bytes));
        UiCanvas ui = c.ui();
        view.open(ui, nullptr);
        const int line = name.size() > 2 ? number(name[2]) : 0;
        for (int k = 0; k < line; ++k) {  // the down arrow's release: line + 1, thumb down, redraw
            view.scrollBy(1);
            view.thumbDown();
        }
        if (line) view.redraw(ui);
        c.forced = true;
        c.rect("panel", 0, 28, 129, 138);
        out(std::format("note lines {} scroll max {} thumb step {} thumb y {}\n",
                        TextViewLayout::countLines(c.fonts, text::untilNul(bytes)), view.scrollMax(), view.thumbStep(), view.thumbY()));
    } else {
        throw UsageError("unknown screen '" + ops[0] + "' (the world's screens: hub, warehouse, blueprint, puzzle)");
    }
    const auto rgba = c.finish(cursor);
    writePng(outPath, rgba, intOption(args, "--scale", 1));
    for (const auto& r : c.rects) out(r);
    out(std::format("{} -> {}\n", ops[0], pathToUtf8(outPath)));
    return 0;
}

int cmdInfo(const GgFiles& files) {
    const GgExe& exe = files.exe();
    const Archive& arc = files.archive();
    Fonts fonts = Fonts::load(arc);
    const DialogStrings strings = DialogStrings::fromExe(exe);
    const MenuBarDef bar = MenuBarDef::fromExe(exe);
    out(std::format("menu bar: {} menus\n", bar.count()));
    for (int i = 0; i < bar.count(); ++i) {
        const MenuDef& m = bar.menus[static_cast<size_t>(i)];
        const DialogLayout L = dlgLayout(fonts, menuDropDownDesc(strings, m));
        out(std::format("menu {}: title x {} width {}, {} items, drop-down width {}, box ({}, {}) {}x{} inner {}\n", i, m.titleX,
                        m.titleWidth, m.itemCount, m.dropWidth, L.x, L.y, L.w, L.h, L.innerW));
        for (size_t k = 0; k < m.handlers.size(); ++k) out(std::format("  item {}: {}\n", k, menuCommandName(m.handlers[k])));
    }
    for (uint16_t id = 0x1064; id <= 0x106F; ++id) {
        try {
            if (id == kChimpsBundle) {
                const OwnedDialog d = askChimpsDesc(strings, arc);
                const DialogLayout L = dlgLayout(fonts, d.desc);
                out(std::format("{:04x} chimps: box ({}, {}) {}x{}\n", id, L.x, L.y, L.w, L.h));
                continue;
            }
            const Bundle b = Bundle::load(arc, id);
            const bool yesNo = b.size() == 3 && b.member(0).resType() == ResType::S;
            const OwnedDialog d = yesNo ? yesNoResDesc(strings, arc, id) : messageResDesc(strings, arc, id);
            const DialogLayout L = dlgLayout(fonts, d.desc);
            out(std::format("{:04x} {}: box ({}, {}) {}x{}, {} buttons, focus {}\n", id, yesNo ? "yes/no" : "message", L.x, L.y,
                            L.w, L.h, L.buttonCount, d.desc.focus));
        } catch (const std::exception& e) {
            out(std::format("{:04x}: {}\n", id, e.what()));
        }
    }
    for (uint16_t id = kHelpAboutGame; id <= kHelpSolvingPuzzles; ++id) {
        const HelpTopic t = HelpTopic::load(arc, id);
        out(std::format("help {:04x}: {} pages", id, t.pageCount()));
        for (const auto& p : t.pages) out(std::format(" {}", p.pictures.size()));
        out(" pictures\n");
    }
    for (uint16_t id = 0x1043; id <= 0x1055; ++id) {
        if (!arc.exists(id)) continue;
        const HintBox box = HintBox::load(strings, arc, id);
        const DialogLayout L = dlgLayout(fonts, box.desc);
        out(std::format("hint {:04x}: box ({}, {}) {}x{}\n", id, L.x, L.y, L.w, L.h));
    }
    return 0;
}

}  // namespace

bool uiCli(const std::vector<std::string>& args, int& exitCode) {
    const auto cmd = std::find_if(args.begin(), args.end(), [](const std::string& a) {
        return std::find(std::begin(kCommands), std::end(kCommands), a) != std::end(kCommands);
    });
    if (cmd == args.end()) return false;
    try {
        const GgFiles files = openFolder(args);
        if (*cmd == "--gg-screen") exitCode = cmdScreen(files, args);
        else exitCode = cmdInfo(files);
    } catch (const UsageError& e) {
        err(std::string("ggport: ") + e.what() + "\n");
        exitCode = 2;
    } catch (const std::exception& e) {
        err(std::string("ggport: ") + e.what() + "\n");
        exitCode = 1;
    }
    std::fflush(stdout);
    return true;
}

}  // namespace tmport::gg
