#pragma once
// In-engine menu bar: replaces the game's Win16 menu (segment 1078 "menu", 1120 GameMenu, 1140
// handlers; re/docs/code_1058-1090.md §2.7/§7, code_10f8-1148.md §3.8/§9/§13, ARCHITECTURE.md
// §2.7/§3.9, PORTING_NOTES.md §3/§4.11).
//
// Model: built from the user's TMTWINCD.EXE (MenuBarDef DS:13bf -> MenuPopupDef[3] at DS:1383,
// titles and '\n'-separated item strings DS:1311..137f, far handler pointers). Command id =
// menu * 0x100 + item (1078:0000). Each handler pointer is mapped to the engine command it stands
// for (MenuCommand) by its address.
//
// Behaviour kept from the original:
//  * the bar exists only while a climb is played (install / remove = 1120:0000 / 000d);
//  * Menu_EnableAll (1078:01b5) greys every menu during modal sequences; it is a no-op while no
//    menu is installed, and Menu_IsEnabled (1078:01b1) is false then;
//  * while a menu is open the game is frozen and audio is paused (WM_ENTERMENULOOP ->
//    App_Deactivate, WM_EXITMENULOOP -> App_Activate): the menu runs inside Engine::holdWhile;
//  * Menu_OnCommand (1078:0135): ignored while a resource load pumps messages
//    (g_inResourceLoad DS:09d4); otherwise menus off, handler, menus on;
//  * commands are delivered only from inside an input fetch (where the original dispatched
//    WM_COMMAND), never re-entrantly (a command arriving while a handler runs is dropped) and
//    never while the menu is disabled or removed (port rule; the original could not get there).
//
// Look (the original's was the Windows 3.1 system menu, PORTING_NOTES.md open question 4): a white
// Win3.1-style bar across the top of the 640x480 canvas (rows 0..kBarHeight-1, above the game area
// at y 48) and drop-down boxes, all drawn with the game font (FFNT 999) into the presenter overlay.
//
// Input (Engine::setEventFilter): mouse clicks on the bar, Alt+<mnemonic> (Alt+F/O/H), F10 or a
// lone Alt; while open: Left/Right (other menu), Up/Down (item), Enter (choose), Esc (close the
// drop-down, then leave), a letter (item mnemonic or first letter), mouse move/click.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

union SDL_Event;

namespace tmport {
class Engine;
class ExeTables;
enum class PumpPhase : uint8_t;
namespace text {
class Font;
}
}  // namespace tmport

namespace tmport::ui {

// What a menu item does, identified from its handler's address in the EXE.
enum class MenuCommand : uint8_t {
    None,               // NULL handler / unknown address: the item does nothing
    SeeRank,            // 10f0:0487 Menu_SeeRank
    StartAnotherGame,   // 1140:0062 MenuCmd_StartAnotherGame
    Exit,               // 1140:0098 MenuCmd_Exit
    NewPlayerMessages,  // 1140:0015 MenuCmd_NewPlayerMessages
    MusicAndSound,      // 1140:00cc MenuCmd_MusicAndSound
    Voice,              // 1140:00f7 MenuCmd_Voice
    HelpAboutGame,      // 1180:0117 Help topic 0
    HelpKeyboard,       // 1180:0128 Help topic 1
    HelpMouse,          // 1180:0139 Help topic 2
    HelpEducation,      // 1180:014a Help topic 3
    AboutTM,            // 1140:0000 MenuCmd_AboutTM
    Count
};
const char* menuCommandName(MenuCommand c);
// The command a handler at selector:offset (Ghidra numbering) stands for.
MenuCommand menuCommandForHandler(uint16_t selector, uint16_t offset);

// A label with an optional '&' mnemonic ("&File": text "File", mnemonic index 0, key 'F').
struct MenuLabel {
    std::vector<uint8_t> text;  // raw bytes as in the EXE, without the '&' markers
    int mnemonicIndex = -1;     // index into text of the underlined character, -1 = none
    uint8_t key = 0;            // upper-case mnemonic letter/digit, 0 = none

    static MenuLabel parse(std::span<const uint8_t> raw);
    std::string str() const { return std::string(text.begin(), text.end()); }
};

struct MenuItemDef {
    MenuLabel label;
    uint16_t commandId = 0;               // menu * 0x100 + item
    MenuCommand command = MenuCommand::None;
    uint16_t handlerSelector = 0, handlerOffset = 0;  // 0:0 = NULL
};

struct MenuDef {
    MenuLabel title;
    std::vector<MenuItemDef> items;
};

struct MenuBarModel {
    std::vector<MenuDef> menus;

    // Decodes the MenuBarDef at `off` (default DS:13bf). Throws FormatError on bad data.
    static MenuBarModel fromExe(const ExeTables& exe, uint16_t off);
    static MenuBarModel fromExe(const ExeTables& exe);
    // The item with this command id, or nullptr.
    const MenuItemDef* find(uint16_t commandId) const;
};

class MenuBar {
public:
    // Geometry in the 640x480 canvas.
    static constexpr int kBarHeight = 22;  // rows 0..21 (row 21 is the bottom line)
    static constexpr int kBarLeft = 4;     // first title cell
    static constexpr int kTitlePad = 8;    // padding left and right of a title
    static constexpr int kItemPadLeft = 16, kItemPadRight = 20, kItemPadV = 1;
    static constexpr int kShadow = 2;

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
        bool empty() const { return w <= 0 || h <= 0; }
    };

    // Installs the event filter and a pump hook on the engine (removed by the destructor). The
    // menu starts removed (not visible). The font must outlive the menu bar.
    MenuBar(Engine& engine, MenuBarModel model, const text::Font& font);
    ~MenuBar();
    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    const MenuBarModel& model() const { return model_; }

    // --- 1120 / 1078 API ----------------------------------------------------------------------------
    void install();                 // 1120:0000 GameMenu_Install: visible, enabled
    void remove();                  // 1120:000d GameMenu_Remove: closes, hides, disabled
    bool installed() const { return installed_; }
    bool isEnabled() const { return enabled_; }  // 1078:01b1 Menu_IsEnabled
    void enableAll(bool enable);    // 1078:01b5 Menu_EnableAll (no-op while removed)

    // RAII for the original's `was = Menu_IsEnabled(); Menu_EnableAll(0); ...; Menu_EnableAll(was)`.
    class DisableScope {
    public:
        explicit DisableScope(MenuBar* m) : m_(m), was_(m && m->isEnabled()) { if (m_) m_->enableAll(false); }
        ~DisableScope() { if (m_) m_->enableAll(was_); }
        DisableScope(const DisableScope&) = delete;
        DisableScope& operator=(const DisableScope&) = delete;
    private:
        MenuBar* m_;
        bool was_;
    };

    // g_inResourceLoad (DS:09d4): set around message pumping during resource loads; commands
    // arriving meanwhile are dropped (1078:0135).
    void setInResourceLoad(bool on) { inResourceLoad_ = on; }
    bool inResourceLoad() const { return inResourceLoad_; }
    class ResourceLoadScope {
    public:
        explicit ResourceLoadScope(MenuBar* m) : m_(m), was_(m && m->inResourceLoad()) { if (m_) m_->setInResourceLoad(true); }
        ~ResourceLoadScope() { if (m_) m_->setInResourceLoad(was_); }
        ResourceLoadScope(const ResourceLoadScope&) = delete;
        ResourceLoadScope& operator=(const ResourceLoadScope&) = delete;
    private:
        MenuBar* m_;
        bool was_;
    };

    // --- commands -----------------------------------------------------------------------------------
    using Handler = std::function<void()>;
    void setHandler(MenuCommand c, Handler h);
    bool hasHandler(MenuCommand c) const;
    // Queues a WM_COMMAND (also what choosing an item does); it is delivered by the next input
    // fetch (Engine::getInputCode / peekInputCode / flushInput).
    void postCommand(uint16_t commandId);
    bool commandPending() const { return pending_.has_value(); }
    // 1078:0135 Menu_OnCommand with the port's rules. Returns true if the command was accepted
    // (a handler may still be missing: the item then does nothing). Dropped (false): during a
    // resource load, while a handler runs (re-entrancy), menu removed or disabled, unknown id.
    bool dispatch(uint16_t commandId);
    bool inHandler() const { return inHandler_; }
    // Statistics for tests and logs.
    struct Stats {
        int delivered = 0, droppedLoad = 0, droppedReentrant = 0, droppedDisabled = 0, droppedUnknown = 0;
    };
    const Stats& stats() const { return stats_; }

    // --- interaction state --------------------------------------------------------------------------
    bool tracking() const { return tracking_; }  // the menu loop is active (bar or drop-down)
    int openMenu() const { return openMenu_; }   // highlighted title, -1 = none
    bool dropped() const { return dropped_; }    // the drop-down of openMenu() is shown
    int selectedItem() const { return item_; }   // -1 = none
    // Starts the menu loop on menu `menu` (dropDown: show its items, first item selected when
    // selectFirst). False if removed, disabled or a handler runs.
    bool open(int menu, bool dropDown, bool selectFirst);
    void close();  // leaves the menu loop without a command

    // The event filter (installed on the engine; public for tests). True = consumed.
    bool handleEvent(const SDL_Event& e);
    // The pump hook (installed on the engine; public for tests): Busy / Fetch run the menu loop
    // (Engine::holdWhile) while tracking; Fetch then delivers a pending command.
    void onPump(PumpPhase phase);

    // --- geometry / drawing -------------------------------------------------------------------------
    Rect titleRect(int menu) const;
    Rect dropDownRect(int menu) const;  // the box without its shadow
    Rect itemRect(int menu, int item) const;
    int fontHeight() const;
    // Redraws the overlay (normally automatic after every state change).
    void redraw();

private:
    void layout();
    void startTracking();
    void stopTracking();
    void choose(int menu, int item);
    bool handleKey(const SDL_Event& e);
    bool handleMouse(const SDL_Event& e);
    bool canvasPos(const SDL_Event& e, float wx, float wy, int& cx, int& cy) const;
    int titleAt(int cx, int cy) const;
    int itemAt(int menu, int cx, int cy) const;
    int findMnemonic(int menu, uint8_t key) const;
    void moveItem(int delta);
    void moveMenu(int delta);
    // Drawing into the presenter overlay.
    void fill(const Rect& r, uint32_t rgba);
    int drawLabel(int x, int y, const MenuLabel& l, uint32_t rgba);
    int labelWidth(const MenuLabel& l) const;

    Engine& engine_;
    MenuBarModel model_;
    const text::Font& font_;
    int pumpHookId_ = 0;

    bool installed_ = false;
    bool enabled_ = false;  // g_menusEnabled
    bool inResourceLoad_ = false;
    bool inHandler_ = false;
    std::array<Handler, static_cast<size_t>(MenuCommand::Count)> handlers_{};
    std::optional<uint16_t> pending_;
    Stats stats_;

    bool tracking_ = false;
    int openMenu_ = -1;
    bool dropped_ = false;
    int item_ = -1;
    bool mouseDown_ = false;        // left button held since it opened / moved in the menu
    bool releaseKeepsOpen_ = false; // the press that opened a drop-down; its release keeps it open
    bool altArmed_ = false;         // a lone Alt press toggles the bar

    std::vector<Rect> titles_;      // per menu, in canvas coordinates
    std::vector<int> dropWidth_;    // per menu
    int itemHeight_ = 0;
    int baseline_ = 0;              // lowest ink row of a capital in the font cell (mnemonic underline: +2)
    Rect drawn_;                    // overlay region drawn last time (bar + drop-down + shadow)
};

}  // namespace tmport::ui
