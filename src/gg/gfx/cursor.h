#pragma once
// The software mouse cursor of segment 17ab (re/gg/docs/code_runtime.md §7.1, §7.3, §11.4, §12).
//
// The cursor is drawn into the RAM screen by Dirty_Flush (Mouse_DrawCursor, with the background
// under it saved first) and removed again by the first primitive of the next frame
// (Gfx_HideMouseOnce -> Mouse_RestoreUnder). Which picture is shown depends on the pointer: the
// first cursor region (a click-zone shape with a cursor record) that contains the pointer's
// top-left, the default full-screen region last; Mouse_ForceDefault selects a fixed region instead
// (dialogs, menus, hints). The active record is copied into DS:7a6a when it changes; the copy holds
// the hot spot the mouse handler adds to clicks, the size Mouse_SetBounds keeps on screen, the
// save-under position and buffer and the shown flag.
//
// Cursor is a plain state machine over a Screen: Dirty_Flush calls draw(), the primitives call
// restoreUnder() through Screen::hideMouseOnce, the mouse handler (17a0, the runtime) reads x(), y(),
// hotX(), hotY() and moves the pointer with Video::moveCursor. The INT 33h calls of Mouse_SetBounds
// go to CursorDriverHooks (the runtime's dos::Int33Mouse).

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "gg/gfx/screen.h"

namespace tmport::gg {

// A cursor record (24 bytes, Mouse_LoadCursors 17ab:0083).
struct CursorRecord {
    int resourceId = -1;       // +00: resource id, -1 when built from a picture pointer
    int saveX = 0, saveY = 0;  // +02/+04: save-under position (meaningful in the active copy)
    int width = 0, height = 0; // +06/+08: from the picture header
    int hotX = 0, hotY = 0;    // +0A/+0C: hot spot (0 at creation; Mouse_SetHotSpot)
    std::span<const uint8_t> picture;  // +0E: LPX payload
    int shown = 0;             // +12: shown flag (meaningful in the active copy)
    std::shared_ptr<SavedImage> saveUnder;  // +14: w * h + 4 bytes (the active copy shares it)
    // The private copy of the picture when loaded by id (Res_LoadCopy); `picture` points into it.
    // Shared so that the active copy outlives a record freed while it is active.
    std::shared_ptr<const std::vector<uint8_t>> ownedPicture;

    // 17ab:0083 Mouse_LoadCursors(id): the record of picture resource `id` (its payload, copied).
    static std::unique_ptr<CursorRecord> fromResource(uint16_t id, std::vector<uint8_t> payload);
    // 17ab:0083 Mouse_LoadCursors(far picture): a record over a picture the caller keeps alive.
    static std::unique_ptr<CursorRecord> fromPicture(std::span<const uint8_t> picture);
};

// A cursor region (28 bytes: list links, an embedded click zone at +8, a far record at +0x18).
struct CursorRegion {
    int shape = 0;     // zone +00: 0 rectangle, else circle (x, y = centre, w = radius)
    int x = 0, y = 0;  // zone +02/+04
    int w = 0, h = 0;  // zone +06/+08
    int priority = 0;  // zone +0A: must be >= 0 to match
    CursorRecord* record = nullptr;

    // 17ab:0507 Zones_HitTest on this one zone: rectangle half-open and signed; circle r^2 > dx^2 +
    // dy^2 with r^2 the 16-bit IMUL of w (strict: the rim is outside); priority >= 0.
    bool contains(int px, int py) const;
};

// The INT 33h calls of Mouse_SetBounds (driver units: x doubled in mode 13h).
struct CursorDriverHooks {
    std::function<void(int minX, int maxX)> setRangeX;  // AX=07h: CX = x (not doubled), DX = 2 * maxX
    std::function<void(int minY, int maxY)> setRangeY;  // AX=08h: CX = y, DX = maxY
};

class Cursor final : public MouseUnder {
public:
    // Registers itself as the screen's MouseUnder.
    explicit Cursor(Screen& screen);
    ~Cursor() override;
    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    // 17ab:000e Mouse_Init: shown = 0; the arrow (picture resource 0) becomes the record of the
    // default and the forced region, hot spot (0, 0); the pointer = driver position (x / 2, y) of
    // INT 33h AX=03h; the region list = the default region only. The two regions' zones are DS:0a5c
    // and DS:0a78 of ssg.exe (Video::init reads them).
    void init(std::unique_ptr<CursorRecord> arrow, int driverX, int driverY, const CursorRegion& defaultZone,
              const CursorRegion& forcedZone);
    void setDriverHooks(CursorDriverHooks hooks) { hooks_ = std::move(hooks); }
    // Dirty_Flush; Mouse_SetCursorPicture flushes after changing the default record (Video sets it).
    void setFlush(std::function<void()> flush) { flush_ = std::move(flush); }

    // g_mouseX DS:7a88 / g_mouseY DS:7a86: the cursor picture's top-left.
    int x() const { return mouseX_; }
    int y() const { return mouseY_; }
    void setPosition(int x, int y) {
        mouseX_ = x;
        mouseY_ = y;
    }
    // g_mouseMaxX DS:7a64 / g_mouseMaxY DS:7a62.
    int maxX() const { return maxX_; }
    int maxY() const { return maxY_; }

    // 17ab:017f Mouse_SetBounds(x, y, w, h): maxX = x + w - cursor width, maxY = y + h - cursor
    // height; the pointer is clamped to them (no lower clamp); INT 33h 07h (x, 2 * maxX) and 08h
    // (y, maxY).
    void setBounds(int x, int y, int w, int h);
    // 17ab:01da Mouse_SetBoundsFull: Mouse_SetBounds(0, 0, 319, 199).
    void setBoundsFull() { setBounds(0, 0, 319, 199); }
    // 17ab:01f0 Mouse_SetCursorPicture(rec): Mouse_RestoreUnder, the default region's record = rec,
    // Dirty_Flush; returns the previous record.
    CursorRecord* setCursorPicture(CursorRecord* rec);
    // 17ab:0222 Mouse_SetHotSpot(rec or nullptr = the active copy, hx, hy): values >= 0 are stored.
    void setHotSpot(CursorRecord* rec, int hx, int hy);
    // 17ab:0263 Mouse_RestoreDefaultCursor: Mouse_SetCursorPicture(arrow) and Mouse_FreeCursor of the
    // record that was active. Returns that record for the caller to free (nullptr when it is the
    // arrow or none); the active copy keeps its picture and buffer alive meanwhile.
    CursorRecord* restoreDefaultCursor();

    // 17ab:0754 MouseRegion_Add (in front: the newest region is tested first); 17ab:0771
    // MouseRegion_AddArray; 17ab:07a4 MouseRegion_Clear (every region but the last, the default).
    // The regions are the caller's; they must stay alive while listed.
    void addRegion(CursorRegion* region);
    void addRegions(std::span<CursorRegion> regions);
    void clearRegions();
    size_t regionCount() const { return regions_.size(); }
    CursorRegion& defaultRegion() { return defaultRegion_; }
    CursorRegion& forcedRegion() { return forcedRegion_; }
    // 17ab:07dd Mouse_ForceDefault(on): DS:0a52 = on; returns the previous value.
    int forceDefault(int on);
    // 17ab:0885 Mouse_Show / 17ab:0890 Mouse_Hide (DS:0a30): Mouse_DrawCursor draws nothing while
    // hidden; a shown cursor stays until the next drawing call removes it.
    void show() { hidden_ = 0; }
    void hide() { hidden_ = 1; }
    bool hidden() const { return hidden_ != 0; }

    // 17ab:0612 Mouse_DrawCursor (called only by Dirty_Flush).
    void draw();
    // 17ab:072a Mouse_RestoreUnder: if shown: shown = 0; Gfx_PutImage(saved x, saved y, save buffer).
    void restoreUnder() override;

    // The active copy DS:7a6a.
    const CursorRecord& active() const { return active_; }
    int hotX() const { return active_.hotX; }
    int hotY() const { return active_.hotY; }
    bool shown() const { return active_.shown != 0; }
    // DS:7a4a: the record the active copy was taken from; DS:7a82: the arrow.
    const CursorRecord* activeRecord() const { return activeRecord_; }
    CursorRecord* arrow() const { return arrow_.get(); }

private:
    Screen& screen_;
    CursorDriverHooks hooks_;
    std::function<void()> flush_;
    std::unique_ptr<CursorRecord> arrow_;
    CursorRecord active_;
    CursorRecord* activeRecord_ = nullptr;
    CursorRegion defaultRegion_;
    CursorRegion forcedRegion_;
    std::vector<CursorRegion*> regions_;  // newest first; the default region last
    int mouseX_ = 0, mouseY_ = 0;
    int maxX_ = 0, maxY_ = 0;
    int forced_ = 0;
    int hidden_ = 0;
};

}  // namespace tmport::gg
