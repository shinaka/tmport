#include "gg/gfx/cursor.h"

#include <algorithm>

#include "util/log.h"

namespace tmport::gg {

namespace {

// The picture's width and height (LPX +6/+8); 0 for a payload shorter than the header.
void sizeFromPicture(CursorRecord& r) {
    const auto& p = r.picture;
    r.width = p.size() >= 10 ? (p[6] | (p[7] << 8)) : 0;
    r.height = p.size() >= 10 ? (p[8] | (p[9] << 8)) : 0;
}

}  // namespace

// 17ab:0083 Mouse_LoadCursors (an id: Res_LoadCopy)
std::unique_ptr<CursorRecord> CursorRecord::fromResource(uint16_t id, std::vector<uint8_t> payload) {
    auto r = std::make_unique<CursorRecord>();
    r->resourceId = id;
    r->ownedPicture = std::make_shared<const std::vector<uint8_t>>(std::move(payload));
    r->picture = *r->ownedPicture;
    sizeFromPicture(*r);
    r->saveUnder = std::make_shared<SavedImage>();  // Mem_Alloc(w * h + 4), contents not initialised
    return r;
}

// 17ab:0083 Mouse_LoadCursors (a far picture)
std::unique_ptr<CursorRecord> CursorRecord::fromPicture(std::span<const uint8_t> picture) {
    auto r = std::make_unique<CursorRecord>();
    r->resourceId = -1;
    r->picture = picture;
    sizeFromPicture(*r);
    r->saveUnder = std::make_shared<SavedImage>();
    return r;
}

// 17ab:0507 Zones_HitTest on one zone
bool CursorRegion::contains(int px, int py) const {
    bool hit;
    if (shape == 0) {
        hit = px >= x && x + w > px && py >= y && y + h > py;
    } else {
        const auto r2 = static_cast<int32_t>(static_cast<int16_t>(w * w));  // 16-bit IMUL, sign-extended
        const int32_t dx = px - x, dy = py - y;
        hit = r2 > dx * dx + dy * dy;
    }
    return hit && priority > -1;
}

Cursor::Cursor(Screen& screen) : screen_(screen) { screen_.setMouseUnder(this); }

Cursor::~Cursor() { screen_.setMouseUnder(nullptr); }

// 17ab:000e Mouse_Init
void Cursor::init(std::unique_ptr<CursorRecord> arrow, int driverX, int driverY, const CursorRegion& defaultZone,
                  const CursorRegion& forcedZone) {
    active_.shown = 0;
    arrow_ = std::move(arrow);
    mouseX_ = static_cast<uint16_t>(driverX) >> 1;  // SHR AX,1 of the driver's CX
    mouseY_ = driverY;
    defaultRegion_ = defaultZone;
    forcedRegion_ = forcedZone;
    defaultRegion_.record = arrow_.get();  // DS:0a6c
    forcedRegion_.record = arrow_.get();   // DS:0a88
    regions_.assign(1, &defaultRegion_);  // List_InsertAt(list, DS:0a54, -1)
    if (arrow_) arrow_->hotX = arrow_->hotY = 0;
}

// 17ab:017f Mouse_SetBounds
void Cursor::setBounds(int x, int y, int w, int h) {
    maxX_ = static_cast<int16_t>(x + w - active_.width);
    maxY_ = static_cast<int16_t>(y + h - active_.height);
    if (mouseX_ > maxX_) mouseX_ = maxX_;
    if (mouseY_ > maxY_) mouseY_ = maxY_;
    if (hooks_.setRangeX) hooks_.setRangeX(x, static_cast<int16_t>(maxX_ * 2));
    if (hooks_.setRangeY) hooks_.setRangeY(y, maxY_);
}

// 17ab:01f0 Mouse_SetCursorPicture
CursorRecord* Cursor::setCursorPicture(CursorRecord* rec) {
    CursorRecord* old = defaultRegion_.record;
    restoreUnder();
    defaultRegion_.record = rec;
    if (flush_) flush_();
    return old;
}

// 17ab:0222 Mouse_SetHotSpot
void Cursor::setHotSpot(CursorRecord* rec, int hx, int hy) {
    CursorRecord& r = rec ? *rec : active_;
    if (hx >= 0) r.hotX = hx;
    if (hy >= 0) r.hotY = hy;
}

// 17ab:0263 Mouse_RestoreDefaultCursor
CursorRecord* Cursor::restoreDefaultCursor() {
    CursorRecord* was = activeRecord_;
    setCursorPicture(arrow_.get());
    return was == arrow_.get() ? nullptr : was;  // the original would free the arrow itself
}

// 17ab:0754 MouseRegion_Add: List_InsertAt(list, item, 0)
void Cursor::addRegion(CursorRegion* region) {
    if (region) regions_.insert(regions_.begin(), region);
}

// 17ab:0771 MouseRegion_AddArray
void Cursor::addRegions(std::span<CursorRegion> regions) {
    for (CursorRegion& r : regions) addRegion(&r);
}

// 17ab:07a4 MouseRegion_Clear: List_Count - 1 times List_RemoveAt(list, 0)
void Cursor::clearRegions() {
    if (regions_.size() > 1) regions_.erase(regions_.begin(), regions_.end() - 1);
}

// 17ab:07dd Mouse_ForceDefault
int Cursor::forceDefault(int on) {
    const int old = forced_;
    forced_ = on;
    return old;
}

// 17ab:0612 Mouse_DrawCursor
void Cursor::draw() {
    if (hidden_) return;
    const CursorRegion* item = nullptr;
    if (forced_) {
        item = &forcedRegion_;
    } else {
        // The walk has no end test: the default region (last) always contains the pointer.
        for (const CursorRegion* r : regions_)
            if (r->contains(mouseX_, mouseY_)) {
                item = r;
                break;
            }
        if (!item) {
            log::debug("Mouse_DrawCursor: no cursor region at ({}, {})", mouseX_, mouseY_);
            if (regions_.empty()) return;
            item = regions_.back();
        }
    }
    if (!item->record) return;
    if (item->record != activeRecord_) {
        active_ = *item->record;  // the 24-byte copy (the record's shown flag included)
        setBounds(0, 0, 319, 199);
        active_.saveX = mouseX_;
        active_.saveY = mouseY_;
        activeRecord_ = item->record;
    }
    if (!active_.saveUnder) active_.saveUnder = std::make_shared<SavedImage>();
    screen_.getImage(mouseX_, mouseY_, active_.width, active_.height, *active_.saveUnder);
    active_.shown = 1;
    active_.saveX = mouseX_;
    active_.saveY = mouseY_;
    screen_.lpxDrawKeyed(mouseX_, mouseY_, active_.picture, 0);
}

// 17ab:072a Mouse_RestoreUnder
void Cursor::restoreUnder() {
    if (!active_.shown) return;
    active_.shown = 0;
    if (active_.saveUnder) screen_.putImage(active_.saveX, active_.saveY, *active_.saveUnder);
}

}  // namespace tmport::gg
