#include "ui/menu_bar.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "mischief/gfx/presenter.h"
#include "mischief/util/error.h"
#include "mischief/util/log.h"
#include "platform/engine.h"
#include "text/font.h"

namespace tmport::ui {

namespace {

// Packed RGBA (little-endian bytes R, G, B, A), as the presenter canvas.
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}
// The Windows 3.1 default scheme: white menus, black text, navy highlight, grey disabled text.
constexpr uint32_t kMenuBg = rgba(255, 255, 255);
constexpr uint32_t kMenuText = rgba(0, 0, 0);
constexpr uint32_t kFrame = rgba(0, 0, 0);
constexpr uint32_t kHighlightBg = rgba(0, 0, 128);
constexpr uint32_t kHighlightText = rgba(255, 255, 255);
constexpr uint32_t kGrayText = rgba(128, 128, 128);
constexpr uint32_t kShadowColour = rgba(64, 64, 64);

constexpr int kCanvasW = mischief::gfx::Presenter::kCanvasWidth;
constexpr int kCanvasH = mischief::gfx::Presenter::kCanvasHeight;

uint8_t upperAscii(uint8_t c) { return (c >= 'a' && c <= 'z') ? static_cast<uint8_t>(c - 'a' + 'A') : c; }

struct HandlerEntry {
    uint16_t selector, offset;
    MenuCommand command;
};
// code_10f8-1148.md §3.8/§13, code_1168-1198.md §5, ARCHITECTURE.md §7.1 C6/C7.
constexpr HandlerEntry kHandlers[] = {
    {0x10F0, 0x0487, MenuCommand::SeeRank},
    {0x1140, 0x0062, MenuCommand::StartAnotherGame},
    {0x1140, 0x0098, MenuCommand::Exit},
    {0x1140, 0x0015, MenuCommand::NewPlayerMessages},
    {0x1140, 0x00CC, MenuCommand::MusicAndSound},
    {0x1140, 0x00F7, MenuCommand::Voice},
    {0x1180, 0x0117, MenuCommand::HelpAboutGame},
    {0x1180, 0x0128, MenuCommand::HelpKeyboard},
    {0x1180, 0x0139, MenuCommand::HelpMouse},
    {0x1180, 0x014A, MenuCommand::HelpEducation},
    {0x1140, 0x0000, MenuCommand::AboutTM},
};

MenuBar::Rect unite(const MenuBar::Rect& a, const MenuBar::Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

// ------------------------------------------------------------------------------------ model

const char* menuCommandName(MenuCommand c) {
    switch (c) {
        case MenuCommand::None: return "none";
        case MenuCommand::SeeRank: return "see-rank";
        case MenuCommand::StartAnotherGame: return "start-another-game";
        case MenuCommand::Exit: return "exit";
        case MenuCommand::NewPlayerMessages: return "new-player-messages";
        case MenuCommand::MusicAndSound: return "music-and-sound";
        case MenuCommand::Voice: return "voice";
        case MenuCommand::HelpAboutGame: return "help-about-the-game";
        case MenuCommand::HelpKeyboard: return "help-keyboard";
        case MenuCommand::HelpMouse: return "help-mouse";
        case MenuCommand::HelpEducation: return "help-educational-focus";
        case MenuCommand::AboutTM: return "about";
        case MenuCommand::Count: break;
    }
    return "?";
}

MenuCommand menuCommandForHandler(uint16_t selector, uint16_t offset) {
    for (const auto& h : kHandlers)
        if (h.selector == selector && h.offset == offset) return h.command;
    return MenuCommand::None;
}

MenuLabel MenuLabel::parse(std::span<const uint8_t> raw) {
    // Windows menu text: '&x' underlines x (the mnemonic), '&&' is a literal '&'.
    MenuLabel l;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '&' && i + 1 < raw.size()) {
            ++i;
            if (raw[i] != '&' && l.mnemonicIndex < 0) {
                l.mnemonicIndex = static_cast<int>(l.text.size());
                l.key = upperAscii(raw[i]);
            }
        }
        l.text.push_back(raw[i]);
    }
    return l;
}

// 1078:0000 Menu_SetMenuBar (the reading part): MenuBarDef {i16 count; MenuPopupDef far*}, each
// popup {char far* title; i16 unk; i16 itemCount; char far* items; FARPROC far* handlers; ...}.
MenuBarModel MenuBarModel::fromExe(const ExeTables& exe, uint16_t off) {
    MenuBarModel m;
    const int count = exe.i16(off);
    const auto popups = exe.dgPtr(off + 2u);
    if (count < 0 || count > 16 || !popups) throw mischief::FormatError("menu bar definition: bad popup table");
    for (int i = 0; i < count; ++i) {
        const uint32_t p = *popups + 0x14u * static_cast<uint32_t>(i);
        MenuDef def;
        def.title = MenuLabel::parse(exe.farCstr(p + 0x00));
        const int itemCount = exe.i16(p + 0x06);
        if (itemCount < 0 || itemCount > 64) throw mischief::FormatError("menu bar definition: bad item count");
        const auto items = exe.farCstr(p + 0x08);
        const auto handlers = exe.dgPtr(p + 0x0C);
        // Items are separated by '\n'; the scan of 1078:0000 stops at '\n' or NUL. Items missing
        // from the string (not in the shipped data) stay empty here.
        size_t pos = 0;
        for (int j = 0; j < itemCount; ++j) {
            MenuItemDef it;
            size_t end = pos;
            while (end < items.size() && items[end] != '\n') ++end;
            if (pos < items.size()) it.label = MenuLabel::parse(items.subspan(pos, end - pos));
            pos = end + 1;
            it.commandId = static_cast<uint16_t>(i * 0x100 + j);
            if (handlers) {
                const FarPtr h = exe.farPtr(*handlers + 4u * static_cast<uint32_t>(j));
                if (h.kind == FarPtr::Kind::Segment) {
                    it.handlerSelector = h.selector();
                    it.handlerOffset = h.offset;
                    it.command = menuCommandForHandler(it.handlerSelector, it.handlerOffset);
                }
            }
            def.items.push_back(std::move(it));
        }
        m.menus.push_back(std::move(def));
    }
    return m;
}

MenuBarModel MenuBarModel::fromExe(const ExeTables& exe) { return fromExe(exe, dg::kMenuBar); }

const MenuItemDef* MenuBarModel::find(uint16_t commandId) const {
    const size_t menu = commandId >> 8, item = commandId & 0xFF;
    if (menu >= menus.size() || item >= menus[menu].items.size()) return nullptr;
    return &menus[menu].items[item];
}

// -------------------------------------------------------------------------------- menu bar

MenuBar::MenuBar(Engine& engine, MenuBarModel model, const text::Font& font)
    : engine_(engine), model_(std::move(model)), font_(font) {
    layout();
    engine_.setEventFilter([this](const SDL_Event& e) { return handleEvent(e); });
    pumpHookId_ = engine_.addPumpHook([this](PumpPhase p) { onPump(p); });
}

MenuBar::~MenuBar() {
    engine_.setEventFilter({});
    engine_.removePumpHook(pumpHookId_);
    engine_.presenter().clearOverlay();
}

int MenuBar::fontHeight() const { return font_.height(); }

int MenuBar::labelWidth(const MenuLabel& l) const {
    int w = 0;
    for (uint8_t c : l.text) w += font_.charWidth(c);
    return w;
}

void MenuBar::layout() {
    // The lowest ink row of 'H' (a capital without descender); the underline goes two rows below.
    baseline_ = font_.height() - 2;
    for (int y = font_.height() - 1; y >= 0; --y) {
        bool ink = false;
        for (int x = 0; x < font_.charWidth('H'); ++x) ink = ink || font_.pixel('H', x, y);
        if (ink) {
            baseline_ = y;
            break;
        }
    }
    itemHeight_ = font_.height() + 2 * kItemPadV;
    titles_.clear();
    dropWidth_.clear();
    int x = kBarLeft;
    for (const MenuDef& m : model_.menus) {
        const int w = labelWidth(m.title) + 2 * kTitlePad;
        titles_.push_back({x, 0, w, kBarHeight - 1});
        x += w;
        int iw = 0;
        for (const MenuItemDef& it : m.items) iw = std::max(iw, labelWidth(it.label));
        dropWidth_.push_back(iw + kItemPadLeft + kItemPadRight + 2);
    }
}

MenuBar::Rect MenuBar::titleRect(int menu) const {
    if (menu < 0 || menu >= static_cast<int>(titles_.size())) return {};
    return titles_[static_cast<size_t>(menu)];
}

MenuBar::Rect MenuBar::dropDownRect(int menu) const {
    if (menu < 0 || menu >= static_cast<int>(titles_.size())) return {};
    const auto& t = titles_[static_cast<size_t>(menu)];
    const int n = static_cast<int>(model_.menus[static_cast<size_t>(menu)].items.size());
    Rect r{t.x, kBarHeight, dropWidth_[static_cast<size_t>(menu)], 2 + 2 + n * itemHeight_};
    if (r.x + r.w + kShadow > kCanvasW) r.x = std::max(0, kCanvasW - kShadow - r.w);
    return r;
}

MenuBar::Rect MenuBar::itemRect(int menu, int item) const {
    const Rect box = dropDownRect(menu);
    if (box.empty() || item < 0 || item >= static_cast<int>(model_.menus[static_cast<size_t>(menu)].items.size())) return {};
    return {box.x + 1, box.y + 2 + item * itemHeight_, box.w - 2, itemHeight_};
}

// --- 1120 / 1078 ------------------------------------------------------------------------------------

void MenuBar::install() {
    installed_ = true;
    enabled_ = true;  // Menu_SetMenuBar sets g_menusEnabled = 1
    redraw();
}

void MenuBar::remove() {
    if (tracking_) stopTracking();
    installed_ = false;
    enabled_ = false;  // Menu_SetMenuBar(NULL) sets g_menusEnabled = 0
    pending_.reset();
    redraw();
}

void MenuBar::enableAll(bool enable) {
    if (!installed_) return;  // g_menuDef == NULL: nothing happens, g_menusEnabled unchanged
    enabled_ = enable;
    if (!enable && tracking_) stopTracking();
    redraw();
}

// --- commands -----------------------------------------------------------------------------------------

void MenuBar::setHandler(MenuCommand c, Handler h) {
    if (c == MenuCommand::None || c == MenuCommand::Count) return;
    handlers_[static_cast<size_t>(c)] = std::move(h);
}

bool MenuBar::hasHandler(MenuCommand c) const {
    return c != MenuCommand::Count && static_cast<bool>(handlers_[static_cast<size_t>(c)]);
}

void MenuBar::postCommand(uint16_t commandId) { pending_ = commandId; }

// 1078:0135 Menu_OnCommand
bool MenuBar::dispatch(uint16_t commandId) {
    if (inResourceLoad_) {
        ++stats_.droppedLoad;
        mischief::log::debug("menu: command {:#06x} ignored during a resource load", commandId);
        return false;
    }
    if (inHandler_) {
        ++stats_.droppedReentrant;
        mischief::log::debug("menu: command {:#06x} ignored while a menu command runs", commandId);
        return false;
    }
    if (!installed_ || !enabled_) {
        ++stats_.droppedDisabled;
        mischief::log::debug("menu: command {:#06x} ignored, menu {}", commandId, installed_ ? "disabled" : "removed");
        return false;
    }
    const MenuItemDef* item = model_.find(commandId);
    if (!item) {
        ++stats_.droppedUnknown;
        mischief::log::warn("menu: unknown command {:#06x}", commandId);
        return false;
    }
    ++stats_.delivered;
    mischief::log::debug("menu: command {:#06x} ({})", commandId, menuCommandName(item->command));
    // Menu_EnableAll(0); handler; Menu_EnableAll(1). The guard also runs when the handler leaves
    // with RestartToSignIn / QuitGame (the original's longjmp skipped it, but the menu is removed
    // and reinstalled on both paths, which resets g_menusEnabled anyway).
    struct Guard {
        MenuBar& m;
        ~Guard() {
            m.inHandler_ = false;
            m.enableAll(true);
        }
    };
    enableAll(false);
    inHandler_ = true;
    Guard guard{*this};
    // A command may draw a dialog or a whole screen over the room: drop the debug treasure boxes
    // (the room loop puts them back on its next tick).
    engine_.presenter().setDebugMarks({});
    if (item->command != MenuCommand::None) {
        const Handler& h = handlers_[static_cast<size_t>(item->command)];
        if (h) h();
    }
    return true;
}

// --- tracking ---------------------------------------------------------------------------------------

bool MenuBar::open(int menu, bool dropDown, bool selectFirst) {
    if (!installed_ || !enabled_ || inHandler_) return false;
    if (menu < 0 || menu >= static_cast<int>(model_.menus.size())) return false;
    openMenu_ = menu;
    dropped_ = dropDown;
    item_ = (dropDown && selectFirst && !model_.menus[static_cast<size_t>(menu)].items.empty()) ? 0 : -1;
    if (!tracking_) startTracking();
    redraw();
    return true;
}

void MenuBar::close() {
    if (tracking_) stopTracking();
}

void MenuBar::startTracking() {
    tracking_ = true;
    mouseDown_ = false;
    releaseKeepsOpen_ = false;
}

void MenuBar::stopTracking() {
    tracking_ = false;
    openMenu_ = -1;
    dropped_ = false;
    item_ = -1;
    mouseDown_ = false;
    releaseKeepsOpen_ = false;
    redraw();
}

void MenuBar::choose(int menu, int item) {
    const auto& m = model_.menus[static_cast<size_t>(menu)];
    const uint16_t id = m.items[static_cast<size_t>(item)].commandId;
    stopTracking();
    postCommand(id);  // WM_COMMAND, dispatched by the next fetch
}

void MenuBar::onPump(PumpPhase phase) {
    if (phase == PumpPhase::Frozen) return;
    // The menu loop: the game stays frozen (and audio paused) while a menu is open.
    if (tracking_) engine_.holdWhile([this] { return tracking_; });
    if (phase == PumpPhase::Fetch && pending_) {
        const uint16_t id = *pending_;
        pending_.reset();
        dispatch(id);
    }
}

void MenuBar::moveItem(int delta) {
    const int n = static_cast<int>(model_.menus[static_cast<size_t>(openMenu_)].items.size());
    if (n == 0) return;
    if (item_ < 0)
        item_ = delta > 0 ? 0 : n - 1;
    else
        item_ = (item_ + delta + n) % n;
}

void MenuBar::moveMenu(int delta) {
    const int n = static_cast<int>(model_.menus.size());
    if (n == 0) return;
    openMenu_ = (openMenu_ + delta + n) % n;
    if (dropped_) item_ = model_.menus[static_cast<size_t>(openMenu_)].items.empty() ? -1 : 0;
}

int MenuBar::findMnemonic(int menu, uint8_t key) const {
    const auto& items = model_.menus[static_cast<size_t>(menu)].items;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].label.key == key) return static_cast<int>(i);
    // The shipped items have no '&': fall back to the first letter (comfort, as later Windows).
    for (size_t i = 0; i < items.size(); ++i)
        if (!items[i].label.text.empty() && upperAscii(items[i].label.text[0]) == key) return static_cast<int>(i);
    return -1;
}

// --- events -------------------------------------------------------------------------------------------

bool MenuBar::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
            altArmed_ = false;
            close();  // a menu loop ends when the window loses activation
            return false;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            close();
            return false;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: return handleKey(e);
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_MOUSE_WHEEL: return tracking_;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: return handleMouse(e);
        default: return false;
    }
}

bool MenuBar::handleKey(const SDL_Event& e) {
    if (!installed_) {
        altArmed_ = false;
        return false;
    }
    const SDL_Keycode key = e.key.key;
    const bool down = e.type == SDL_EVENT_KEY_DOWN;
    const bool alt = (e.key.mod & SDL_KMOD_ALT) != 0;
    const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
    const bool usable = enabled_ && !inHandler_;

    // A lone Alt (press and release, nothing in between) enters or leaves the bar, as Windows.
    if (key == SDLK_LALT || key == SDLK_RALT) {
        if (down) {
            if (!e.key.repeat) altArmed_ = !ctrl;  // AltGr (Ctrl+Alt) never arms it
        } else if (altArmed_) {
            altArmed_ = false;
            if (tracking_)
                close();
            else if (usable)
                open(0, false, false);
            return true;
        }
        return tracking_;
    }
    if (down) altArmed_ = false;

    if (!tracking_) {
        if (!down) return false;
        if (key == SDLK_F10 && !ctrl) {  // WM_SYSKEYDOWN: never seen by the game
            if (usable && !alt) open(0, false, false);
            return true;
        }
        if (alt && !ctrl) {
            const uint8_t k = (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') ? upperAscii(static_cast<uint8_t>(key)) : 0;
            if (k) {
                for (size_t m = 0; m < model_.menus.size(); ++m) {
                    if (model_.menus[m].title.key == k) {
                        if (usable) open(static_cast<int>(m), true, true);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // The menu loop has the keyboard.
    if (!down) return true;
    switch (key) {
        case SDLK_F12: return false;  // screenshots still work
        case SDLK_LEFT: moveMenu(-1); break;
        case SDLK_RIGHT: moveMenu(+1); break;
        case SDLK_UP:
            if (dropped_)
                moveItem(-1);
            else {
                dropped_ = true;
                item_ = -1;
                moveItem(-1);
            }
            break;
        case SDLK_DOWN:
            if (dropped_)
                moveItem(+1);
            else {
                dropped_ = true;
                item_ = -1;
                moveItem(+1);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (dropped_ && item_ >= 0) {
                choose(openMenu_, item_);
                return true;
            }
            if (!dropped_) {
                dropped_ = true;
                item_ = -1;
                moveItem(+1);
            }
            break;
        case SDLK_ESCAPE:
            if (dropped_) {
                dropped_ = false;  // back to the highlighted title
                item_ = -1;
            } else {
                close();
                return true;
            }
            break;
        case SDLK_F10:
            close();
            return true;
        default: {
            const uint8_t k = (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') ? upperAscii(static_cast<uint8_t>(key)) : 0;
            if (!k) break;
            if (dropped_ && !alt) {
                const int i = findMnemonic(openMenu_, k);
                if (i >= 0) {
                    choose(openMenu_, i);
                    return true;
                }
                break;
            }
            for (size_t m = 0; m < model_.menus.size(); ++m) {
                if (model_.menus[m].title.key == k) {
                    openMenu_ = static_cast<int>(m);
                    dropped_ = true;
                    item_ = model_.menus[m].items.empty() ? -1 : 0;
                    break;
                }
            }
            break;
        }
    }
    redraw();
    return true;
}

bool MenuBar::canvasPos(const SDL_Event& e, float wx, float wy, int& cx, int& cy) const {
    float fx = wx, fy = wy;
    SDL_WindowID wid = 0;
    if (e.type == SDL_EVENT_MOUSE_MOTION)
        wid = e.motion.windowID;
    else
        wid = e.button.windowID;
    if (wid != 0) {
        // Window -> 640x480 canvas through the renderer's logical presentation.
        SDL_Renderer* r = engine_.input().translator().renderer();
        if (!r || !SDL_RenderCoordinatesFromWindow(r, wx, wy, &fx, &fy)) return false;
    }
    cx = static_cast<int>(std::floor(fx));
    cy = static_cast<int>(std::floor(fy));
    return true;
}

int MenuBar::titleAt(int cx, int cy) const {
    for (size_t i = 0; i < titles_.size(); ++i)
        if (titles_[i].contains(cx, cy)) return static_cast<int>(i);
    return -1;
}

int MenuBar::itemAt(int menu, int cx, int cy) const {
    if (menu < 0) return -1;
    const int n = static_cast<int>(model_.menus[static_cast<size_t>(menu)].items.size());
    for (int i = 0; i < n; ++i)
        if (itemRect(menu, i).contains(cx, cy)) return i;
    return -1;
}

bool MenuBar::handleMouse(const SDL_Event& e) {
    if (!installed_) return false;
    int cx = 0, cy = 0;
    const bool motion = e.type == SDL_EVENT_MOUSE_MOTION;
    if (!canvasPos(e, motion ? e.motion.x : e.button.x, motion ? e.motion.y : e.button.y, cx, cy)) return tracking_;
    const bool inBar = cy >= 0 && cy < kBarHeight && cx >= 0 && cx < kCanvasW;
    const bool left = !motion && e.button.button == SDL_BUTTON_LEFT;

    if (!tracking_) {
        if (motion) return false;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && left && inBar) {
            const int m = titleAt(cx, cy);
            if (m >= 0 && enabled_ && !inHandler_ && open(m, true, false)) {
                mouseDown_ = true;
                releaseKeepsOpen_ = true;
            }
        }
        return inBar;  // the bar is the non-client area: the game never sees clicks there
    }

    if (motion) {
        const int m = titleAt(cx, cy);
        if (m >= 0 && m != openMenu_ && (dropped_ || mouseDown_)) {
            openMenu_ = m;
            dropped_ = true;
            item_ = -1;
        } else if (dropped_) {
            const int i = itemAt(openMenu_, cx, cy);
            if (i >= 0)
                item_ = i;
            else if (mouseDown_)
                item_ = -1;
        }
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int m = titleAt(cx, cy);
        if (m >= 0) {
            if (m == openMenu_ && dropped_) {
                close();  // a second click on the open title closes it
                return true;
            }
            openMenu_ = m;
            dropped_ = true;
            item_ = -1;
            mouseDown_ = true;
            releaseKeepsOpen_ = true;
        } else if (dropped_ && dropDownRect(openMenu_).contains(cx, cy)) {
            mouseDown_ = true;
            releaseKeepsOpen_ = false;
            item_ = itemAt(openMenu_, cx, cy);
        } else {
            close();  // a click outside cancels the menu (and is swallowed)
            return true;
        }
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (!left) return true;
        mouseDown_ = false;
        const bool keep = releaseKeepsOpen_;
        releaseKeepsOpen_ = false;
        if (dropped_) {
            const int i = itemAt(openMenu_, cx, cy);
            if (i >= 0) {
                choose(openMenu_, i);
                return true;
            }
            if (titleAt(cx, cy) >= 0 && keep) {
                // Released on the title that opened the drop-down: it stays open.
            } else if (!dropDownRect(openMenu_).contains(cx, cy)) {
                close();
                return true;
            }
        }
    }
    redraw();
    return true;
}

// --- drawing ------------------------------------------------------------------------------------------

void MenuBar::fill(const Rect& r0, uint32_t colour) {
    Rect r = r0;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > kCanvasW) r.w = kCanvasW - r.x;
    if (r.y + r.h > kCanvasH) r.h = kCanvasH - r.y;
    if (r.empty()) return;
    auto px = engine_.presenter().overlayPixels();
    for (int y = r.y; y < r.y + r.h; ++y)
        std::fill_n(px.begin() + static_cast<ptrdiff_t>(y) * kCanvasW + r.x, r.w, colour);
}

int MenuBar::drawLabel(int x, int y, const MenuLabel& l, uint32_t colour) {
    auto px = engine_.presenter().overlayPixels();
    const int h = font_.height();
    for (size_t i = 0; i < l.text.size(); ++i) {
        const uint8_t c = l.text[i];
        const int w = font_.charWidth(c);
        for (int gy = 0; gy < h; ++gy) {
            const int py = y + gy;
            if (py < 0 || py >= kCanvasH) continue;
            for (int gx = 0; gx < w; ++gx) {
                const int pxx = x + gx;
                if (pxx < 0 || pxx >= kCanvasW) continue;
                if (font_.pixel(c, gx, gy)) px[static_cast<size_t>(py) * kCanvasW + pxx] = colour;
            }
        }
        if (static_cast<int>(i) == l.mnemonicIndex) {
            const int uy = y + std::min(baseline_ + 2, h - 1);  // one blank row under the glyph
            fill({x, uy, std::max(1, w - 1), 1}, colour);
        }
        x += w;
    }
    return x;
}

void MenuBar::redraw() {
    mischief::gfx::Presenter& pr = engine_.presenter();
    if (!installed_) {
        if (!drawn_.empty() || pr.hasOverlay()) pr.clearOverlay();
        drawn_ = {};
        return;
    }
    Rect region{0, 0, kCanvasW, kBarHeight};
    Rect box{};
    if (tracking_ && dropped_ && openMenu_ >= 0) {
        box = dropDownRect(openMenu_);
        region = unite(region, {box.x, box.y, box.w + kShadow, box.h + kShadow});
    }
    // Clear what was drawn before (outside the new region it becomes transparent again).
    fill(drawn_, 0u);

    // The bar: white with a black bottom line; titles grey while the menus are disabled.
    fill({0, 0, kCanvasW, kBarHeight - 1}, kMenuBg);
    fill({0, kBarHeight - 1, kCanvasW, 1}, kFrame);
    const int textY = (kBarHeight - 1 - font_.height()) / 2;
    for (size_t m = 0; m < model_.menus.size(); ++m) {
        const Rect& t = titles_[m];
        const bool hot = tracking_ && openMenu_ == static_cast<int>(m);
        if (hot) fill(t, kHighlightBg);
        drawLabel(t.x + kTitlePad, textY, model_.menus[m].title, hot ? kHighlightText : (enabled_ ? kMenuText : kGrayText));
    }

    // The drop-down: shadow, white box with a black frame, highlighted item.
    if (!box.empty()) {
        fill({box.x + kShadow, box.y + kShadow, box.w, box.h}, kShadowColour);
        fill(box, kFrame);
        fill({box.x + 1, box.y + 1, box.w - 2, box.h - 2}, kMenuBg);
        const auto& items = model_.menus[static_cast<size_t>(openMenu_)].items;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect r = itemRect(openMenu_, static_cast<int>(i));
            const bool sel = static_cast<int>(i) == item_;
            if (sel) fill(r, kHighlightBg);
            drawLabel(r.x + kItemPadLeft - 1, r.y + kItemPadV, items[i].label, sel ? kHighlightText : kMenuText);
        }
    }
    const Rect changed = unite(drawn_, region);
    pr.overlayChanged(changed.x, changed.y, changed.w, changed.h);
    drawn_ = region;
}

}  // namespace tmport::ui
