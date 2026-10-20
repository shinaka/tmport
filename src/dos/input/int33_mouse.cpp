#include "dos/input/int33_mouse.h"

#include <algorithm>
#include <utility>

namespace tmport::dos {

Int33Mouse::Int33Mouse(const Int33MouseConfig& config)
    : config_(config),
      minX_(std::min(config.minX, config.maxX)),
      maxX_(std::max(config.minX, config.maxX)),
      minY_(std::min(config.minY, config.maxY)),
      maxY_(std::max(config.minY, config.maxY)),
      x_(config.startX),
      y_(config.startY) {}

uint16_t Int33Mouse::clampX(long x) const { return static_cast<uint16_t>(std::clamp<long>(x, minX_, maxX_)); }
uint16_t Int33Mouse::clampY(long y) const { return static_cast<uint16_t>(std::clamp<long>(y, minY_, maxY_)); }

void Int33Mouse::setLogicalPosition(int x, int y) {
    const uint16_t nx = clampX(static_cast<long>(x) * config_.xMul / config_.xDiv);
    const uint16_t ny = clampY(y);
    const bool moved = nx != x_ || ny != y_;
    x_ = nx;
    y_ = ny;
    if (moved) emit(kEventMove);
}

void Int33Mouse::setButtons(uint16_t mask) {
    const uint16_t down = static_cast<uint16_t>(mask & ~buttons_), up = static_cast<uint16_t>(buttons_ & ~mask);
    buttons_ = mask;
    uint16_t ev = 0;
    if (down & kLeft) ev |= kEventLeftDown;
    if (up & kLeft) ev |= kEventLeftUp;
    if (down & kRight) ev |= kEventRightDown;
    if (up & kRight) ev |= kEventRightUp;
    if (down & kMiddle) ev |= kEventMiddleDown;
    if (up & kMiddle) ev |= kEventMiddleUp;
    if (ev) emit(ev);
}

void Int33Mouse::setPosition(int x, int y) {
    x_ = clampX(x);
    y_ = clampY(y);
}

void Int33Mouse::setRangeX(int min, int max) {
    minX_ = std::min(min, max);
    maxX_ = std::max(min, max);
    x_ = clampX(x_);
}

void Int33Mouse::setRangeY(int min, int max) {
    minY_ = std::min(min, max);
    maxY_ = std::max(min, max);
    y_ = clampY(y_);
}

void Int33Mouse::setEventHandler(uint16_t mask, EventHandler handler) {
    mask_ = mask;
    handler_ = mask ? std::move(handler) : EventHandler{};
}

void Int33Mouse::emit(uint16_t events) {
    if (!handler_ || !present) return;
    const uint16_t ev = static_cast<uint16_t>(events & mask_);
    if (ev == 0) return;
    const EventHandler h = handler_;  // the handler may replace itself
    h(Event{ev, buttons_, x_, y_});
}

}  // namespace tmport::dos
