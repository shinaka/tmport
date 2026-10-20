#pragma once
// The scrolling text viewer (ovl34 4140; re/gg/docs/code_ui.md §8): the puzzles' instructions behind
// their Hint button (Puzzle_ShowHintText 38e2:0286). A 129 x 138 panel at (0, 28) with the text in
// the small font, an up and a down arrow, and for long texts a scroll bar (track (118, 45) 9 x 104,
// an 11-pixel thumb that moves in steps of 104 / (lines - 10)). Its own text layout, not
// Text_Render: x from 3, y from 31, a word wraps when x + its width >= 116 unless it is 108 wide or
// more; escapes are skipped; 13 lines (y 31..151) are visible.
//
// The input loop (TextView_Run 4140:078d: the arrow repeat on timer slot 6, the thumb drag with the
// pointer warped by INT 33h fn 4, the close on Esc or a click outside) is the game's (wave 3); this
// class is its state and drawing, with the zones it adds.

#include <cstdint>
#include <span>
#include <vector>

#include "gg/data/resources.h"
#include "gg/input/zones.h"
#include "gg/ui/ui_canvas.h"

namespace tmport::gg {

class Archive;
class GgExe;

inline constexpr uint16_t kTextViewPanel = 0x11D4;   // the 129 x 138 panel (keyed at (0, 28))
inline constexpr uint16_t kTextViewArrows = 0x11D5;  // 0 up, 1 down, 2 up pressed, 3 down pressed

// Input codes of the viewer's zones (code_ui.md §8.3).
inline constexpr uint16_t kViewUpPress = 0x051A, kViewUpRelease = 0x051B;
inline constexpr uint16_t kViewDownPress = 0x051C, kViewDownRelease = 0x051D;
inline constexpr uint16_t kViewText = 0x051E, kViewClose = 0x051F;
inline constexpr uint16_t kViewPageUp = 0x0520, kViewThumb = 0x0521, kViewPageDown = 0x0522;

// The layout of 4140 with the selected font's metrics (Font_CharWidth, Font_GetHeight).
struct TextViewLayout {
    static constexpr int kLeft = 3, kTop = 31, kRight = 116, kBottom = 163, kLongWord = 108;

    // 4140:01ff TextView_WordWidth: the sum of Font_CharWidth up to a space, TAB, CR, LF or NUL; FF
    // escapes (to the next FF) are skipped.
    static int wordWidth(const Fonts& fonts, std::span<const uint8_t> text, size_t i);
    // 4140:01bc TextView_GlyphVisible: x >= 3, y >= 31, x + Font_CharWidth(c) < 116, y + line height
    // - 1 < 163.
    static bool glyphVisible(const Fonts& fonts, int x, int y, uint8_t c);
    // 4140:03d7 TextView_CountLines: the line breaks of the layout (lines - 1). Unlike the drawing it
    // advances x for every glyph, visible or not.
    static int countLines(const Fonts& fonts, std::span<const uint8_t> text);
    // 4140:027a TextView_DrawText(text, first): lines before `first` are laid out at y 31 without
    // drawing; an invisible glyph does not advance x.
    static void drawText(Screen& screen, const Fonts& fonts, std::span<const uint8_t> text, int first);
};

class TextViewer {
public:
    // The arrows (Res_LoadCopy(0x11D5)), the panel (Res_Read(0x11D4)) and the seven zones DS:4060..40e4
    // of ssg.exe. `text` is the caller's (the puzzle's instruction text) and stays alive.
    TextViewer(const GgExe& exe, const Archive& archive, std::span<const uint8_t> text);
    // Its zones may be linked into a ClickZones list: it stays where it is.
    TextViewer(const TextViewer&) = delete;
    TextViewer& operator=(const TextViewer&) = delete;

    // 4140:0667 TextView_Run's set-up after the caller's g_menuHandler save, Zones_Push,
    // Mouse_ForceDefault(1) and Mouse_SetHotSpot(NULL, 0, 0): Gfx_GetImage(0, 28, 129, 138) (the
    // save-under), g_drawColor 0, Font_Select(1), Font_SetColor(0), the zones (close, text, up, down,
    // and the bar's three when scrollable) when `zones` is given, scroll max = max(1, CountLines - 10),
    // thumb step = 104 / max, thumb y 45, line 0, Redraw.
    void open(const UiCanvas& ui, ClickZones* zones);
    // 4140:0605 TextView_Redraw(arrows, text, up, down, line): the panel keyed at (0, 28), the arrows
    // (pressed or not) at (117, 29) and (117, 149), the text from `line()`, the bar when scrollable,
    // Dirty_Flush.
    void redraw(const UiCanvas& ui, bool upPressed = false, bool downPressed = false);
    // 4140:04e5 TextView_DrawArrow (no flush) and 4140:056b TextView_FlashArrow's drawing.
    void drawArrow(const UiCanvas& ui, bool down, bool pressed);
    // The exit's drawing: Gfx_PutImage(0, 28, saved), Dirty_Flush (the caller then Zones_ClearLocal /
    // Mouse_ForceDefault(0) / Zones_Pop / the hook).
    void close(const UiCanvas& ui);

    // 4140:063d TextView_ScrollBy: line += delta, clamped to 0..scrollMax - 1.
    void scrollBy(int delta);
    // 4140:00b1 / 00d3 TextView_ThumbDown / ThumbUp: thumb y +/- step, snapping to 138 / 45 within one
    // step of the end. The thumb and the line move independently (they can disagree near the ends).
    void thumbDown();
    void thumbUp();
    // 4140:0081 TextView_SetThumbY: clamped to 45..138.
    void setThumbY(int y);
    // 4140:0102 TextView_UpdateBarZones: the page-up zone (h = thumb y - 45, only when the thumb is not
    // at the top), the page-down zone (y = thumb y + 11, h = 138 - thumb y, only when not at the
    // bottom), the thumb zone (y = thumb y).
    void updateBarZones(ClickZones& zones);

    int line() const { return line_; }
    int scrollMax() const { return scrollMax_; }
    int thumbStep() const { return thumbStep_; }
    int thumbY() const { return thumbY_; }
    bool scrollable() const { return scrollMax_ != 1; }
    const SavedImage& saved() const { return saved_; }

private:
    void drawPanel(const UiCanvas& ui, bool upPressed, bool downPressed);
    void drawBar(const UiCanvas& ui);

    std::span<const uint8_t> text_;
    Bundle arrows_;
    std::vector<uint8_t> panel_;
    SavedImage saved_;
    Zone upZone_, downZone_, textZone_, closeZone_, pageUpZone_, thumbZone_, pageDownZone_;
    ClickZones* zones_ = nullptr;
    int line_ = 0;
    int scrollMax_ = 1;  // g_viewScrollMax DS:bc78
    int thumbStep_ = 0;  // g_viewThumbStep DS:bc74
    int thumbY_ = 45;    // g_viewThumbY DS:bc76
};

}  // namespace tmport::gg
